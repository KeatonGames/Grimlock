#include "assets/gltf_animation.hpp"
#include "assets/gltf_materials.hpp"
#include "assets/model_loader.hpp"
#include "assets/tangents.hpp"
#include "core/startup_profile.hpp"
#include "grimlock/assets/animation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <simdjson.h>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace grimlock::assets {

namespace {

[[noreturn]] void invalid(const std::string& reason) {
    throw std::runtime_error(reason);
}

// Error-only inspection reuses fastgltf's existing JSON dependency. It never decodes geometry or
// bypasses parser validation.
std::string extensionDiagnostics(const std::filesystem::path& path) {
    try {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        const auto fileSize = static_cast<std::streamoff>(file.tellg());

        if (!file || fileSize < 4) {
            return {};
        }

        file.seekg(0);
        std::array<char, 20> header{};
        file.read(header.data(), 4);
        std::size_t size = static_cast<std::size_t>(fileSize);

        if (std::string_view{header.data(), 4} == "glTF") {
            file.read(header.data() + 4, 16);
            const auto word = [&](int start) {
                std::uint32_t value = 0;

                for (int b = 0; b < 4; ++b) {
                    value |= static_cast<std::uint32_t>(static_cast<unsigned char>(header[start + b])) << (8 * b);
                }

                return value;
            };

            if (!file || word(16) != 0x4e4f534a || word(12) > size - 20) {
                return {};
            }

            size = word(12);
        } else {
            file.clear();
            file.seekg(0);
        }

        if (size > 64 * 1024 * 1024) {
            return " (diagnostic JSON exceeds 64 MiB)";
        }

        std::string json(size, '\0');

        if (!file.read(json.data(), static_cast<std::streamsize>(size))) {
            return {};
        }

        simdjson::dom::parser parser;
        simdjson::dom::element root;

        if (parser.parse(json).get(root)) {
            return {};
        }

        std::string result;

        for (const auto field : {"extensionsRequired", "extensionsUsed"}) {
            simdjson::dom::array names;

            if (root[field].get_array().get(names)) {
                continue;
            }

            result += "; ";
            result += field;
            result += '=';

            for (auto name : names) {
                std::string_view value;

                if (!name.get_string().get(value)) {
                    result += value;
                    result += ' ';
                }
            }
        }

        return result;
    } catch (...) {
        // Preserve the original parser failure if even diagnostics cannot be read.
        return {};
    }
}

bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool unsignedIndex(const fastgltf::ComponentType type) {
    return type == fastgltf::ComponentType::UnsignedByte || type == fastgltf::ComponentType::UnsignedShort ||
           type == fastgltf::ComponentType::UnsignedInt;
}

// Accessor tools assume validated, resident buffers. Check actual byte ranges and
// sparse references before invoking either them or fastgltf's structural validator.
void validateStorage(const fastgltf::Asset& asset) {
    std::size_t bufferIndex = 0;

    for (const auto& buffer : asset.buffers) {
        const auto size = std::visit(
            [](const auto& source) -> std::size_t {
                using T = std::decay_t<decltype(source)>;

                if constexpr (std::is_same_v<T, fastgltf::sources::Array> ||
                              std::is_same_v<T, fastgltf::sources::Vector> ||
                              std::is_same_v<T, fastgltf::sources::ByteView>) {
                    return source.bytes.size();
                } else {
                    invalid("Geometry buffer is not resident in CPU memory.");
                }
            },
            buffer.data);
        if (buffer.byteLength == 0 || buffer.byteLength > size) {
            invalid("Truncated geometry buffer " + std::to_string(bufferIndex));
        }

        ++bufferIndex;
    }

    for (const auto& view : asset.bufferViews) {
        if (view.bufferIndex >= asset.buffers.size()) {
            invalid("Buffer view references an invalid buffer.");
        }

        const auto size = asset.buffers[view.bufferIndex].byteLength;

        if (view.byteOffset > size || view.byteLength > size - view.byteOffset) {
            invalid("Buffer view exceeds its buffer.");
        }
    }

    std::size_t currentAccessor = 0, nextAccessor = 0;
    const auto range =
        [&](std::size_t viewIndex, std::size_t offset, std::size_t count, std::size_t stride, std::size_t elementSize) {
            if (viewIndex >= asset.bufferViews.size()) {
                invalid("Accessor " + std::to_string(currentAccessor) + " references invalid buffer view " +
                        std::to_string(viewIndex));
            }

            const auto size = asset.bufferViews[viewIndex].byteLength;

            if (count == 0 || stride < elementSize || elementSize == 0 || offset > size ||
                elementSize > size - offset || count - 1 > (size - offset - elementSize) / stride) {
                invalid("Accessor " + std::to_string(currentAccessor) + " byte range/stride exceeds buffer view " +
                        std::to_string(viewIndex));
            }
        };
    for (const auto& accessor : asset.accessors) {
        currentAccessor = nextAccessor++;

        if (accessor.type == fastgltf::AccessorType::Invalid ||
            accessor.componentType == fastgltf::ComponentType::Invalid || accessor.count == 0 ||
            accessor.count > std::numeric_limits<std::uint32_t>::max()) {
            invalid("Invalid accessor type/count or geometry exceeds 32-bit drawing limits.");
        }

        const auto elementSize = fastgltf::getElementByteSize(accessor.type, accessor.componentType);

        if (accessor.bufferViewIndex) {
            const auto view = *accessor.bufferViewIndex;

            if (view >= asset.bufferViews.size()) {
                invalid("Accessor buffer view is out of range.");
            }

            range(view,
                  accessor.byteOffset,
                  accessor.count,
                  asset.bufferViews[view].byteStride.value_or(elementSize),
                  elementSize);
        } else if (accessor.byteOffset != 0) {
            invalid("Accessor without a buffer view has a byte offset.");
        }

        if (accessor.sparse) {
            const auto& sparse = *accessor.sparse;

            if (!unsignedIndex(sparse.indexComponentType) || sparse.count > accessor.count) {
                invalid("Invalid sparse accessor indices/count.");
            }

            const auto indexSize = fastgltf::getComponentByteSize(sparse.indexComponentType);
            range(sparse.indicesBufferView, sparse.indicesByteOffset, sparse.count, indexSize, indexSize);
            range(sparse.valuesBufferView, sparse.valuesByteOffset, sparse.count, elementSize, elementSize);

            // Sparse indices must be strictly increasing and inside the parent accessor.
            fastgltf::Accessor indices{};
            indices.type = fastgltf::AccessorType::Scalar;
            indices.componentType = sparse.indexComponentType;
            indices.count = sparse.count;
            indices.byteOffset = sparse.indicesByteOffset;
            indices.bufferViewIndex = sparse.indicesBufferView;
            const auto& view = asset.bufferViews[sparse.indicesBufferView];
            const auto& valuesView = asset.bufferViews[sparse.valuesBufferView];

            if (view.byteStride || valuesView.byteStride || view.target || valuesView.target) {
                invalid("Sparse buffer views must be tightly packed without a target.");
            }

            std::optional<std::uint32_t> previous;
            fastgltf::iterateAccessor<std::uint32_t>(asset, indices, [&](const auto index) {
                if (index >= accessor.count || (previous && index <= *previous)) {
                    invalid("Invalid sparse index order/range.");
                }

                previous = index;
            });
        }
    }
}

void validatePrimitiveAttributes(const fastgltf::Primitive& source, const std::string& name, std::string& warnings) {
    for (const auto& attribute : source.attributes) {
        const std::string key{attribute.name};

        if ((key.starts_with("JOINTS_") || key.starts_with("WEIGHTS_")) && key != "JOINTS_0" && key != "JOINTS_1" &&
            key != "WEIGHTS_0" && key != "WEIGHTS_1") {
            invalid("More than eight skin influences are unsupported: " + name + "/" + key);
        }

        if (key != "POSITION" && key != "NORMAL" && key != "TANGENT" && key != "TEXCOORD_0" && key != "TEXCOORD_1" &&
            key != "JOINTS_0" && key != "JOINTS_1" && key != "WEIGHTS_0" && key != "WEIGHTS_1") {
            warnings += "Vertex attribute '" + key + "' in primitive '" + name +
                        "' is preserved only in the source file and is not rendered.\n";
        }
    }

    if (source.type != fastgltf::PrimitiveType::Triangles) {
        invalid("Only TRIANGLES primitives are supported: " + name);
    }
}

bool readVertexGeometry(const fastgltf::Asset& asset, const fastgltf::Primitive& source, MeshData& mesh) {
    const auto& name = mesh.name;

    const auto position = source.findAttribute("POSITION");

    if (position == source.attributes.end()) {
        invalid("Primitive is missing POSITION: " + name);
    }

    const auto& positions = asset.accessors.at(position->accessorIndex);

    if (positions.type != fastgltf::AccessorType::Vec3) {
        invalid("POSITION must be VEC3: " + name);
    }

    mesh.vertices.resize(positions.count);
    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, positions, [&](const auto value, const std::size_t index) {
        if (!finite(value)) {
            invalid("Non-finite position: " + name);
        }

        mesh.vertices[index].position = value;
        mesh.vertices[index].morphIndex = static_cast<std::uint32_t>(index);
    });
    const auto normal = source.findAttribute("NORMAL");

