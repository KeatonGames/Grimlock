#pragma once

#include "entities/enemy.hpp"

#include <span>
#include <vector>

namespace grimlock::game {

// Reusable open-addressed spatial hash; buckets and links retain their capacity.
class EnemyNeighbors final {
public:
    void build(std::span<const Enemy> enemies, double binSize, glm::dvec3 origin, std::size_t capacity = 0);
    void relocate(std::size_t index, glm::dvec3 before, glm::dvec3 after);
    void insert(std::size_t index, glm::dvec3 position);

    template <class Visitor> void visit(glm::dvec3 position, double range, Visitor visitor) const {
        const auto minimum = key(position - glm::dvec3{range, 0, range});
        const auto maximum = key(position + glm::dvec3{range, 0, range});
        for (auto z = minimum.y; z <= maximum.y; ++z) {
            for (auto x = minimum.x; x <= maximum.x; ++x) {
                const auto& bucket = buckets_[find({x, z})];
                if (!bucket.used) {
                    continue;
                }
                for (auto index = bucket.first; index != none; index = next_[index]) {
                    visitor(index);
                }
            }
        }
    }

private:
    static constexpr std::size_t none = static_cast<std::size_t>(-1);

    struct Bucket {
        glm::i64vec2 key{0};
        std::size_t first = none;
        bool used = false;
    };

    [[nodiscard]] glm::i64vec2 key(glm::dvec3 position) const;
    [[nodiscard]] std::size_t find(glm::i64vec2 key) const;

    std::vector<Bucket> buckets_;
    std::vector<std::size_t> next_;
    double binSize_ = 1;
    glm::dvec3 origin_{0};
};

struct CrowdMetrics {
    std::uint64_t neighborChecks = 0;
    std::size_t substeps = 0;
};

class CrowdMovement final {
public:
    void update(const NavMesh& navigation,
                std::optional<glm::dvec2> destination,
                std::span<Enemy> enemies,
                double delta,
                bool despawnOnArrival = true);
    void update(const NavMesh& navigation, std::span<Enemy> enemies, double delta, bool despawnOnArrival = true);

    [[nodiscard]] const CrowdMetrics& metrics() const noexcept {
        return metrics_;
    }

private:
    [[nodiscard]] glm::dvec2 separation(std::size_t index, std::span<const Enemy> enemies, double maximumRadius);
    [[nodiscard]] glm::dvec3 move(const NavMesh& navigation,
                                  std::size_t index,
                                  std::span<const Enemy> enemies,
                                  glm::dvec2 displacement,
                                  double maximumRadius);
    void resolveOverlaps(const NavMesh& navigation, std::span<Enemy> enemies, double maximumRadius);
    void step(const NavMesh& navigation,
              std::span<Enemy> enemies,
              double delta,
              double maximumRadius,
              bool despawnOnArrival);
    EnemyNeighbors neighbors_;
    std::vector<double> remaining_;
    CrowdMetrics metrics_;
    std::uint64_t order_ = 0;
    std::optional<glm::dvec2> sharedDestination_;
};

} // namespace grimlock::game
