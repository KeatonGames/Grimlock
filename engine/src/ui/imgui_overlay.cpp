#include <algorithm>
#include <cmath>
#include <grimlock/imgui_overlay.hpp>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>
#include <string>

namespace grimlock::ui {
namespace {
void check(VkResult result) {
    if (result < 0) {
        throw std::runtime_error("Dear ImGui Vulkan backend failed (VkResult " + std::to_string(result) + ").");
    }
}

} // namespace

void ImGuiOverlay::initialize(const OverlayContext& context) {
    native_ = context;
    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    auto& io = ImGui::GetIO();

    io.IniFilename = nullptr;
    io.LogFilename = nullptr; // Session layout; no source-tree or user-profile artifacts.
    configureUi();

    if (!ImGui_ImplSDL3_InitForVulkan(native_.window)) {
        throw std::runtime_error("Dear ImGui SDL3 initialization failed.");
    }

    platformReady_ = true;
    initializeRenderer();
}

void ImGuiOverlay::initializeRenderer() {
    if (native_.target.images < native_.target.minimumImages) {
        throw std::runtime_error("UI overlay requires at least two swapchain images.");
    }

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = native_.instance;
    info.PhysicalDevice = native_.physicalDevice;
    info.Device = native_.device;
    info.QueueFamily = native_.queueFamily;
    info.Queue = native_.queue;
    info.DescriptorPoolSize = 64;
    info.MinImageCount = native_.target.minimumImages;
    info.ImageCount = native_.target.images;
    info.UseDynamicRendering = true;
    info.MinAllocationSize = 1024 * 1024;
    info.CheckVkResultFn = check;
    auto& pipeline = info.PipelineInfoMain;
    pipeline.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    pipeline.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    pipeline.PipelineRenderingCreateInfo.pColorAttachmentFormats = &native_.target.colorFormat;

    // If initialization throws after allocating backend state, shutdown must still clean it.
    try {
        if (!ImGui_ImplVulkan_Init(&info)) {
            throw std::runtime_error("Dear ImGui Vulkan initialization failed.");
        }

        rendererReady_ = true;
    } catch (...) {
        rendererReady_ = ImGui::GetIO().BackendRendererUserData != nullptr;
        throw;
    }
}

void ImGuiOverlay::shutdown() noexcept {
    releaseUi();
    if (!context_) {
        return;
    }

    ImGui::SetCurrentContext(context_);

    if (rendererReady_) {
        ImGui_ImplVulkan_Shutdown();
        rendererReady_ = false;
    }

    if (platformReady_) {
        ImGui_ImplSDL3_Shutdown();
        platformReady_ = false;
    }

    ImGui::DestroyContext(context_);
    context_ = nullptr;
}

void ImGuiOverlay::targetChanged(const OverlayTarget& target) {
    if (native_.target.colorFormat == target.colorFormat && native_.target.images == target.images &&
        native_.target.minimumImages == target.minimumImages) {
        return;
    }

    ImGui::SetCurrentContext(context_);
    ImGui_ImplVulkan_Shutdown();
    rendererReady_ = false;
    native_.target = target;
    initializeRenderer(); // CPU context, panels and document survive.
}

void ImGuiOverlay::processEvent(const SDL_Event& event) {
    ImGui::SetCurrentContext(context_);
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void ImGuiOverlay::render(VkCommandBuffer command) {
    ImGui::SetCurrentContext(context_);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

// Clip in Vulkan homogeneous coordinates before perspective division, including the near plane.
int projectPolygon(const glm::dmat4& matrix,
                   std::span<const glm::dvec3> vertices,
                   ImVec2 offset,
                   ImVec2 size,
                   std::array<ImVec2, 12>& points) {
    if (vertices.size() < 3 || vertices.size() > 4) {
        return 0;
    }
    std::array<glm::dvec4, 12> polygon{}, clipped{};
    int count = static_cast<int>(vertices.size());
    for (int i = 0; i < count; ++i) {
        polygon[i] = matrix * glm::dvec4{vertices[i], 1};
    }

    const auto distance = [](glm::dvec4 p, int plane) {
        switch (plane) {
        case 0:
            return p.w + p.x;
        case 1:
            return p.w - p.x;
        case 2:
            return p.w + p.y;
        case 3:
            return p.w - p.y;
        case 4:
            return p.z;
        default:
            return p.w - p.z;
        }
    };

    for (int plane = 0; plane < 6 && count; ++plane) {
        int output = 0;
        auto previous = polygon[count - 1];
        double before = distance(previous, plane);

        for (int i = 0; i < count; ++i) {
            const auto current = polygon[i];
            const double after = distance(current, plane);

            if ((before >= 0) != (after >= 0)) {
                clipped[output++] = previous + (current - previous) * (before / (before - after));
            }

            if (after >= 0) {
                clipped[output++] = current;
            }

            previous = current;
            before = after;
        }

        polygon = clipped;
        count = output;
    }

    if (count < 3) {
        return 0;
    }

    for (int i = 0; i < count; ++i) {
        const auto p = polygon[i];

        if (!std::isfinite(p.w) || p.w <= 0) {
            return 0;
        }

        points[i] = {offset.x + static_cast<float>((p.x / p.w + 1) * 0.5) * size.x,
                     offset.y + static_cast<float>((p.y / p.w + 1) * 0.5) * size.y};
    }

    float area = 0;

    for (int i = 0; i < count; ++i) {
        const auto a = points[i], b = points[(i + 1) % count];
        area += a.x * b.y - b.x * a.y;
    }

    if (!std::isfinite(area) || std::abs(area) < 0.1F) {
        return 0;
    }

    if (area < 0) {
        std::reverse(points.begin(), points.begin() + count);
    }

    return count;
}

} // namespace grimlock::ui
