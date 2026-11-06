#include "assets/model_loader.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <grimlock/assets/animation.hpp>
#include <iostream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using Json = nlohmann::json;

void require(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void append(std::vector<std::uint8_t>& bytes, const void* data, std::size_t size) {
    const auto* begin = static_cast<const std::uint8_t*>(data);
    bytes.insert(bytes.end(), begin, begin + size);
}

void convertObj(const std::filesystem::path& input, const std::filesystem::path& output) {
    const auto model = grimlock::assets::loadModel(input);
    Json document = {{"asset", {{"version", "2.0"}}},
                     {"scene", 0},
                     {"scenes", Json::array({{{"nodes", Json::array()}}})},
                     {"nodes", Json::array()},
                     {"meshes", Json::array()},
                     {"bufferViews", Json::array()},
                     {"accessors", Json::array()},
                     {"materials",
                      Json::array({{{"doubleSided", true},
                                    {"pbrMetallicRoughness", {{"metallicFactor", 0}, {"roughnessFactor", 0.7}}}}})}};
    std::vector<std::uint8_t> binary;
    for (const auto& mesh : model.meshes) {
        const auto view = document["bufferViews"].size();
        const auto accessor = document["accessors"].size();
        const auto vertexOffset = binary.size();
        glm::vec3 minimum{std::numeric_limits<float>::max()}, maximum{-std::numeric_limits<float>::max()};
        for (const auto& vertex : mesh.vertices) {
            const float values[] = {vertex.position.x,
                                    vertex.position.y,
                                    vertex.position.z,
                                    vertex.normal.x,
                                    vertex.normal.y,
                                    vertex.normal.z,
                                    vertex.uv.x,
                                    vertex.uv.y};
            append(binary, values, sizeof(values));
            minimum = glm::min(minimum, vertex.position);
            maximum = glm::max(maximum, vertex.position);
        }
        document["bufferViews"].push_back({{"buffer", 0},
                                           {"byteOffset", vertexOffset},
                                           {"byteLength", binary.size() - vertexOffset},
                                           {"byteStride", 32},
                                           {"target", 34962}});
        for (int attribute = 0; attribute < 3; ++attribute) {
            Json item = {{"bufferView", view},
                         {"byteOffset", attribute == 2 ? 24 : attribute * 12},
                         {"componentType", 5126},
                         {"count", mesh.vertices.size()},
                         {"type", attribute == 2 ? "VEC2" : "VEC3"}};
            if (attribute == 0) {
                item["min"] = {minimum.x, minimum.y, minimum.z};
                item["max"] = {maximum.x, maximum.y, maximum.z};
            }
            document["accessors"].push_back(item);
        }
        const auto indexOffset = binary.size();
        append(binary, mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t));
        document["bufferViews"].push_back({{"buffer", 0},
                                           {"byteOffset", indexOffset},
                                           {"byteLength", binary.size() - indexOffset},
                                           {"target", 34963}});
        document["accessors"].push_back(
            {{"bufferView", view + 1}, {"componentType", 5125}, {"count", mesh.indices.size()}, {"type", "SCALAR"}});
        document["scenes"][0]["nodes"].push_back(document["nodes"].size());
        document["nodes"].push_back({{"name", mesh.name}, {"mesh", document["meshes"].size()}});
        document["meshes"].push_back(
            {{"name", mesh.name},
             {"primitives",
              Json::array(
                  {{{"attributes", {{"POSITION", accessor}, {"NORMAL", accessor + 1}, {"TEXCOORD_0", accessor + 2}}},
                    {"indices", accessor + 3},
                    {"material", 0}}})}});
    }
    document["buffers"] = Json::array({{{"byteLength", binary.size()}}});
    auto json = document.dump();
    while (json.size() % 4) {
        json.push_back(' ');
    }
    std::filesystem::create_directories(output.parent_path());
    std::ofstream stream{output, std::ios::binary};
    require(static_cast<bool>(stream), "Cannot write converted GLB");
    const auto word = [&](std::uint32_t value) {
        stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    word(0x46546c67);
    word(2);
    word(static_cast<std::uint32_t>(28 + json.size() + binary.size()));
    word(static_cast<std::uint32_t>(json.size()));
    word(0x4e4f534a);
    stream.write(json.data(), static_cast<std::streamsize>(json.size()));
    word(static_cast<std::uint32_t>(binary.size()));
    word(0x004e4942);
    stream.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
    stream.close();
    require(!stream.fail(), "Failed writing converted GLB");
    const auto converted = grimlock::assets::loadModel(output);
    require(model.meshes.size() == converted.meshes.size(), "OBJ conversion lost mesh parts");
    for (std::size_t i = 0; i < model.meshes.size(); ++i) {
        require(model.meshes[i].indices.size() == converted.meshes[i].indices.size(), "OBJ conversion lost triangles");
    }
    require(glm::length(model.minimum - converted.minimum) < 1e-4F &&
                glm::length(model.maximum - converted.maximum) < 1e-4F,
            "OBJ conversion changed bounds");
}

void references(const Json& value, const std::filesystem::path& root) {
    if (value.is_string()) {
        auto path = value.get<std::string>();
        if (path.ends_with(".glb") || path.ends_with(".gltf") || path.ends_with(".obj")) {
            require(path.ends_with(".glb") && std::filesystem::is_regular_file(root / path),
                    "Broken/non-GLB reference: " + path);
        }
    } else if (value.is_structured()) {
        for (const auto& item : value) {
            references(item, root);
        }
    }
}

void validate(const std::filesystem::path& root) {
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto path = entry.path();
        const auto extension = path.extension();
        require(extension != ".obj" && extension != ".gltf" && extension != ".bin" && extension != ".fbx" &&
                    extension != ".mtl",
                "Obsolete model format: " + path.string());
        if (extension == ".json") {
            std::ifstream file{path};
            references(Json::parse(file), root);
        }
        if (extension != ".glb") {
            continue;
        }
        std::ifstream file{path, std::ios::binary};
        std::uint32_t header[5]{};
        file.read(reinterpret_cast<char*>(header), sizeof(header));
        require(header[0] == 0x46546c67 && header[1] == 2 && header[4] == 0x4e4f534a &&
                    header[2] == entry.file_size() && entry.file_size() >= 20 && header[3] <= entry.file_size() - 20,
                "Invalid GLB header: " + path.string());
        std::string json(header[3], ' ');
        file.read(json.data(), static_cast<std::streamsize>(json.size()));
        const auto document = Json::parse(json);
        for (const auto& buffer : document.value("buffers", Json::array())) {
            require(!buffer.contains("uri"), "External GLB buffer: " + path.string());
        }
        for (const auto& image : document.value("images", Json::array())) {
            require(!image.contains("uri") && image.contains("bufferView"), "External GLB texture: " + path.string());
        }
        auto model = std::make_shared<grimlock::assets::ModelData>(grimlock::assets::loadModel(path));
        grimlock::assets::ModelInstance instance{model};
        for (std::size_t clip = 0; clip < model->animations.size(); ++clip) {
            instance.play(clip, false);
            instance.seek((model->animations[clip].start + model->animations[clip].end) * 0.5F);
        }
        ++count;
    }
    require(count > 0, "Empty asset library");
    std::cout << "PASS " << count << " self-contained GLBs and all authored JSON model references.\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view{argv[1]} == "--convert-obj") {
            convertObj(argv[2], argv[3]);
        } else {
            require(argc == 2 || argc == 3, "Expected asset root [migration manifest] or --convert-obj input output");
            if (argc == 3) {
                std::ifstream manifest{argv[2]};
                require(static_cast<bool>(manifest), "Cannot read asset migration manifest");
                references(Json::parse(manifest), argv[1]);
            }
            validate(argv[1]);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL asset library: " << error.what() << '\n';
        return 1;
    }
}