    if (normal == source.attributes.end()) {
        return true;
    }

    bool generateNormals = false;
    const auto& normals = asset.accessors.at(normal->accessorIndex);

    if (normals.type != fastgltf::AccessorType::Vec3 || normals.count != positions.count) {
        invalid("NORMAL type/count does not match POSITION: " + name);
    }

    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, normals, [&](const auto value, const std::size_t index) {
        const auto length = glm::dot(value, value);

        if (!finite(value) || !std::isfinite(length)) {
            invalid("Non-finite normal: " + name);
        }

        if (length <= 1.0e-20F) {
            generateNormals = true;
        } else {
            mesh.vertices[index].normal = value / std::sqrt(length);
        }
    });

    return generateNormals;
}

bool readTextureCoordinates(const fastgltf::Asset& asset,
                            const fastgltf::Primitive& source,
                            const MaterialData& material,
                            MeshData& mesh,
                            std::string& warnings) {
    const auto& name = mesh.name;

    bool normalCoordinates = true;

    for (std::uint32_t set = 0; set < 2; ++set) {
        const auto uv = source.findAttribute(set == 0 ? "TEXCOORD_0" : "TEXCOORD_1");

        if (uv == source.attributes.end()) {
            for (const auto* slot : {&material.baseColorTexture,
                                     &material.metallicRoughnessTexture,
                                     &material.occlusionTexture,
                                     &material.emissiveTexture,
                                     &material.specularTexture,
                                     &material.specularColorTexture,
                                     &material.clearcoatTexture,
                                     &material.clearcoatRoughnessTexture,
                                     &material.clearcoatNormalTexture}) {
                if (*slot && (*slot)->uvSet == set) {
                    invalid("Material requires a missing UV set in " + name);
                }
            }

            if (material.normalTexture && material.normalTexture->uvSet == set) {
                normalCoordinates = false;
                warnings += "Missing normal-map UV coordinates; using geometric normals in " + name + ".\n";
            }

            continue;
        }

        const auto& uvs = asset.accessors.at(uv->accessorIndex);

        if (uvs.type != fastgltf::AccessorType::Vec2 || uvs.count != mesh.vertices.size() ||
            (uvs.componentType != fastgltf::ComponentType::Float && !uvs.normalized)) {
            invalid("Invalid TEXCOORD_0 type/count/normalization: " + name);
        }

        fastgltf::iterateAccessorWithIndex<glm::vec2>(asset, uvs, [&](const auto value, const std::size_t index) {
            if (!std::isfinite(value.x) || !std::isfinite(value.y)) {
                invalid("Non-finite UV: " + name);
            }

            (set == 0 ? mesh.vertices[index].uv : mesh.vertices[index].uv1) = value;
        });
    }

    return normalCoordinates;
}

