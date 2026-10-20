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
    Json document = {{"asset", {{"version", "2.0"}}}, {"bufferViews", Json::array()}, {"accessors", Json::array()}};
    std::ofstream buffer(output / "model.bin", std::ios::binary);
    const auto accessor = [&](const auto& values, const char* type, int componentType, std::size_t count) {
        const auto index = document["accessors"].size();
        const auto offset = static_cast<std::size_t>(buffer.tellp());
        buffer.write(reinterpret_cast<const char*>(values.data()), sizeof(values));
        document["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", offset}, {"byteLength", sizeof(values)}});
        document["accessors"].push_back(
            {{"bufferView", index}, {"componentType", componentType}, {"count", count}, {"type", type}});
        return index;
    };

    const auto positions = accessor(std::array<float, 9>{0, 0, 0, 1, 0, 0, 0, 1, 0}, "VEC3", 5126, 3);
    document["accessors"][positions]["min"] = {0, 0, 0};
    document["accessors"][positions]["max"] = {1, 1, 0};
    const auto morph = accessor(std::array<float, 9>{}, "VEC3", 5126, 3);
    const auto joints = accessor(std::array<std::uint16_t, 12>{}, "VEC4", 5123, 3);
    const auto weights = accessor(std::array<float, 12>{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}, "VEC4", 5126, 3);
    const auto binds = accessor(std::array<float, 32>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1,
                                                      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 0, 0, 1},
                                "MAT4",
                                5126,
                                2);
    const Json primitive = {{"attributes", {{"POSITION", positions}, {"JOINTS_0", joints}, {"WEIGHTS_0", weights}}},
                            {"targets", Json::array({{{"POSITION", morph}}})}};
    document["meshes"] = Json::array({{{"name", "shared"}, {"primitives", {primitive, primitive}}, {"weights", {0.25}}},
                                      {{"name", "unused"}, {"primitives", {primitive}}}});
    document["nodes"] = Json::array({{{"name", "group"}, {"translation", {10, 0, 0}}, {"children", {1, 2}}},
                                     {{"name", "left"}, {"mesh", 0}, {"translation", {1, 0, 0}}, {"weights", {0.75}}},
                                     {{"name", "right"},
                                      {"mesh", 0},
                                      {"matrix", {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 2, 0, 1}},
                                      {"extensions", {{"KHR_lights_punctual", {{"light", 0}}}}}},
                                     {{"name", "unused"}, {"mesh", 1}, {"translation", {100, 0, 0}}}});
    document["scenes"] = Json::array({{{"nodes", {3}}}, {{"nodes", {0}}}});
    document["scene"] = 1;
    document["cameras"] = Json::array({{{"type", "perspective"}, {"perspective", {{"yfov", 1}, {"znear", 0.1}}}}});
    document["extensionsUsed"] = {"KHR_lights_punctual"};
    document["extensions"] = {{"KHR_lights_punctual", {{"lights", Json::array({{{"type", "point"}}})}}}};
    document["buffers"] =
        Json::array({{{"uri", "model.bin"}, {"byteLength", static_cast<std::size_t>(buffer.tellp())}}});
    // An unreferenced accessor keeps the same fixture buffer available for the skin cases.
    require(binds == 4 && static_cast<bool>(buffer), "cannot write model buffer");
    return document;
}

grimlock::assets::ModelData loadFixture(const std::filesystem::path& output, const Json& document) {
    const auto path = output / "model.gltf";
    {
        std::ofstream file(path);
        file << document.dump();
        require(static_cast<bool>(file), "cannot write model document");
    }

    return grimlock::assets::loadGltf(path);
}

void checkAssembly(const std::filesystem::path& output, const Json& document) {
    const auto model = loadFixture(output, document);
    require(model.nodes.size() == 4 && model.meshes.size() == 3, "node/primitive counts changed");
    require(model.roots == std::vector<std::size_t>{0}, "default scene was not selected");
    require(model.nodes[1].parent == 0 && model.nodes[2].parent == 0 && !model.nodes[3].parent,
            "node parent references changed");
    require(model.nodes[1].meshes == std::vector<std::size_t>{0, 1} && model.nodes[2].meshes == model.nodes[1].meshes &&
                model.nodes[3].meshes == std::vector<std::size_t>{2},
            "shared mesh primitive mapping changed");
    require(model.meshes[0].name == "shared/0" && model.meshes[1].name == "shared/1" &&
                model.meshes[2].name == "unused/0",
            "primitive names/order changed");
    require(!model.nodes[1].matrixTransform && model.nodes[1].translation == glm::vec3{1, 0, 0} &&
                model.nodes[2].matrixTransform && model.nodes[2].localTransform[3] == glm::vec4{0, 2, 0, 1},
            "TRS or explicit matrix conversion changed");
    require(model.nodes[1].weights == std::vector<float>{0.75F} &&
                model.nodes[2].weights == std::vector<float>{0.25F} && model.nodes[3].weights == std::vector<float>{0},
            "node override, mesh default, or zero morph weights changed");
    require(model.minimum == glm::vec3{10, 0, 0} && model.maximum == glm::vec3{12, 3, 0},
            "bounds included an unselected scene or lost hierarchy transforms");
    require(model.lights.size() == 1 && model.nodes[2].light == 0, "light binding changed");
    require(model.warnings.find("Imported cameras are ignored") != std::string::npos, "camera warning changed");

    auto firstScene = document;
    firstScene.erase("scene");
    const auto first = loadFixture(output, firstScene);
    require(first.roots == std::vector<std::size_t>{3} && first.minimum == glm::vec3{100, 0, 0} &&
                first.maximum == glm::vec3{101, 1, 0} && first.warnings.find("No default scene") != std::string::npos,
            "first-scene fallback changed");

    firstScene.erase("scenes");
    const auto allRoots = loadFixture(output, firstScene);
    require(allRoots.roots == std::vector<std::size_t>{0, 3} && allRoots.maximum == glm::vec3{101, 3, 0} &&
                allRoots.warnings.find("No scenes; selected all root nodes") != std::string::npos,
            "root-node fallback changed");
}

