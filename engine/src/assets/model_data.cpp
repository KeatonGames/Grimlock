#include "grimlock/assets/model_data.hpp"

#include "core/startup_profile.hpp"
#include "grimlock/assets/animation.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace grimlock::assets {

std::vector<ModelNodeTransform> modelNodeTransforms(const ModelData& model) {
    const auto invalid = [](const char* reason) {
        throw std::invalid_argument(reason);
    };

    std::vector<bool> hasParent(model.nodes.size(), false);

    for (std::size_t index = 0; index < model.nodes.size(); ++index) {
        const auto& node = model.nodes[index];

        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                if (!std::isfinite(node.localTransform[c][r])) {
                    invalid("Non-finite node transform.");
                }
            }
        }

        if (node.localTransform[0][3] != 0.0F || node.localTransform[1][3] != 0.0F ||
            node.localTransform[2][3] != 0.0F || node.localTransform[3][3] != 1.0F) {
            invalid("Node transforms must be affine.");
        }

        for (const auto mesh : node.meshes) {
            if (mesh >= model.meshes.size()) {
                invalid("Node mesh reference is out of range.");
            }
        }

        for (const auto child : node.children) {
            if (child >= model.nodes.size() || hasParent[child] || model.nodes[child].parent != index) {
                invalid("Invalid or multiple node parent/child references.");
            }

            hasParent[child] = true;
        }
    }

    std::vector<std::size_t> pending;

    for (std::size_t index = 0; index < model.nodes.size(); ++index) {
        if (hasParent[index] != model.nodes[index].parent.has_value()) {
            invalid("Inconsistent node parent.");
        }

        if (!hasParent[index]) {
            pending.push_back(index);
        }
    }

    // Check even nodes outside the selected scene. Iterative traversal avoids stack overflow.
    std::size_t visited = 0;

    for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
        ++visited;
        const auto& children = model.nodes[pending[cursor]].children;
        pending.insert(pending.end(), children.begin(), children.end());
    }

    if (visited != model.nodes.size()) {
        invalid("Cycle in model node hierarchy.");
    }

    std::vector<bool> selected(model.nodes.size(), false);
    std::vector<ModelNodeTransform> result;
    result.reserve(model.nodes.size());

    for (const auto root : model.roots) {
        if (root >= model.nodes.size() || model.nodes[root].parent || selected[root]) {
            invalid("Invalid or repeated scene root.");
        }

        selected[root] = true;
        result.push_back({root, model.nodes[root].localTransform});
    }

    for (std::size_t cursor = 0; cursor < result.size(); ++cursor) {
        const auto current = result[cursor];

        for (const auto child : model.nodes[current.node].children) {
            result.push_back({child, current.world * model.nodes[child].localTransform});
        }
    }

    return result;
}

void updateModelBounds(ModelData& model) {
    diagnostics::StartupSpan profile{"asset.initial_bounds"};
    model.minimum = glm::vec3{std::numeric_limits<float>::max()};
    model.maximum = glm::vec3{std::numeric_limits<float>::lowest()};
    bool hasGeometry = false;

    // One-time CPU position evaluation is only for framing/bounds, never the rendering path.
    ModelInstance instance{std::shared_ptr<const ModelData>{&model, [](const auto*) {
                                                            }}};
    if (!model.animations.empty()) {
        instance.play(0);
    }

    const auto& pose = instance.pose();

    for (const auto& entry : modelNodeTransforms(model)) {
        for (const auto mesh : model.nodes[entry.node].meshes) {
            for (const auto& vertex : model.meshes[mesh].vertices) {
                const auto& geometry = model.meshes[mesh];
                auto local = glm::vec4{vertex.position, 1};

                for (std::uint32_t target = 0; target < geometry.morphTargetCount; ++target) {
                    const auto morphIndex =
                        static_cast<std::size_t>(target) * geometry.morphVertexCount + vertex.morphIndex;
                    const auto& delta = geometry.morphDeltas.at(morphIndex);
                    local += delta.position * pose.weights[entry.node].at(target);
                }

                if (!pose.joints[entry.node].empty()) {
                    glm::mat4 skin{0};

                    for (int c = 0; c < 4; ++c) {
                        skin += pose.joints[entry.node].at(vertex.joints0[c]) * vertex.weights0[c];
                        skin += pose.joints[entry.node].at(vertex.joints1[c]) * vertex.weights1[c];
                    }

                    local = skin * local;
                }

                const glm::vec3 point{pose.world[entry.node] * local};

                if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
                    throw std::invalid_argument("Non-finite transformed model bounds.");
                }

                model.minimum = glm::min(model.minimum, point);
                model.maximum = glm::max(model.maximum, point);
                hasGeometry = true;
            }
        }
    }

    if (!hasGeometry) {
        throw std::invalid_argument("Selected model scene has no triangle geometry.");
    }
}

} // namespace grimlock::assets
