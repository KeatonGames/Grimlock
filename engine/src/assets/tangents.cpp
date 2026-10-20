#include "assets/tangents.hpp"

#include "grimlock/assets/model_data.hpp"

#include <cmath>
#include <limits>
#include <mikktspace.h>
#include <stdexcept>

namespace grimlock::assets {

namespace {

struct Context {
    MeshData& mesh;
    const TextureReference& coordinates;
    std::vector<glm::vec4> corners;
    std::vector<bool> validFaces;
};

Context& data(const SMikkTSpaceContext* context) {
    return *static_cast<Context*>(context->m_pUserData);
}

const Vertex& vertex(const SMikkTSpaceContext* context, int face, int corner) {
    auto& mesh = data(context).mesh;

    return mesh.vertices[mesh.indices[static_cast<std::size_t>(face) * 3 + corner]];
}

glm::vec2 uv(const Vertex& vertex, const TextureReference& reference) {
    const auto value = (reference.uvSet == 0 ? vertex.uv : vertex.uv1) * reference.scale;
    const float c = std::cos(reference.rotation), s = std::sin(reference.rotation);

    return glm::vec2{c * value.x - s * value.y, s * value.x + c * value.y} + reference.offset;
}

} // namespace

void generateTangents(MeshData& mesh, const TextureReference& coordinates) {
    if (mesh.indices.size() / 3 > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Mesh exceeds MikkTSpace face limits.");
    }

    Context work{mesh,
                 coordinates,
                 std::vector<glm::vec4>(mesh.indices.size()),
                 std::vector<bool>(mesh.indices.size() / 3)};
    for (std::size_t face = 0; face < work.validFaces.size(); ++face) {
        const auto a = uv(mesh.vertices[mesh.indices[face * 3]], coordinates);
        const auto b = uv(mesh.vertices[mesh.indices[face * 3 + 1]], coordinates) - a;
        const auto c = uv(mesh.vertices[mesh.indices[face * 3 + 2]], coordinates) - a;
        work.validFaces[face] = std::abs(b.x * c.y - b.y * c.x) > 1.0e-12F;
    }

    SMikkTSpaceInterface interface{};
    interface.m_getNumFaces = [](const auto* context) {
        return static_cast<int>(data(context).mesh.indices.size() / 3);
    };

    interface.m_getNumVerticesOfFace = [](const auto*, int) {
        return 3;
    };

    interface.m_getPosition = [](const auto* context, float output[], int face, int corner) {
        const auto& value = vertex(context, face, corner).position;

        for (int i = 0; i < 3; ++i) {
            output[i] = value[i];
        }
    };

    interface.m_getNormal = [](const auto* context, float output[], int face, int corner) {
        const auto& value = vertex(context, face, corner).normal;

        for (int i = 0; i < 3; ++i) {
            output[i] = value[i];
        }
    };

    interface.m_getTexCoord = [](const auto* context, float output[], int face, int corner) {
        const auto value = uv(vertex(context, face, corner), data(context).coordinates);
        output[0] = value.x;
        output[1] = value.y;
    };

    interface.m_setTSpaceBasic = [](const auto* context, const float tangent[], float sign, int face, int corner) {
        auto& work = data(context);
        const glm::vec3 value{tangent[0], tangent[1], tangent[2]};

        if (work.validFaces[face] && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
            glm::dot(value, value) > 1.0e-12F) {
            work.corners[static_cast<std::size_t>(face) * 3 + corner] = glm::vec4{glm::normalize(value), sign};
        }
    };

    const SMikkTSpaceContext context{&interface, &work};

    if (!genTangSpaceDefault(&context)) {
        throw std::runtime_error("MikkTSpace tangent generation failed: " + mesh.name);
    }

    const auto originalCount = mesh.vertices.size();
    std::vector<std::vector<std::uint32_t>> alternatives(originalCount);

    for (std::size_t corner = 0; corner < mesh.indices.size(); ++corner) {
        const auto source = mesh.indices[corner];
        const auto tangent = work.corners[corner];
        auto& matches = alternatives[source];
        std::optional<std::uint32_t> match;

        for (const auto candidate : matches) {
            if (glm::all(
                    glm::lessThanEqual(glm::abs(mesh.vertices[candidate].tangent - tangent), glm::vec4{1.0e-5F}))) {
                match = candidate;
                break;
            }
        }

        if (!match) {
            if (matches.empty()) {
                match = source;
                mesh.vertices[source].tangent = tangent;
            } else {
                if (mesh.vertices.size() >= std::numeric_limits<std::uint32_t>::max()) {
                    throw std::runtime_error("Tangent seams exceed vertex limits.");
                }

                auto copy = mesh.vertices[source];
                copy.tangent = tangent;
                match = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(copy);
            }

            matches.push_back(*match);
        }

        mesh.indices[corner] = *match;
    }
}

} // namespace grimlock::assets
