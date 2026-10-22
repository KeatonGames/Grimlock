#pragma once

#include <array>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <grimlock/ray.hpp>
#include <span>

namespace grimlock {
class Engine;
}

namespace grimlock::editor {

struct ViewportInput {
    std::optional<Ray> ray, pressRay;
    bool cancel = false;
};

struct DebugBox {
    glm::vec3 minimum{}, maximum{};
    glm::vec4 color{1};
    glm::quat rotation{1, 0, 0, 0};
};

struct DebugQuad {
    std::array<glm::vec3, 4> vertices;
    glm::vec4 color{0.2F, 0.7F, 0.5F, 0.08F};
};

// Game-owned development controls hosted in the existing terrain tools menu.
class GridDebug {
public:
    virtual ~GridDebug() = default;
    virtual void menu(Engine& engine) = 0;
    virtual void update(Engine& engine, double delta) = 0;

    virtual void panel(Engine&) {
    }

    [[nodiscard]] virtual std::optional<std::filesystem::path> takeSceneRequest() {
        return {};
    }

    virtual void sceneOpened(Engine&, const std::filesystem::path&) {
    }

    [[nodiscard]] virtual bool capturesSelection() const noexcept {
        return false;
    }

    // Return true when a tool modifies authored scene data.
    virtual bool viewport(Engine&, const ViewportInput&) {
        return false;
    }

    [[nodiscard]] virtual std::span<const DebugBox> boxes() const noexcept {
        return {};
    }

    [[nodiscard]] virtual std::span<const DebugQuad> quads() const noexcept {
        return {};
    }

    // Optional world-space development geometry; Editor owns projection/drawing.
    [[nodiscard]] virtual std::span<const std::array<glm::vec3, 3>> triangles() const noexcept {
        return {};
    }

    // Called before the attached Engine is destroyed; release owned preview instances here.
    virtual void shutdown(Engine&) noexcept {
    }
};

} // namespace grimlock::editor
