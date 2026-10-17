#include "core/startup_profile.hpp"
#include "assets/environment.hpp"
#include "assets/image_loader.hpp"

#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace grimlock::assets {
namespace {
constexpr float pi = glm::pi<float>();
constexpr std::uint32_t samples = 128, specularSize = 64, irradianceSize = 8, lutSize = 64;

glm::vec2 hammersley(std::uint32_t i, std::uint32_t count) {
    auto bits = i;
    bits = (bits << 16U) | (bits >> 16U);
    bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
    bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
    bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
    bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
    return {static_cast<float>(i) / count, static_cast<float>(bits) * 2.3283064365386963e-10F};
}

glm::mat3 basis(const glm::vec3 n) {
    const auto t = glm::normalize(glm::cross(std::abs(n.z) < 0.999F ? glm::vec3{0, 0, 1} : glm::vec3{1, 0, 0}, n));
    return {t, glm::cross(n, t), n};
}

glm::vec3 ggx(const glm::vec2 xi, float roughness) {
    const float a = roughness * roughness;
    const float cosTheta = std::sqrt((1 - xi.y) / (1 + (a * a - 1) * xi.y));
    const float sinTheta = std::sqrt(std::max(0.0F, 1 - cosTheta * cosTheta));
    return {std::cos(2 * pi * xi.x) * sinTheta, std::sin(2 * pi * xi.x) * sinTheta, cosTheta};
}

std::array<glm::vec3,samples> ggxSamples(float roughness) {
    std::array<glm::vec3,samples> result;
    for (std::uint32_t sample=0;sample<samples;++sample) { result[sample]=ggx(hammersley(sample,samples),roughness); }
    return result;
}

glm::vec3 sampleCube(const EnvironmentCube& cube, const glm::vec3 d) {
    const auto a = glm::abs(d);
    std::size_t face;
    glm::vec2 uv;
    if (a.x >= a.y && a.x >= a.z) {
        face = d.x > 0 ? 0 : 1; uv = {d.x > 0 ? -d.z : d.z, -d.y}; uv /= a.x;
    } else if (a.y >= a.z) {
        face = d.y > 0 ? 2 : 3; uv = {d.x, d.y > 0 ? d.z : -d.z}; uv /= a.y;
    } else {
        face = d.z > 0 ? 4 : 5; uv = {d.z > 0 ? d.x : -d.x, -d.y}; uv /= a.z;
    }
    const auto& image = cube[face];
    uv = (uv * 0.5F + 0.5F) * static_cast<float>(image.width) - 0.5F;
    const int x = static_cast<int>(std::floor(uv.x)), y = static_cast<int>(std::floor(uv.y));
    const auto pixel = [&](int px, int py) {
        const auto last = static_cast<int>(image.width) - 1;
        return glm::vec3{image.pixels[static_cast<std::size_t>(std::clamp(py, 0, last)) * image.width + std::clamp(px, 0, last)]};
    };
    return glm::mix(glm::mix(pixel(x, y), pixel(x + 1, y), uv.x - x),
                    glm::mix(pixel(x, y + 1), pixel(x + 1, y + 1), uv.x - x), uv.y - y);
}

FloatImageData image(const std::uint32_t size, const std::string& name) {
    return {name, size, size, std::vector<glm::vec4>(static_cast<std::size_t>(size) * size)};
}

float smithG1(float cosine, float alphaSquared) {
    return 2 * cosine / std::max(cosine + std::sqrt(alphaSquared + (1 - alphaSquared) * cosine * cosine), 1.0e-6F);
}
}

glm::vec3 cubeDirection(const std::size_t face, const float u, const float v) {
    switch (face) {
    case 0: return glm::normalize(glm::vec3{1, -v, -u});
    case 1: return glm::normalize(glm::vec3{-1, -v, u});
    case 2: return glm::normalize(glm::vec3{u, 1, v});
    case 3: return glm::normalize(glm::vec3{u, -1, -v});
    case 4: return glm::normalize(glm::vec3{u, -v, 1});
    case 5: return glm::normalize(glm::vec3{-u, -v, -1});
    default: throw std::invalid_argument("Invalid environment cube face.");
    }
}

EnvironmentCube loadEnvironmentCube(const std::filesystem::path& folder) {
    constexpr std::array names{"px", "nx", "py", "ny", "pz", "nz"};
    EnvironmentCube cube;
    for (std::size_t face = 0; face < cube.size(); ++face) {
        std::filesystem::path path;
        for (const auto extension : {".hdr", ".png", ".jpg", ".jpeg"}) {
            const auto candidate = folder / (std::string{names[face]} + extension);
            if (std::filesystem::is_regular_file(candidate)) { path = candidate; break; }
        }
        if (path.empty()) { throw std::runtime_error("Missing cubemap face " + std::string{names[face]} + " in " + folder.string()); }
        cube[face] = loadLinearImage(path);
    }
    return cube;
}

EnvironmentCube fallbackEnvironment() {
    EnvironmentCube cube;
    for (std::size_t face = 0; face < cube.size(); ++face) {
        cube[face] = image(32, "fallback studio environment");
        for (std::uint32_t y = 0; y < 32; ++y) {
            for (std::uint32_t x = 0; x < 32; ++x) {
                const auto d = cubeDirection(face, 2 * (x + 0.5F) / 32 - 1, 2 * (y + 0.5F) / 32 - 1);
                auto color = glm::vec3{0.08F, 0.10F, 0.14F} + std::max(d.y, 0.0F) * glm::vec3{0.12F, 0.14F, 0.16F};
                color += std::pow(std::max(glm::dot(d, glm::normalize(glm::vec3{-1, 2, 1})), 0.0F), 32.0F) * glm::vec3{6, 5.4F, 4.8F};
                color += std::pow(std::max(glm::dot(d, glm::normalize(glm::vec3{2, 1, -1})), 0.0F), 64.0F) * glm::vec3{2, 3, 5};
                cube[face].pixels[static_cast<std::size_t>(y) * 32 + x] = {color, 1};
            }
        }
    }
    return cube;
}