bool readVertexTangents(const fastgltf::Asset& asset, const fastgltf::Primitive& source, MeshData& mesh) {
    const auto& name = mesh.name;

    const auto tangent = source.findAttribute("TANGENT");

    if (tangent == source.attributes.end()) {
        return false;
    }

    const auto& tangents = asset.accessors.at(tangent->accessorIndex);

    if (tangents.type != fastgltf::AccessorType::Vec4 || tangents.componentType != fastgltf::ComponentType::Float ||
        tangents.count != mesh.vertices.size()) {
        invalid("Invalid TANGENT type/count: " + name);
    }

    fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, tangents, [&](const auto value, const std::size_t index) {
        const auto length = glm::dot(glm::vec3{value}, glm::vec3{value});

        if (!finite(glm::vec3{value}) || !std::isfinite(length) || length <= 1.0e-20F ||
            (value.w != -1.0F && value.w != 1.0F)) {
            invalid("Invalid tangent vector/handedness: " + name);
        }

        mesh.vertices[index].tangent = glm::vec4{glm::vec3{value} / std::sqrt(length), value.w};
    });

    return true;
}

void readSkinWeights(const fastgltf::Asset& asset, const fastgltf::Primitive& source, MeshData& mesh) {
    const auto& name = mesh.name;

    for (std::uint32_t set = 0; set < 2; ++set) {
        const auto joints = source.findAttribute(set == 0 ? "JOINTS_0" : "JOINTS_1");
        const auto weights = source.findAttribute(set == 0 ? "WEIGHTS_0" : "WEIGHTS_1");

        if ((joints == source.attributes.end()) != (weights == source.attributes.end())) {
            invalid("Unpaired joints/weights: " + name);
        }

        if (joints == source.attributes.end()) {
            continue;
        }

        const auto& jointAccessor = asset.accessors.at(joints->accessorIndex);
        const auto& weightAccessor = asset.accessors.at(weights->accessorIndex);

        if (jointAccessor.type != fastgltf::AccessorType::Vec4 || weightAccessor.type != fastgltf::AccessorType::Vec4 ||
            jointAccessor.count != mesh.vertices.size() || weightAccessor.count != mesh.vertices.size() ||
            jointAccessor.normalized ||
            (jointAccessor.componentType != fastgltf::ComponentType::UnsignedByte &&
             jointAccessor.componentType != fastgltf::ComponentType::UnsignedShort) ||
            (weightAccessor.componentType != fastgltf::ComponentType::Float &&
             !((weightAccessor.componentType == fastgltf::ComponentType::UnsignedByte ||
                weightAccessor.componentType == fastgltf::ComponentType::UnsignedShort) &&
               weightAccessor.normalized))) {
            invalid("Invalid joint/weight accessor: " + name);
        }

        fastgltf::iterateAccessorWithIndex<glm::uvec4>(asset, jointAccessor, [&](auto value, std::size_t i) {
            (set == 0 ? mesh.vertices[i].joints0 : mesh.vertices[i].joints1) = value;
        });
        fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, weightAccessor, [&](auto value, std::size_t i) {
            for (int c = 0; c < 4; ++c) {
                if (!std::isfinite(value[c]) || value[c] < 0) {
                    invalid("Invalid skin weight: " + name);
                }
            }

            (set == 0 ? mesh.vertices[i].weights0 : mesh.vertices[i].weights1) = value;
        });
    }

    if (source.findAttribute("JOINTS_1") != source.attributes.end() &&
        source.findAttribute("JOINTS_0") == source.attributes.end()) {
        invalid("JOINTS_1 requires JOINTS_0: " + name);
    }

    for (auto& vertex : mesh.vertices) {
        const float sum = glm::dot(vertex.weights0 + vertex.weights1, glm::vec4{1});

        if (!std::isfinite(sum)) {
            invalid("Non-finite skin weight sum: " + name);
        }

        if (sum > 0) {
            vertex.weights0 /= sum;
            vertex.weights1 /= sum;
        }
    }
}

