#include "assets/model_loader.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Json = nlohmann::json;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Json writeFixtureBuffer(const std::filesystem::path& output) {
    Json document = {{"asset", {{"version", "2.0"}}},
                     {"bufferViews", Json::array()},
                     {"accessors", Json::array()},
                     {"meshes", Json::array({{{"primitives", Json::array({{{"attributes", Json::object()}}})}}})},
                     {"nodes", Json::array({{{"mesh", 0}}})},
                     {"scenes", Json::array({{{"nodes", {0}}}})},
                     {"scene", 0}};
    std::ofstream buffer(output / "primitive.bin", std::ios::binary);
    const auto accessor = [&](const auto& values, const char* type, int componentType, std::size_t count) {
        const auto index = document["accessors"].size();
        const auto offset = static_cast<std::size_t>(buffer.tellp());
        buffer.write(reinterpret_cast<const char*>(values.data()), sizeof(values));
        document["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", offset}, {"byteLength", sizeof(values)}});
        document["accessors"].push_back(
            {{"bufferView", index}, {"componentType", componentType}, {"count", count}, {"type", type}});
        return index;
    };

    auto& primitive = document["meshes"][0]["primitives"][0];
    auto& attributes = primitive["attributes"];
    attributes["POSITION"] = accessor(std::array<float, 12>{0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 0}, "VEC3", 5126, 4);
    document["accessors"][0]["min"] = {0, 0, 0};
    document["accessors"][0]["max"] = {2, 2, 0};
    attributes["NORMAL"] = accessor(std::array<float, 12>{0, 2, 0, 0, 0, 0, 0, 0, 2, 0, 0, 2}, "VEC3", 5126, 4);
    attributes["TEXCOORD_0"] = accessor(std::array<float, 8>{0, 0, 1, 0, 0, 1, 0, 0}, "VEC2", 5126, 4);
    attributes["TEXCOORD_1"] = accessor(std::array<float, 8>{0, 0, 2, 0, 0, 2, 0, 0}, "VEC2", 5126, 4);
    attributes["TANGENT"] =
        accessor(std::array<float, 16>{2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1}, "VEC4", 5126, 4);
    attributes["JOINTS_0"] =
        accessor(std::array<std::uint16_t, 16>{0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3}, "VEC4", 5123, 4);
    attributes["JOINTS_1"] =
        accessor(std::array<std::uint16_t, 16>{4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7}, "VEC4", 5123, 4);
    const std::array<float, 16> weights{2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0};
    attributes["WEIGHTS_0"] = accessor(weights, "VEC4", 5126, 4);
    attributes["WEIGHTS_1"] = accessor(weights, "VEC4", 5126, 4);
    const auto morph = accessor(std::array<float, 12>{0, 0, 1, 0, 0, 2, 0, 0, 3, 0, 0, 4}, "VEC3", 5126, 4);
    primitive["targets"] = Json::array({{{"POSITION", morph}}});
    primitive["indices"] = accessor(std::array<std::uint16_t, 6>{2, 0, 1, 0, 3, 1}, "SCALAR", 5123, 6);
    document["buffers"] =
        Json::array({{{"uri", "primitive.bin"}, {"byteLength", static_cast<std::size_t>(buffer.tellp())}}});
    require(static_cast<bool>(buffer), "cannot write primitive buffer");
    return document;
}

grimlock::assets::ModelData loadFixture(const std::filesystem::path& output, const Json& document) {
    const auto path = output / "primitive.gltf";
    {
        std::ofstream file(path);
        file << document.dump();
        require(static_cast<bool>(file), "cannot write primitive document");
    }

    return grimlock::assets::loadGltf(path);
}

