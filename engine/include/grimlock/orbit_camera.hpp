#pragma once
#include "grimlock/camera.hpp"
#include "grimlock/input.hpp"

namespace grimlock {
struct OrbitCameraConfig {
    glm::vec3 target{0};
    float distance = 10, yaw = 0, pitch = 0.610865238F; // Angles in radians, pitch above XZ.
    float panSensitivity = 1, rotationSensitivity = 0.005F, zoomSensitivity = 0.15F;
    float minDistance = 0.5F, maxDistance = 100;
    float minPitch = 0.087266463F, maxPitch = 1.483529864F; // 5 to 85 degrees.
};

// Changes the existing Camera, not a second view/projection system. Mouse deltas
// are displacements, so they are intentionally not multiplied by frame delta.
class OrbitCameraController final {
public:
    explicit OrbitCameraController(const OrbitCameraConfig& config = {});
    void configure(const OrbitCameraConfig& config);

    [[nodiscard]] const OrbitCameraConfig& config() const noexcept { return config_; }

    // Derives target/distance/angles, retaining sensitivity/bounds. Applies only
    // necessary limits/world-up correction, preserving normal authored poses.
    void synchronize(Camera& camera);
    void apply(Camera& camera) const;
    [[nodiscard]] bool update(Camera& camera, const InputState& input, float windowHeight);
    void cancelDrag() noexcept;

private:
    enum class Drag { none, pan, orbit };
    OrbitCameraConfig config_;
    Drag drag_ = Drag::none;
};
} // namespace grimlock