void readMorphTargets(const fastgltf::Asset& asset, const fastgltf::Primitive& source, MeshData& mesh) {
    const auto& name = mesh.name;

    const auto vertexCount = mesh.vertices.size();
    mesh.morphVertexCount = static_cast<std::uint32_t>(vertexCount);
    mesh.morphTargetCount = static_cast<std::uint32_t>(source.targets.size());
    mesh.morphDeltas.resize(vertexCount * source.targets.size());

    for (std::size_t target = 0; target < source.targets.size(); ++target) {
        for (const auto& attribute : source.targets[target]) {
            if (attribute.name != "POSITION" && attribute.name != "NORMAL" && attribute.name != "TANGENT") {
                invalid("Unsupported morph attribute " + std::string{attribute.name} + ": " + name);
            }

            const auto& accessor = asset.accessors.at(attribute.accessorIndex);

            if (accessor.type != fastgltf::AccessorType::Vec3 ||
                accessor.componentType != fastgltf::ComponentType::Float || accessor.count != vertexCount) {
                invalid("Invalid morph accessor: " + name);
            }

            fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, accessor, [&](auto value, std::size_t i) {
                if (!finite(value)) {
                    invalid("Non-finite morph delta: " + name);
                }

                auto& delta = mesh.morphDeltas[target * vertexCount + i];

                if (attribute.name == "POSITION") {
                    delta.position = glm::vec4{value, 0};
                } else if (attribute.name == "NORMAL") {
                    delta.normal = glm::vec4{value, 0};
                } else {
                    delta.tangent = glm::vec4{value, 0};
                }
            });
        }
    }
}

void readTriangleIndices(const fastgltf::Asset& asset, const fastgltf::Primitive& source, MeshData& mesh) {
    const auto& name = mesh.name;

    if (source.indicesAccessor) {
        const auto& indices = asset.accessors.at(*source.indicesAccessor);

        if (indices.type != fastgltf::AccessorType::Scalar || !unsignedIndex(indices.componentType) ||
            indices.normalized) {
            invalid("Indices must be unnormalized unsigned scalars: " + name);
        }

        mesh.indices.reserve(indices.count);
        fastgltf::iterateAccessor<std::uint32_t>(asset, indices, [&](const auto value) {
            if (value >= mesh.vertices.size()) {
                invalid("Index exceeds vertex count: " + name);
            }

            mesh.indices.push_back(value);
        });
    } else {
        mesh.indices.resize(mesh.vertices.size());
        std::iota(mesh.indices.begin(), mesh.indices.end(), 0U);
    }

    if (mesh.indices.empty() || mesh.indices.size() % 3 != 0) {
        invalid("Incomplete triangle list: " + name);
    }
}