void checkGeometry(const grimlock::assets::ModelData& model, bool suppliedNormals = true) {
    require(model.meshes.size() == 1, "primitive count changed");
    const auto& mesh = model.meshes.front();
    require(mesh.vertices.size() == 3 && mesh.indices == std::vector<std::uint32_t>{0, 1, 2},
            "degenerate filtering/flat vertex indices changed");
    require(model.warnings.find("Skipped 1 degenerate triangle(s)") != std::string::npos, "degenerate warning changed");
    require(mesh.morphVertexCount == 4 && mesh.morphTargetCount == 1 && mesh.morphDeltas.size() == 4,
            "morph storage was changed by vertex splitting");
    constexpr std::array<std::uint32_t, 3> originalVertices{2, 0, 1};
    const std::array positions{glm::vec3{0, 2, 0}, glm::vec3{0, 0, 0}, glm::vec3{2, 0, 0}};

    for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
        const auto& vertex = mesh.vertices[index];
        const auto original = originalVertices[index];
        const auto expectedNormal = suppliedNormals && original == 0 ? glm::vec3{0, 1, 0} : glm::vec3{0, 0, 1};
        require(vertex.position == positions[index], "flat vertex ordering changed");
        require(vertex.normal == expectedNormal, "normal normalization/generation changed");
        require(vertex.tangent == glm::vec4{1, 0, 0, 1}, "imported tangent changed");
        require(vertex.joints0 == glm::uvec4{0, 1, 2, 3} && vertex.joints1 == glm::uvec4{4, 5, 6, 7},
                "skin joint lanes changed");
        require(vertex.weights0 == glm::vec4{0.5F, 0, 0, 0} && vertex.weights1 == glm::vec4{0.5F, 0, 0, 0},
                "eight-influence weight normalization changed");
        require(vertex.morphIndex == original && mesh.morphDeltas[vertex.morphIndex].position ==
                                                     glm::vec4{0, 0, static_cast<float>(original + 1), 0},
                "vertex splitting lost original morph coordinates");
    }

    require(mesh.vertices[2].uv == glm::vec2{1, 0} && mesh.vertices[0].uv1 == glm::vec2{0, 2},
            "texture coordinates were not preserved");
}

void rejects(const std::filesystem::path& output, const Json& document, std::string_view reason) {
    try {
        (void)loadFixture(output, document);
    } catch (const std::exception& error) {
        require(std::string_view{error.what()}.find(reason) != std::string_view::npos, "unexpected import error");
        return;
    }

    throw std::runtime_error("invalid primitive was accepted");
}

void checkInvalidData(const std::filesystem::path& output, const Json& document) {
    auto unpaired = document;
    unpaired["meshes"][0]["primitives"][0]["attributes"].erase("WEIGHTS_1");
    rejects(output, unpaired, "Unpaired joints/weights");

    auto incomplete = document;
    incomplete["meshes"][0]["primitives"][0].erase("indices");
    rejects(output, incomplete, "Incomplete triangle list");

    const auto& attributes = document["meshes"][0]["primitives"][0]["attributes"];
    const auto tangent = attributes["TANGENT"].get<std::size_t>();
    const auto view = document["accessors"][tangent]["bufferView"].get<std::size_t>();
    const auto offset = document["bufferViews"][view]["byteOffset"].get<std::size_t>() + 3 * sizeof(float);
    {
        std::fstream buffer(output / "primitive.bin", std::ios::binary | std::ios::in | std::ios::out);
        const float invalidHandedness = 0;
        buffer.seekp(static_cast<std::streamoff>(offset));
        buffer.write(reinterpret_cast<const char*>(&invalidHandedness), sizeof(invalidHandedness));
        require(static_cast<bool>(buffer), "cannot modify tangent fixture");
    }

    rejects(output, document, "Invalid tangent vector/handedness");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected fixture output path");
        const std::filesystem::path output{argv[1]};
        std::filesystem::create_directories(output);
        const auto document = writeFixtureBuffer(output);
        checkGeometry(loadFixture(output, document));

        auto generatedNormals = document;
        generatedNormals["meshes"][0]["primitives"][0]["attributes"].erase("NORMAL");
        checkGeometry(loadFixture(output, generatedNormals), false);

        auto smooth = document;
        auto& smoothPrimitive = smooth["meshes"][0]["primitives"][0];
        smoothPrimitive["attributes"]["NORMAL"] = smoothPrimitive["targets"][0]["POSITION"];
        const auto smoothModel = loadFixture(output, smooth);
        require(smoothModel.meshes.front().vertices.size() == 4 &&
                    smoothModel.meshes.front().indices == std::vector<std::uint32_t>{2, 0, 1},
                "valid supplied normals caused flat vertex splitting");

        auto missingTangents = document;
        missingTangents["meshes"][0]["primitives"][0]["attributes"].erase("TANGENT");
        const auto untangentModel = loadFixture(output, missingTangents);
        require(untangentModel.meshes.front().vertices.front().tangent == glm::vec4{0},
                "missing tangents changed the geometric-normal fallback");

        auto nonIndexed = document;
        nonIndexed["meshes"][0]["primitives"][0].erase("indices");
        for (auto& accessor : nonIndexed["accessors"]) {
            accessor["count"] = 3;
        }

        const auto model = loadFixture(output, nonIndexed);
        require(model.meshes.front().indices == std::vector<std::uint32_t>{0, 1, 2},
                "non-indexed triangles were not generated");
        checkInvalidData(output, document);
        std::cout
            << "PASS glTF primitive: indices, normals, tangents, UVs, eight influences, morph seams and errors.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL glTF primitive: " << error.what() << '\n';
        return 1;
    }
}
