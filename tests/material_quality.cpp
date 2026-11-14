#include "grimlock/color.hpp"
#include "platform/window.hpp"
#include "renderer/mipmaps.hpp"
#include "renderer/vulkan_renderer.hpp"

#include <SDL3/SDL.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace grimlock;
using namespace grimlock::renderer;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void checkMips() {
    assets::ImageData image{"mip test", 2, 2, {0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 255, 255, 255, 255}};
    const auto color = rgbaMipChain(image, true);
    const auto data = rgbaMipChain(image, false);
    require(color.size() == 2 && color.back().pixels[0] == 188 && color.back().pixels[3] == 128,
            "sRGB mip averaging or linear alpha is incorrect");
    require(data.back().pixels[0] == 128, "Data mip was gamma corrected");
    image = {"odd", 3, 1, {0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255}};
    require(rgbaMipChain(image, false).back().pixels[0] == 85, "Odd mip extent dropped edge texels");
    for (float value : {0.0F, 0.02F, 0.18F, 0.5F, 1.0F}) {
        require(std::abs(linearToSrgb(srgbToLinear(value)) - value) < 0.00001F, "sRGB transfer roundtrip failed");
    }
}

assets::ModelData quad(bool textured) {
    assets::ModelData model;
    model.materials.resize(1);
    model.materials[0].unlit = true;
    model.materials[0].baseColor = textured ? glm::vec4{1} : glm::vec4{0.5F, 0.5F, 0.5F, 1};
    model.meshes.resize(1);
    auto& mesh = model.meshes[0];
    mesh.vertices.resize(4);
    mesh.indices = {0, 1, 2, 2, 3, 0};
    const glm::vec3 positions[]{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
    for (int i = 0; i < 4; ++i) {
        mesh.vertices[i].position = positions[i];
        mesh.vertices[i].normal = {0, 0, 1};
        mesh.vertices[i].uv = {positions[i].x * 0.5F + 0.5F, positions[i].y * 0.5F + 0.5F};
        mesh.vertices[i].color = textured ? glm::vec4{1} : glm::vec4{0.5F, 0.5F, 0.5F, 1};
    }
    model.nodes.resize(1);
    model.nodes[0].meshes = {0};
    model.roots = {0};
    if (textured) {
        model.images.push_back(assets::ImageData{"sRGB gray", 2, 2, std::vector<std::uint8_t>(16, 128)});
        for (int i = 3; i < 16; i += 4) {
            model.images.back()->pixels[i] = 255;
        }
        model.textures.push_back({0, {}});
        model.materials[0].baseColorTexture = assets::TextureReference{0};
    }
    return model;
}

FrameCapture capture(VulkanRenderer& renderer, std::size_t model) {
    Camera camera;
    camera.position = {0, 0, 4};
    camera.target = {0, 0, 0};
    const ModelDraw draw{.model = model};
    renderer.requestCapture();
    for (int i = 0; i < 4; ++i) {
        SDL_PumpEvents();
        if (renderer.drawFrame(camera, std::span{&draw, 1})) {
            return renderer.takeCapture();
        }
    }
    throw std::runtime_error("No material frame");
}

int center(const FrameCapture& image) {
    require(image.width > 0, "Capture unavailable");
    return image.rgba[(static_cast<std::size_t>(image.height / 2) * image.width + image.width / 2) * 4];
}

void gpuChecks(SDL_Window* window) {
    // Exercise the CPU upload path even on GPUs that support linear blits.
    {
        VulkanContext context{window};
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        info.queueFamilyIndex = context.graphicsFamily();
        require(vkCreateCommandPool(context.device(), &info, nullptr, &pool) == VK_SUCCESS, "Mip upload pool failed");
        try {
            const assets::ImageData image{"fallback", 3, 5, std::vector<std::uint8_t>(3 * 5 * 4, 128)};
            Texture texture{context, pool, image, TextureColorSpace::srgb, true};
            require(texture.mipLevels() == 3, "CPU fallback did not upload full mip chain");
        } catch (...) {
            vkDestroyCommandPool(context.device(), pool, nullptr);
            throw;
        }
        vkDestroyCommandPool(context.device(), pool, nullptr);
    }
    VulkanRenderer renderer{window};
    const auto colored = renderer.uploadModel(quad(false));
    const auto textured = renderer.uploadModel(quad(true));
    auto settings = renderer.renderSettings();
    auto invalid = settings;
    invalid.anisotropy = 3;
    bool rejected = false;
    try {
        renderer.setRenderSettings(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && renderer.renderSettings() == settings, "Invalid filtering settings mutated renderer state");
    settings.toneMapping = ToneMapping::linear;
    renderer.setRenderSettings(settings);
    require(std::abs(center(capture(renderer, colored)) - 137) <= 2,
            "Vertex/material colors were not multiplied in linear space");
    require(std::abs(center(capture(renderer, textured)) - 128) <= 2,
            "sRGB texture/output was double encoded or not decoded");
    auto translucent = quad(false);
    for (auto& vertex : translucent.meshes[0].vertices) {
        vertex.color.a = 0.25F;
    }
    translucent.materials[0].alphaMode = assets::AlphaMode::mask;
    const auto masked = capture(renderer, renderer.uploadModel(translucent));
    require(center(masked) == masked.rgba[0], "Vertex alpha did not participate in alpha masking");
    translucent.materials[0].alphaMode = assets::AlphaMode::blend;
    const auto blended = capture(renderer, renderer.uploadModel(translucent));
    require(center(blended) > center(masked) && center(blended) < 137,
            "Vertex alpha did not preserve transparent blending");
    for (auto filtering : {TextureFiltering::bilinear, TextureFiltering::trilinear, TextureFiltering::anisotropic}) {
        settings.textureFiltering = filtering;
        for (auto anisotropy : renderer.supportedAnisotropy()) {
            settings.anisotropy = anisotropy;
            renderer.setRenderSettings(settings);
            require(std::abs(center(capture(renderer, textured)) - 128) <= 2,
                    "Filtering changed constant texture colors");
        }
    }
    for (auto tone : {ToneMapping::reinhard, ToneMapping::filmic}) {
        settings.toneMapping = tone;
        renderer.setRenderSettings(settings);
        const float linear = tone == ToneMapping::reinhard
                                 ? 0.25F / 1.25F
                                 : (0.25F * (2.51F * 0.25F + 0.03F)) / (0.25F * (2.43F * 0.25F + 0.59F) + 0.14F);
        require(std::abs(center(capture(renderer, colored)) - int(std::lround(linearToSrgb(linear) * 255))) <= 2,
                "Tone mapping was not applied before output encoding");
    }

    auto pattern = quad(true);
    auto& image = *pattern.images[0];
    image.width = image.height = 256;
    image.pixels.resize(256 * 256 * 4);
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            const auto offset = (y * 256 + x) * 4;
            const auto value = static_cast<std::uint8_t>(((x / 8) ^ (y / 8)) & 1 ? 255 : 0);
            image.pixels[offset] = image.pixels[offset + 1] = image.pixels[offset + 2] = value;
            image.pixels[offset + 3] = 255;
        }
    }
    for (auto& vertex : pattern.meshes[0].vertices) {
        vertex.uv *= 8;
        vertex.position.z = vertex.position.x * 0.7F;
        vertex.position.x *= 0.7F;
    }
    const auto filteredModel = renderer.uploadModel(pattern);
    settings.toneMapping = ToneMapping::linear;
    settings.textureFiltering = TextureFiltering::bilinear;
    renderer.setRenderSettings(settings);
    const auto bilinear = capture(renderer, filteredModel);
    settings.textureFiltering = TextureFiltering::trilinear;
    renderer.setRenderSettings(settings);
    const auto trilinear = capture(renderer, filteredModel);
    require(bilinear.rgba != trilinear.rgba, "Filtering preference did not affect minified texture samples");
    if (renderer.supportedAnisotropy().back() > 1) {
        settings.textureFiltering = TextureFiltering::anisotropic;
        settings.anisotropy = renderer.supportedAnisotropy().back();
        renderer.setRenderSettings(settings);
        require(capture(renderer, filteredModel).rgba != trilinear.rgba,
                "Anisotropy did not affect grazing-angle samples");
    }
    pattern.textures[0].sampler.min = pattern.textures[0].sampler.mag = assets::TextureFilter::nearest;
    pattern.textures[0].sampler.mip = assets::MipmapFilter::none;
    const auto paletteModel = renderer.uploadModel(pattern);
    const auto nearest = capture(renderer, paletteModel);
    settings.textureFiltering = TextureFiltering::bilinear;
    renderer.setRenderSettings(settings);
    require(capture(renderer, paletteModel).rgba == nearest.rgba,
            "Filtering override blurred an authored nearest palette");
}
} // namespace

int main() {
    try {
        checkMips();
        platform::Window window{"Material/color checks", 640, 480};
        gpuChecks(window.native());
        std::cout
            << "PASS linear vertex/material colors, sRGB output, mip fallback, filtering modes and tone mapping.\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL material quality: " << error.what() << '\n';
        return 1;
    }
}