void prepareTriangleGeometry(MeshData& mesh, const bool generateNormals, std::string& warnings) {
    const auto& name = mesh.name;

    std::vector<Vertex> flatVertices;
    std::vector<std::uint32_t> validIndices;
    validIndices.reserve(mesh.indices.size());

    if (generateNormals) {
        flatVertices.reserve(mesh.indices.size());
    }

    std::size_t degenerate = 0;

    for (std::size_t face = 0; face < mesh.indices.size(); face += 3) {
        const auto a = mesh.indices[face], b = mesh.indices[face + 1], c = mesh.indices[face + 2];
        const auto cross = glm::cross(mesh.vertices[b].position - mesh.vertices[a].position,
                                      mesh.vertices[c].position - mesh.vertices[a].position);
        const auto length = glm::dot(cross, cross);

        if (!std::isfinite(length)) {
            invalid("Triangle coordinates are too large: " + name);
        }

        if (length <= 1.0e-20F) {
            ++degenerate;
            continue;
        }

        for (const auto index : {a, b, c}) {
            if (generateNormals) {
                auto vertex = mesh.vertices[index];

                if (glm::dot(vertex.normal, vertex.normal) <= 1.0e-20F) {
                    vertex.normal = cross / std::sqrt(length);
                }

                validIndices.push_back(static_cast<std::uint32_t>(flatVertices.size()));
                flatVertices.push_back(vertex);
            } else {
                validIndices.push_back(index);
            }
        }
    }

    if (generateNormals) {
        mesh.vertices = std::move(flatVertices);
    }

    mesh.indices = std::move(validIndices);

    if (mesh.indices.empty()) {
        invalid("Primitive has no non-degenerate triangles: " + name);
    }

    if (degenerate != 0) {
        warnings += "Skipped " + std::to_string(degenerate) + " degenerate triangle(s) in " + name + ".\n";
    }
}

void prepareNormalMapping(MeshData& mesh,
                          const MaterialData& material,
                          const bool generateNormals,
                          const bool normalCoordinates,
                          const bool importedTangents,
                          std::string& warnings) {
    if (!normalCoordinates) {
        for (auto& vertex : mesh.vertices) {
            vertex.tangent = glm::vec4{0.0F};
        }

        return;
    }

    if (!material.normalTexture) {
        return;
    }

    const auto& reference = *material.normalTexture;

    if (!importedTangents || generateNormals || reference.uvSet != 0 || reference.rotation != 0.0F ||
        reference.scale != glm::vec2{1.0F}) {
        generateTangents(mesh, reference);
    }

    if (std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [](const auto& vertex) {
            return vertex.tangent.w == 0.0F;
        })) {
        warnings += "Degenerate normal-map tangent frames use geometric normals in " + mesh.name + ".\n";
    }
}

MeshData readPrimitive(const fastgltf::Asset& asset,
                       const fastgltf::Primitive& source,
                       const std::string& name,
                       const ModelData& model,
                       std::string& warnings) {
    validatePrimitiveAttributes(source, name, warnings);

    MeshData mesh;
    mesh.name = name;
    mesh.material = source.materialIndex.value_or(model.materials.size() - 1);
    const bool generateNormals = readVertexGeometry(asset, source, mesh);

    const auto& material = model.materials.at(mesh.material);
    const bool normalCoordinates = readTextureCoordinates(asset, source, material, mesh, warnings);
    const bool importedTangents = readVertexTangents(asset, source, mesh);
    readSkinWeights(asset, source, mesh);
    readMorphTargets(asset, source, mesh);
    readTriangleIndices(asset, source, mesh);

    prepareTriangleGeometry(mesh, generateNormals, warnings);
    prepareNormalMapping(mesh, material, generateNormals, normalCoordinates, importedTangents, warnings);

    return mesh;
}

