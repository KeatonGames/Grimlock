#include "assets/model_loader.hpp"

#include "core/startup_profile.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <tiny_obj_loader.h>
#include <unordered_map>
#include <utility>

namespace grimlock::assets {

namespace {

struct VertexHash {
    std::size_t operator()(const Vertex& vertex) const noexcept {
        std::size_t hash = 0;
        const std::array values{vertex.position.x,
                                vertex.position.y,
                                vertex.position.z,
                                vertex.normal.x,
                                vertex.normal.y,
                                vertex.normal.z,
                                vertex.uv.x,
                                vertex.uv.y,
                                vertex.uv1.x,
                                vertex.uv1.y,
                                vertex.tangent.x,
                                vertex.tangent.y,
                                vertex.tangent.z,
                                vertex.tangent.w};
        for (const float value : values) {
            hash ^= std::hash<float>{}(value) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        }

        return hash;
    }
};

bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

ModelData loadObj(const std::filesystem::path& path) {
    diagnostics::StartupSpan profile{"asset.OBJ.read_parse_convert"};
    std::ifstream file(path);

    if (!file) {
        throw std::runtime_error("Cannot open OBJ model: " + path.string());
    }

    tinyobj::attrib_t attributes;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    ModelData model;
    model.materials.front().doubleSided = true; // Retain the existing two-sided OBJ preview.
    model.materials.front().metallic = 0.0F;
    model.materials.front().roughness = 0.7F;
    std::string error;

    // Stream loading handles native filesystem paths. MTL/textures are intentionally not imported.
    if (!tinyobj::LoadObj(&attributes, &shapes, &materials, &model.warnings, &error, &file, nullptr, true, false)) {
        throw std::runtime_error("Cannot parse OBJ model '" + path.string() + "': " + error);
    }

    const auto invalid = [&path](const std::string& reason) {
        throw std::runtime_error("Invalid OBJ model '" + path.string() + "': " + reason);
    };

    const auto read3 = [&invalid](const std::vector<tinyobj::real_t>& data, const int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= data.size() / 3) {
            invalid("position or normal index is out of range");
        }

        const auto offset = static_cast<std::size_t>(index) * 3;
        const glm::vec3 result{data[offset], data[offset + 1], data[offset + 2]};

        if (!finite(result)) {
            invalid("non-finite position or normal");
        }

        return result;
    };

    std::size_t degenerateFaces = 0;

    for (const auto& shape : shapes) {
        MeshData mesh;
        mesh.name = shape.name.empty() ? "mesh" : shape.name;
        std::unordered_map<Vertex, std::uint32_t, VertexHash> vertices;
        std::size_t offset = 0;

        for (const auto faceSize : shape.mesh.num_face_vertices) {
            if (faceSize != 3 || offset + 3 > shape.mesh.indices.size()) {
                invalid("triangulation produced an invalid face");
            }

            std::array<Vertex, 3> face{};

            for (std::size_t corner = 0; corner < 3; ++corner) {
                face[corner].position = read3(attributes.vertices, shape.mesh.indices[offset + corner].vertex_index);
            }

            const glm::vec3 cross =
                glm::cross(face[1].position - face[0].position, face[2].position - face[0].position);
            const float lengthSquared = glm::dot(cross, cross);

            if (!std::isfinite(lengthSquared)) {
                invalid("face coordinates are too large");
            }

            if (lengthSquared <= 1.0e-20F) {
                ++degenerateFaces;
                offset += 3;
                continue;
            }

            const glm::vec3 faceNormal = cross / std::sqrt(lengthSquared);

            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto& source = shape.mesh.indices[offset + corner];
                auto& vertex = face[corner];
                vertex.normal = faceNormal;

                if (source.normal_index != -1) {
                    const glm::vec3 normal = read3(attributes.normals, source.normal_index);
                    const float normalLength = glm::dot(normal, normal);

                    if (!std::isfinite(normalLength)) {
                        invalid("normal is too large");
                    }

                    if (normalLength > 1.0e-20F) {
                        vertex.normal = normal / std::sqrt(normalLength);
                    }
                }

                if (source.texcoord_index != -1) {
                    if (source.texcoord_index < 0 ||
                        static_cast<std::size_t>(source.texcoord_index) >= attributes.texcoords.size() / 2) {
                        invalid("texture-coordinate index is out of range");
                    }

                    const auto uvOffset = static_cast<std::size_t>(source.texcoord_index) * 2;
                    vertex.uv = {attributes.texcoords[uvOffset], attributes.texcoords[uvOffset + 1]};

                    if (!std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y)) {
                        invalid("non-finite texture coordinate");
                    }
                }

                if (mesh.vertices.size() >= std::numeric_limits<std::uint32_t>::max() ||
                    mesh.indices.size() >= std::numeric_limits<std::uint32_t>::max()) {
                    invalid("mesh exceeds 32-bit indexed drawing limits");
                }

                const auto [entry, inserted] =
                    vertices.try_emplace(vertex, static_cast<std::uint32_t>(mesh.vertices.size()));
                if (inserted) {
                    mesh.vertices.push_back(vertex);
                }

                mesh.indices.push_back(entry->second);
            }

            offset += 3;
        }

        if (!mesh.indices.empty()) {
            model.meshes.push_back(std::move(mesh));
        }
    }

    if (model.meshes.empty()) {
        invalid("no non-degenerate triangle geometry was found");
    }

    if (degenerateFaces != 0) {
        model.warnings += "Skipped " + std::to_string(degenerateFaces) + " degenerate triangle(s).\n";
    }

    for (std::size_t mesh = 0; mesh < model.meshes.size(); ++mesh) {
        model.roots.push_back(model.nodes.size());
        model.nodes.push_back({model.meshes[mesh].name, std::nullopt, {}, {mesh}, glm::mat4{1.0F}});
    }

    updateModelBounds(model);

    return model;
}

ModelData loadModel(const std::filesystem::path& path) {
    const auto label = "asset.load:" + path.filename().string();
    diagnostics::StartupSpan profile{label};
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });

    if (extension == ".obj") {
        return loadObj(path);
    }

    if (extension == ".gltf" || extension == ".glb") {
        return loadGltf(path);
    }

    throw std::invalid_argument("Unsupported model format '" + path.string() + "': expected OBJ, GLB, or glTF.");
}

} // namespace grimlock::assets
