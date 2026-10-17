#pragma once
#include "grimlock/assets/model_data.hpp"
#include <memory>

namespace grimlock::assets {
struct ModelPose {
    std::vector<glm::mat4> local, world;
    std::vector<std::vector<float>> weights;
    std::vector<std::vector<glm::mat4>> joints; // Per mesh node, in mesh-local coordinates.
    std::vector<std::size_t> activeNodes;
};

// Immutable shared asset; playback and pose belong solely to this instance.
class ModelInstance final {
public:
    explicit ModelInstance(std::shared_ptr<const ModelData> model);
    void play(std::size_t clip = 0, bool loop = true);
    void stop(bool restoreRestPose = true);
    void pause() noexcept { paused_ = true; }
    void resume() noexcept { paused_ = false; }
    void setSpeed(float speed);
    void seek(float seconds);
    void update(double deltaSeconds);
    [[nodiscard]] bool playing() const noexcept { return playing_ && !paused_; }
    [[nodiscard]] float time() const noexcept { return time_; }
    [[nodiscard]] std::size_t clip() const noexcept { return clip_; }
    [[nodiscard]] float speed() const noexcept { return speed_; }
    [[nodiscard]] bool looping() const noexcept { return loop_; }
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    [[nodiscard]] bool running() const noexcept { return playing_; }
    [[nodiscard]] bool restPose() const noexcept { return !animatedPose_; }
    [[nodiscard]] const ModelPose& pose() const noexcept { return pose_; }
    [[nodiscard]] const ModelData& model() const noexcept { return *model_; }
    [[nodiscard]] const std::shared_ptr<const ModelData>& asset() const noexcept { return model_; }
private:
    void evaluate(bool animated);
    std::shared_ptr<const ModelData> model_;
    ModelPose pose_;
    std::vector<std::size_t> order_;
    std::vector<glm::vec3> translations_, scales_;
    std::vector<glm::quat> rotations_;
    std::vector<float> sampled_;
    std::size_t clip_ = 0;
    float time_ = 0, speed_ = 1;
    bool playing_ = false, paused_ = false, loop_ = true;
    bool animatedPose_ = false;
};
} // namespace grimlock::assets