fastgltf::Expected<fastgltf::Asset> parseGltfAsset(fastgltf::Parser& parser,
                                                   fastgltf::GltfDataBuffer& data,
                                                   const std::filesystem::path& directory,
                                                   const std::filesystem::path& path) {
    auto parsed = [&] {
        diagnostics::StartupSpan profile{"asset.glTF.parse_external_buffers"};

        return parser.loadGltf(data, directory, fastgltf::Options::LoadExternalBuffers);
    }();

    if (parsed.error() != fastgltf::Error::None) {
        invalid("fastgltf parser error " + std::to_string(static_cast<int>(parsed.error())) + ": " +
                std::string{fastgltf::getErrorMessage(parsed.error())} + extensionDiagnostics(path) +
                " (required extensions must have a supported decoder; KTX2/Basis needs "
                "libktx/Basis Universal, Draco needs Draco, meshopt needs meshoptimizer; none "
                "are required by the supplied assets)");
    }

    return parsed;
}

void validateGltfAsset(const fastgltf::Asset& asset) {
    for (const auto& extension : asset.extensionsUsed) {
        const std::string name{extension};

        if (name != "KHR_texture_transform" && name != "KHR_materials_unlit" &&
            name != "KHR_materials_emissive_strength" && name != "KHR_materials_clearcoat" &&
            name != "KHR_materials_specular" && name != "KHR_materials_ior" && name != "KHR_materials_transmission" &&
            name != "KHR_materials_volume" && name != "KHR_materials_sheen" && name != "KHR_lights_punctual") {
            invalid("Unsupported glTF extension: " + name + "; no compatible decoder enabled");
        }
    }

    for (const auto& extension : asset.extensionsRequired) {
        if (extension == "KHR_materials_transmission" || extension == "KHR_materials_volume" ||
            extension == "KHR_materials_sheen") {
            invalid("Required extension " + std::string{extension} +
                    " needs an unimplemented advanced material rendering path");
        }
    }

    validateStorage(asset);

    // Guard cross-references which the upstream validator assumes are valid.
    for (const auto& animation : asset.animations) {
        for (const auto& channel : animation.channels) {
            if (channel.samplerIndex >= animation.samplers.size() ||
                (channel.nodeIndex && *channel.nodeIndex >= asset.nodes.size())) {
                invalid("Invalid animation reference.");
            }
        }

        for (const auto& sampler : animation.samplers) {
            if (sampler.inputAccessor >= asset.accessors.size() || sampler.outputAccessor >= asset.accessors.size()) {
                invalid("Invalid animation accessor reference.");
            }
        }
    }

    if (const auto error = fastgltf::validate(asset); error != fastgltf::Error::None) {
        invalid("glTF structural validation failed: " + std::string{fastgltf::getErrorMessage(error)});
    }
}

void readModelNodes(const fastgltf::Asset& asset, ModelData& model) {
    model.nodes.resize(asset.nodes.size());

    for (std::size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex) {
        const auto& source = asset.nodes[nodeIndex];

        if (const auto* trs = std::get_if<fastgltf::TRS>(&source.transform)) {
            const auto& rotation = trs->rotation;
            const float squaredLength = rotation.x() * rotation.x() + rotation.y() * rotation.y() +
                                        rotation.z() * rotation.z() + rotation.w() * rotation.w();

            if (!std::isfinite(squaredLength) || std::abs(squaredLength - 1.0F) > 1.0e-3F) {
                invalid("Node quaternion must have unit length.");
            }
        }

        auto& node = model.nodes[nodeIndex];
        node.name = source.name;
        node.weights.assign(source.weights.begin(), source.weights.end());

        if (source.meshIndex && node.weights.empty()) {
            const auto& weights = asset.meshes.at(*source.meshIndex).weights;
            node.weights.assign(weights.begin(), weights.end());

            if (node.weights.empty() && !asset.meshes.at(*source.meshIndex).primitives.empty()) {
                node.weights.resize(asset.meshes.at(*source.meshIndex).primitives.front().targets.size(), 0);
            }
        }

        for (float weight : node.weights) {
            if (!std::isfinite(weight)) {
                invalid("Non-finite morph weight");
            }
        }

        if (const auto* trs = std::get_if<fastgltf::TRS>(&source.transform)) {
            node.matrixTransform = false;
            node.translation = {trs->translation[0], trs->translation[1], trs->translation[2]};
            node.scale = {trs->scale[0], trs->scale[1], trs->scale[2]};
            node.rotation = {trs->rotation.w(), trs->rotation.x(), trs->rotation.y(), trs->rotation.z()};
        }

        const auto matrix = fastgltf::getLocalTransformMatrix(source);

        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                node.localTransform[column][row] = matrix[column][row];
            }
        }

        node.children.assign(source.children.begin(), source.children.end());

        for (const auto child : node.children) {
            if (child >= model.nodes.size() || model.nodes[child].parent) {
                invalid("Invalid child or multiple node parents.");
            }

            model.nodes[child].parent = nodeIndex;
        }
    }
}

