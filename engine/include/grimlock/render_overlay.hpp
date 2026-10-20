#pragma once

// Opt-in native rendering bridge. Engine's normal public API has no SDL/Vulkan includes.
#include <cstdint>
#include <vulkan/vulkan.h>

struct SDL_Window;
union SDL_Event;

namespace grimlock {

struct Camera;

struct OverlayTarget {
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    std::uint32_t minimumImages = 2, images = 0;
};

struct OverlayContext {
    SDL_Window* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    OverlayTarget target;
};

struct OverlayInput {
    bool mouse = false, keyboard = false, close = false;
    bool selection = false; // A viewport tool owns scene-picking clicks, not camera input.
};

// Non-owning attachment: detach before destroying the implementation. No Engine
// or Scene UI policy lives here; callbacks run on the Engine thread only.
class RenderOverlay {
public:
    virtual ~RenderOverlay() = default;
    virtual void initialize(const OverlayContext& context) = 0;
    virtual void shutdown() noexcept = 0; // Called after device idle, including failed attach.
    virtual void processEvent(const SDL_Event& event) = 0;
    virtual OverlayInput frame() = 0; // Polling is complete; mutations precede scene preparation.

    // Final camera is available; finish world-space debug UI here.
    virtual void finishFrame(const Camera&) {
    }

    virtual void render(VkCommandBuffer command) = 0;            // After tone mapping, inside color rendering.
    virtual void targetChanged(const OverlayTarget& target) = 0; // Device is idle.
    virtual bool requestClose() = 0;                             // False defers closing (e.g. unsaved document).
};

} // namespace grimlock