EnvironmentMaps preprocessEnvironment(const EnvironmentCube& source) {
    diagnostics::StartupSpan profile{"environment.preprocess_CPU"};
    for (const auto& face : source) {
        if (face.width == 0 || face.width != face.height || face.width != source[0].width
            || face.pixels.size() != static_cast<std::size_t>(face.width) * face.height) {
            throw std::invalid_argument("Environment needs six equally sized, square faces.");
        }
        for (const auto& pixel : face.pixels) {
            for (int c = 0; c < 3; ++c) {
                if (!std::isfinite(pixel[c]) || pixel[c] < 0) { throw std::invalid_argument("Environment radiance must be finite and non-negative."); }
            }
        }
    }
    // Directions depend on sample index/roughness, not the output texel. Reuse
    // exactly the same samples and accumulation order without repeating trig.
    std::array<glm::vec3,samples> diffuseDirections;
    for (std::uint32_t sample=0;sample<samples;++sample) {
        const auto xi=hammersley(sample,samples);
        const float r=std::sqrt(xi.x),phi=2*pi*xi.y;
        diffuseDirections[sample]={r*std::cos(phi),r*std::sin(phi),std::sqrt(1-xi.x)};
    }
    EnvironmentMaps result;
    for (std::size_t face = 0; face < 6; ++face) {
        auto& irradiance = result.irradiance[face] = image(irradianceSize, "diffuse irradiance");
        for (std::uint32_t y = 0; y < irradianceSize; ++y) {
            for (std::uint32_t x = 0; x < irradianceSize; ++x) {
                const auto n = cubeDirection(face, 2 * (x + 0.5F) / irradianceSize - 1, 2 * (y + 0.5F) / irradianceSize - 1);
                const auto frame = basis(n);
                glm::vec3 sum{0};
                for (std::uint32_t sample = 0; sample < samples; ++sample) {
                    sum += sampleCube(source, frame * diffuseDirections[sample]);
                }
                irradiance.pixels[static_cast<std::size_t>(y) * irradianceSize + x] = {sum * (pi / samples), 1};
            }
        }
    }
    const auto levels = std::bit_width(specularSize);
    result.specular.reserve(levels * 6);
    for (std::uint32_t mip = 0; mip < levels; ++mip) {
        const auto size = specularSize >> mip;
        const float roughness = static_cast<float>(mip) / (levels - 1);
        const auto halfVectors = mip>0 ? ggxSamples(roughness) : std::array<glm::vec3,samples>{};
        for (std::size_t face = 0; face < 6; ++face) {
            auto prefiltered = image(size, "GGX prefiltered radiance");
            for (std::uint32_t y = 0; y < size; ++y) {
                for (std::uint32_t x = 0; x < size; ++x) {
                    const auto n = cubeDirection(face, 2 * (x + 0.5F) / size - 1, 2 * (y + 0.5F) / size - 1);
                    glm::vec3 sum{0}; float weight = 0;
                    if (mip == 0) { sum = sampleCube(source, n); weight = 1; }
                    else {
                        const auto frame = basis(n);
                        for (std::uint32_t sample = 0; sample < samples; ++sample) {
                            const auto h = frame * halfVectors[sample];
                            const auto l = glm::reflect(-n, h);
                            const auto cosine = std::max(glm::dot(n, l), 0.0F);
                            if (cosine > 0) { sum += sampleCube(source, l) * cosine; weight += cosine; }
                        }
                    }
                    prefiltered.pixels[static_cast<std::size_t>(y) * size + x] = {sum / std::max(weight, 1.0e-6F), 1};
                }
            }
            result.specular.push_back(std::move(prefiltered));
        }
    }
    result.brdf = image(lutSize, "split-sum BRDF integration");
    for (std::uint32_t y = 0; y < lutSize; ++y) {
        const float roughness = (y + 0.5F) / lutSize, alpha = roughness * roughness;
        const auto halfVectors = ggxSamples(roughness);
        for (std::uint32_t x = 0; x < lutSize; ++x) {
            const float nv = (x + 0.5F) / lutSize;
            const glm::vec3 v{std::sqrt(1 - nv * nv), 0, nv};
            glm::vec2 sum{0};
            for (std::uint32_t sample = 0; sample < samples; ++sample) {
                const auto h = halfVectors[sample];
                const auto l = glm::reflect(-v, h);
                const float nl = std::max(l.z, 0.0F), nh = std::max(h.z, 0.0F), vh = std::max(glm::dot(v, h), 0.0F);
                if (nl > 0 && nh > 0) {
                    const float visibility = smithG1(nv, alpha * alpha) * smithG1(nl, alpha * alpha) * vh / (nh * nv);
                    const float fresnel = std::pow(1 - vh, 5.0F);
                    sum += glm::vec2{1 - fresnel, fresnel} * visibility;
                }
            }
            result.brdf.pixels[static_cast<std::size_t>(y) * lutSize + x] = {sum / static_cast<float>(samples), 0, 1};
        }
    }
    return result;
}

} // namespace grimlock::assets
