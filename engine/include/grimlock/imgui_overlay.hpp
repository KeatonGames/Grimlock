#pragma once

#include "grimlock/render_overlay.hpp"

#include <array>
#include <glm/glm.hpp>
#include <imgui.h>
#include <span>

namespace grimlock::ui {

// Optional SDL/Vulkan UI bridge shared by applications, separate from Engine's core dependency surface.
class ImGuiOverlay : public RenderOverlay {
public:
    void initialize(const OverlayContext& context) override;
    void shutdown() noexcept override;
    void processEvent(const SDL_Event& event) override;
    void render(VkCommandBuffer command) override;
    void targetChanged(const OverlayTarget& target) override;

protected:
    virtual void configureUi() = 0;

    virtual void releaseUi() noexcept {
    }

    OverlayContext native_{};
    ImGuiContext* context_ = nullptr;

private:
    void initializeRenderer();
    bool platformReady_ = false, rendererReady_ = false;
};

[[nodiscard]] int projectPolygon(const glm::dmat4& matrix,
                                 std::span<const glm::dvec3> vertices,
                                 ImVec2 offset,
                                 ImVec2 size,
                                 std::array<ImVec2, 12>& points);

} // namespace grimlock::ui