void selectModelRoots(const fastgltf::Asset& asset, ModelData& model) {
    if (!asset.scenes.empty()) {
        const auto scene = asset.defaultScene.value_or(0);

        if (scene >= asset.scenes.size()) {
            invalid("Default scene is out of range.");
        }

        const auto& roots = asset.scenes[scene].nodeIndices;
        model.roots.assign(roots.begin(), roots.end());

        if (!asset.defaultScene) {
            model.warnings += "No default scene; selected the first scene.\n";
        }

        return;
    }

    if (asset.defaultScene) {
        invalid("Default scene is set but no scenes are defined.");
    }

    for (std::size_t node = 0; node < model.nodes.size(); ++node) {
        if (!model.nodes[node].parent) {
            model.roots.push_back(node);
        }
    }

    model.warnings += "No scenes; selected all root nodes.\n";
}

std::vector<std::vector<std::size_t>> readModelMeshes(const fastgltf::Asset& asset, ModelData& model) {
    std::vector<std::vector<std::size_t>> meshPrimitives(asset.meshes.size());

    for (std::size_t meshIndex = 0; meshIndex < asset.meshes.size(); ++meshIndex) {
        const auto& mesh = asset.meshes[meshIndex];

        if (mesh.primitives.empty()) {
            invalid("Mesh has no primitives");
        }

        for (std::size_t primitive = 0; primitive < mesh.primitives.size(); ++primitive) {
            const auto name = std::string{mesh.name} + "/" + std::to_string(primitive);
            meshPrimitives[meshIndex].push_back(model.meshes.size());
            model.meshes.push_back(readPrimitive(asset, mesh.primitives[primitive], name, model, model.warnings));
        }
    }

    return meshPrimitives;
}

void validateSkinnedPrimitive(const fastgltf::Primitive& sourcePrimitive,
                              const MeshData& mesh,
                              const SkinData& skin,
                              const std::string& nodeName) {
    if (sourcePrimitive.findAttribute("JOINTS_0") == sourcePrimitive.attributes.end()) {
        invalid("Skinned primitive lacks JOINTS_0/WEIGHTS_0: " + mesh.name);
    }

    for (const auto& vertex : mesh.vertices) {
        if (glm::dot(vertex.weights0 + vertex.weights1, glm::vec4{1}) < 1e-6F) {
            invalid("Skinned vertex has zero weights: " + mesh.name);
        }

        for (int lane = 0; lane < 4; ++lane) {
            // All joint indices must be valid, even for zero-weight entries, because
            // shaders fetch all lanes.
            if (vertex.joints0[lane] >= skin.joints.size() || vertex.joints1[lane] >= skin.joints.size()) {
                invalid("Joint index outside skin on node " + nodeName);
            }
        }
    }
}

bool isBindPosePreviewCandidate(const ModelData& model, const SkinData& skin) {
    bool identityTransforms = skin.joints.size() > 1;
    bool nonIdentityBinds = false;

    for (std::size_t jointIndex = 0; jointIndex < skin.joints.size(); ++jointIndex) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                const float diagonal = column == row ? 1.F : 0.F;

                if (std::abs(model.nodes[skin.joints[jointIndex]].localTransform[column][row] - diagonal) > 1e-6F) {
                    identityTransforms = false;
                }

                if (std::abs(skin.inverseBindMatrices[jointIndex][column][row] - diagonal) > 1e-5F) {
                    nonIdentityBinds = true;
                }
            }
        }
    }

    return identityTransforms && nonIdentityBinds;
}