Json withSkin(Json document) {
    document["nodes"][0]["children"] = {1, 2, 4, 5};
    document["nodes"][1]["skin"] = 0;
    document["nodes"].push_back({{"name", "joint-a"}});
    document["nodes"].push_back({{"name", "joint-b"}});
    document["skins"] = Json::array({{{"joints", {4, 5}}, {"inverseBindMatrices", 4}}});
    return document;
}

void checkBindPosePreview(const std::filesystem::path& output, const Json& document) {
    auto skinned = withSkin(document);
    const auto preview = loadFixture(output, skinned);
    require(preview.nodes[1].skin == 0 && preview.nodes[1].bindPosePreview &&
                preview.warnings.find("missing rest joint transforms") != std::string::npos,
            "legacy bind-pose preview detection changed");

    skinned["nodes"][0]["children"] = {1, 2, 4, 6};
    skinned["nodes"].push_back({{"translation", {0, 1, 0}}, {"children", {5}}});
    const auto distinctWorlds = loadFixture(output, skinned);
    require(!distinctWorlds.nodes[1].bindPosePreview &&
                distinctWorlds.warnings.find("missing rest joint transforms") == std::string::npos,
            "different world joint transforms retained the bind-pose preview");
}

void rejects(const std::filesystem::path& output, const Json& document, std::string_view reason) {
    try {
        (void)loadFixture(output, document);
    } catch (const std::exception& error) {
        const std::string_view message{error.what()};
        const auto prefix = "Cannot load glTF model '" + (output / "model.gltf").string() + "': ";
        if (!message.starts_with(prefix) || message.find(reason) == std::string_view::npos) {
            throw std::runtime_error("Expected import error containing '" + std::string{reason} +
                                     "'; got: " + error.what());
        }
        return;
    }

    throw std::runtime_error("invalid model was accepted");
}

void checkValidation(const std::filesystem::path& output, const Json& document) {
    auto bad = document;
    bad["asset"]["version"] = "0.0";
    rejects(output, bad, "fastgltf parser error");

    bad = document;
    bad["animations"] =
        Json::array({{{"samplers", {{{"input", 0}, {"output", 0}}}},
                      {"channels", {{{"sampler", 7}, {"target", {{"node", 1}, {"path", "translation"}}}}}}}});
    rejects(output, bad, "Invalid animation reference");
    bad["bufferViews"][0]["byteLength"] = document["buffers"][0]["byteLength"].get<std::size_t>() + 4;
    rejects(output, bad, "Buffer view exceeds its buffer");

    bad = document;
    bad["nodes"][3]["children"] = {1};
    rejects(output, bad, "Invalid child or multiple node parents");

    bad = document;
    bad["nodes"][1]["weights"] = {0.5, 0.5};
    rejects(output, bad, "Morph weight count mismatch");

    bad = withSkin(document);
    auto& attributes = bad["meshes"][0]["primitives"][0]["attributes"];
    attributes.erase("JOINTS_0");
    attributes.erase("WEIGHTS_0");
    rejects(output, bad, "glTF structural validation failed");

    const auto joints = document["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"].get<std::size_t>();
    const auto view = document["accessors"][joints]["bufferView"].get<std::size_t>();
    const auto offset = document["bufferViews"][view]["byteOffset"].get<std::size_t>() + 3 * sizeof(std::uint16_t);
    {
        std::fstream buffer(output / "model.bin", std::ios::binary | std::ios::in | std::ios::out);
        const std::uint16_t invalidJoint = 2;
        buffer.seekp(static_cast<std::streamoff>(offset));
        buffer.write(reinterpret_cast<const char*>(&invalidJoint), sizeof(invalidJoint));
        require(static_cast<bool>(buffer), "cannot modify joint fixture");
    }

    rejects(output, withSkin(document), "Joint index outside skin on node left");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected fixture output path");
        const std::filesystem::path output{argv[1]};
        std::filesystem::create_directories(output);
        const auto document = writeFixtureBuffer(output);
        checkAssembly(output, document);
        checkBindPosePreview(output, document);
        checkValidation(output, document);
        std::cout
            << "PASS glTF assembly: hierarchy, scenes, shared primitives, morph defaults, skins, lights, errors.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL glTF assembly: " << error.what() << '\n';
        return 1;
    }
}