void bindModelNodes(const fastgltf::Asset& asset,
                    ModelData& model,
                    const std::vector<std::vector<std::size_t>>& meshPrimitives) {
    for (std::size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex) {
        const auto& source = asset.nodes[nodeIndex];
        auto& node = model.nodes[nodeIndex];

        if (source.skinIndex) {
            if (*source.skinIndex >= model.skins.size() || !source.meshIndex) {
                invalid("Invalid skin reference on node " + node.name);
            }

            node.skin = *source.skinIndex;
        }

        if (source.lightIndex) {
            if (*source.lightIndex >= model.lights.size()) {
                invalid("Invalid light reference on node " + node.name);
            }

            node.light = *source.lightIndex;
        }

        if (!source.meshIndex) {
            continue;
        }

        node.meshes = meshPrimitives.at(*source.meshIndex);
        const auto targetCount = model.meshes[node.meshes.front()].morphTargetCount;

        if (node.weights.empty()) {
            node.weights.assign(targetCount, 0);
        }

        if (node.weights.size() != targetCount) {
            invalid("Morph weight count mismatch on node " + node.name);
        }

        for (auto primitive : node.meshes) {
            const auto& mesh = model.meshes[primitive];

            if (mesh.morphTargetCount != targetCount) {
                invalid("Inconsistent primitive morph counts");
            }

            if (!node.skin) {
                continue;
            }

            const auto& sourcePrimitive = asset.meshes[*source.meshIndex].primitives[primitive - node.meshes.front()];
            validateSkinnedPrimitive(sourcePrimitive, mesh, model.skins[*node.skin], node.name);
        }

        if (node.skin && model.animations.empty()) {
            node.bindPosePreview = isBindPosePreviewCandidate(model, model.skins[*node.skin]);
        }
    }
}

void resolveBindPosePreviews(ModelData& model) {
    if (std::none_of(model.nodes.begin(), model.nodes.end(), [](const auto& node) {
            return node.bindPosePreview;
        })) {
        return;
    }

    ModelInstance rest{std::shared_ptr<const ModelData>{&model, [](const auto*) {
                                                        }}};

    for (auto& node : model.nodes) {
        if (!node.bindPosePreview) {
            continue;
        }

        const auto& skin = model.skins[*node.skin];
        const auto& firstJointTransform = rest.pose().world[skin.joints.front()];

        for (auto joint : skin.joints) {
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    if (std::abs(rest.pose().world[joint][column][row] - firstJointTransform[column][row]) > 1e-6F) {
                        node.bindPosePreview = false;
                    }
                }
            }
        }

        if (node.bindPosePreview) {
            model.warnings += "Node '" + node.name +
                              "': missing rest joint transforms; GPU undeformed bind-pose "
                              "preview (re-export for the intended pose).\n";
        }
    }
}

} // namespace

ModelData loadGltf(const std::filesystem::path& path) {
    try {
        if (!std::filesystem::is_regular_file(path)) {
            invalid("Model file does not exist (relative paths resolve beside the executable; "
                    "rebuild to stage new assets)");
        }

        // Keep the backing buffer and parser alive throughout model conversion.
        auto data = [&] {
            diagnostics::StartupSpan profile{"asset.glTF.file_read"};

            return fastgltf::GltfDataBuffer::FromPath(path);
        }();

        if (data.error() != fastgltf::Error::None) {
            invalid(std::string{fastgltf::getErrorMessage(data.error())});
        }

        fastgltf::Parser parser{
            fastgltf::Extensions::KHR_texture_transform | fastgltf::Extensions::KHR_materials_unlit |
            fastgltf::Extensions::KHR_materials_emissive_strength | fastgltf::Extensions::KHR_materials_clearcoat |
            fastgltf::Extensions::KHR_materials_specular | fastgltf::Extensions::KHR_materials_ior |
            fastgltf::Extensions::KHR_materials_transmission | fastgltf::Extensions::KHR_materials_volume |
            fastgltf::Extensions::KHR_materials_sheen | fastgltf::Extensions::KHR_lights_punctual};
        const auto directory = path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."};
        auto parsed = parseGltfAsset(parser, data.get(), directory, path);
        const auto& asset = parsed.get();
        validateGltfAsset(asset);

        ModelData model;
        loadGltfMaterials(asset, directory, model);

        if (!asset.cameras.empty()) {
            model.warnings += "Imported cameras are ignored; the application camera is used.\n";
        }

        readModelNodes(asset, model);
        selectModelRoots(asset, model);
        loadGltfAnimation(asset, model);

        const auto meshPrimitives = readModelMeshes(asset, model);
        bindModelNodes(asset, model, meshPrimitives);
        resolveBindPosePreviews(model);

        std::clog << "glTF '" << path.string() << "': " << model.meshes.size() << " primitives, " << model.skins.size()
                  << " skins, " << model.animations.size() << " clips, " << model.lights.size()
                  << " punctual lights; extensions:";
        for (const auto& extension : asset.extensionsUsed) {
            std::clog << ' ' << extension;
        }

        std::clog << '\n';
        updateModelBounds(model);

        return model;
    } catch (const std::exception& error) {
        const std::string reason{error.what()};
        throw std::runtime_error(
            "Cannot load glTF model '" + path.string() + "': " + reason +
            (reason.find("extensionsUsed=") == std::string::npos ? extensionDiagnostics(path) : ""));
    }
}

} // namespace grimlock::assets
