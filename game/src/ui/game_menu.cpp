#include "ui/game_hud.hpp"
#include "ui/game_style.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <stdexcept>

namespace grimlock::game {
namespace {
constexpr auto menuFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoScrollbar;
constexpr std::array<glm::ivec2, 4> resolutions{{{1024, 576}, {1280, 720}, {1600, 900}, {1920, 1080}}};
constexpr const char* resolutionNames[]{"1024 x 576", "1280 x 720", "1600 x 900", "1920 x 1080"};

void imageCover(ImDrawList* draw, ImTextureRef image, ImVec2 a, ImVec2 b, float aspect = 16.0F / 9) {
    const float target = (b.x - a.x) / (b.y - a.y);
    ImVec2 uv0{0, 0}, uv1{1, 1};
    if (target > aspect) {
        const float crop = (1 - aspect / target) * 0.5F;
        uv0.y = crop;
        uv1.y = 1 - crop;
    } else {
        const float crop = (1 - target / aspect) * 0.5F;
        uv0.x = crop;
        uv1.x = 1 - crop;
    }
    draw->AddImage(image, a, b, uv0, uv1);
}
} // namespace

void GameHud::initializeMenu() {
    presentation::menuColorSpace(native_.target.colorFormat == VK_FORMAT_B8G8R8A8_SRGB ||
                                 native_.target.colorFormat == VK_FORMAT_R8G8B8A8_SRGB);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    msaaSamples_ = engine_.msaaSamples();
    shadowQuality_ = engine_.shadowSettings().quality;
    engine_.setRenderSettings({TextureFiltering::anisotropic, 16, ToneMapping::filmic});
    const auto fontPath = engine_.assetRoot() / "ui/karla.ttf";
    if (std::filesystem::exists(fontPath)) {
        menuFont_ = ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 20);
        ImGui::GetIO().FontDefault = menuFont_;
    }
    if (char* folder = optionsPath_.empty() ? SDL_GetPrefPath("KeatonGames", "Grimlock") : nullptr) {
        optionsPath_ = std::filesystem::path{folder} / "options.json";
        SDL_free(folder);
    }
    if (optionsPath_.empty() || !std::filesystem::exists(optionsPath_)) {
        return;
    }
    try {
        std::ifstream input{optionsPath_};
        const auto json = nlohmann::json::parse(input);
        resolution_ = std::clamp(json.value("resolution", 1), 0, 3);
        fullscreen_ = json.value("fullscreen", false);
        vsync_ = json.value("vsync", true);
        const auto shadowResolution = json.value("shadow_resolution", 2048U);
        shadowQuality_ = shadowResolution == 0      ? ShadowQuality::off
                         : shadowResolution == 1024 ? ShadowQuality::low
                         : shadowResolution == 4096 ? ShadowQuality::high
                                                    : ShadowQuality::medium;
        msaaSamples_ = json.value("msaa_samples", 4U);
        if (msaaSamples_ != 1 && msaaSamples_ != 2 && msaaSamples_ != 4 && msaaSamples_ != 8) {
            msaaSamples_ = 4;
        }
        uiScale_ = std::clamp(json.value("ui_scale", 1.0F), 0.85F, 1.25F);
        worldHealthBars_ = json.value("health_bars", true);
        applyOptions();
    } catch (const std::exception& error) {
        optionsError_ = error.what();
    }
}

ImTextureRef GameHud::menuImage(const std::filesystem::path& path) {
    auto found = menuImages_.find(path);
    if (found == menuImages_.end()) {
        int width = 0, height = 0, channels = 0;
        std::ifstream input{path, std::ios::binary};
        const std::vector<unsigned char> encoded{std::istreambuf_iterator<char>{input}, {}};
        if (encoded.empty() || encoded.size() > 64 * 1024 * 1024) {
            throw std::runtime_error("Invalid menu image: " + path.string());
        }
        const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded{
            stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, 4),
            &stbi_image_free};
        auto* pixels = decoded.get();
        if (!pixels) {
            throw std::runtime_error("Cannot load menu image: " + path.string());
        }
        if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
            throw std::runtime_error("Menu images must fit within 4096 x 4096 pixels: " + path.string());
        }
        prepareSrgbPixels({pixels, static_cast<std::size_t>(width) * height * 4});
        auto texture = std::make_unique<ImTextureData>();
        texture->Create(ImTextureFormat_RGBA32, width, height);
        std::memcpy(texture->Pixels, pixels, static_cast<std::size_t>(width) * height * 4);
        found = menuImages_.emplace(path, std::move(texture)).first;
        ImGui::RegisterUserTexture(found->second.get());
    }
    return found->second->GetTexRef();
}

void GameHud::releaseUi() noexcept {
    if (!context_) {
        return;
    }
    ImGui::SetCurrentContext(context_);
    for (auto& [path, texture] : menuImages_) {
        texture->WantDestroyNextFrame = true;
        texture->UnusedFrames = 1000;
        texture->SetStatus(ImTextureStatus_WantDestroy);
        if (texture->BackendUserData) {
            ImGui_ImplVulkan_UpdateTexture(texture.get());
        }
        ImGui::UnregisterUserTexture(texture.get());
        ImGui::GetPlatformIO().Textures.find_erase(texture.get());
    }
    menuImages_.clear();
}

void GameHud::targetChanged(const OverlayTarget& target) {
    const auto srgb = [](VkFormat format) {
        return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB;
    };
    const bool encodingChanged = srgb(native_.target.colorFormat) != srgb(target.colorFormat);
    ui::ImGuiOverlay::targetChanged(target);
    presentation::menuColorSpace(srgb(target.colorFormat));
    if (encodingChanged) {
        std::array<unsigned char, 256> channel;
        for (std::size_t i = 0; i < channel.size(); ++i) {
            const float value = i / 255.0F;
            const float converted =
                srgb(target.colorFormat)
                    ? presentation::menuColor({value, 0, 0, 1}).x
                    : (value <= 0.0031308F ? value * 12.92F : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F);
            channel[i] = static_cast<unsigned char>(std::clamp(std::round(converted * 255), 0.0F, 255.0F));
        }
        // Keep texture references alive: the current frame's draw data already points to them.
        // Backend recreation uploads these re-encoded pixels into the new target's textures.
        for (auto& [path, texture] : menuImages_) {
            for (int i = 0; i < texture->GetSizeInBytes(); i += 4) {
                texture->Pixels[i] = channel[texture->Pixels[i]];
                texture->Pixels[i + 1] = channel[texture->Pixels[i + 1]];
                texture->Pixels[i + 2] = channel[texture->Pixels[i + 2]];
            }
        }
    }
}

void GameHud::menuBackground(bool darkened) {
    auto* viewport = ImGui::GetMainViewport();
    auto* draw = ImGui::GetBackgroundDrawList(viewport);
    const auto a = viewport->Pos;
    const ImVec2 b{a.x + viewport->Size.x, a.y + viewport->Size.y};
    imageCover(draw, menuImage(engine_.assetRoot() / "previews/forest_defense.png"), a, b);
    if (darkened) {
        draw->AddRectFilled(a, b, presentation::menuInk({35 / 255.0F, 30 / 255.0F, 48 / 255.0F, 170 / 255.0F}));
    } else {
        draw->AddRectFilledMultiColor(a,
                                      b,
                                      presentation::menuInk({35 / 255.0F, 30 / 255.0F, 48 / 255.0F, 248 / 255.0F}),
                                      presentation::menuInk({35 / 255.0F, 30 / 255.0F, 48 / 255.0F, 0}),
                                      presentation::menuInk({35 / 255.0F, 30 / 255.0F, 48 / 255.0F, 30 / 255.0F}),
                                      presentation::menuInk({35 / 255.0F, 30 / 255.0F, 48 / 255.0F, 248 / 255.0F}));
    }
    // A short fade-in hides abrupt screen changes without keeping a live menu scene.
    draw->AddRectFilled(a,
                        b,
                        presentation::menuInk({35 / 255.0F, 30 / 255.0F, 48 / 255.0F, (1 - entrance_) * 120 / 255.0F}));
}

void GameHud::menu() {
    presentation::MenuTheme theme;
    ImGui::PushFont(menuFont_, 20);
    const auto* v = ImGui::GetMainViewport();
    const float s = layout_.scale;
    const bool selecting = screen_ == Screen::worldMap;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (settingsOpen_) {
            settingsOpen_ = false;
        } else if (selecting) {
            requestedScreen_ = Screen::mainMenu;
        }
    }
    if (!selecting) {
        menuBackground(false);
    }
    if (catalog_.levels.empty() && refreshRemaining_ <= 0) {
        refreshLevels();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4F + 0.6F * entrance_);
    ImGui::BeginDisabled(settingsOpen_);
    if (!selecting) {
        const float width = std::min(410 * s, v->Size.x - 48 * s);
        ImGui::SetNextWindowPos({v->Pos.x + v->Size.x * 0.065F, v->Pos.y + v->Size.y * 0.13F});
        ImGui::SetNextWindowSize({width, v->Size.y * 0.82F});
        if (ImGui::Begin("Main Menu", nullptr, menuFlags | ImGuiWindowFlags_NoBackground)) {
            ImGui::PushFont(menuFont_, std::min(58.0F, width / (5.8F * s)));
            ImGui::TextColored(presentation::menuColor(presentation::ivory), "GRIMLOCK");
            const auto titlePosition = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText({titlePosition.x + 1.2F, titlePosition.y},
                                                presentation::menuInk(presentation::ivory),
                                                "GRIMLOCK");
            ImGui::PopFont();
            const auto p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(p,
                                                {p.x + std::min(260 * s, width - 32 * s), p.y},
                                                presentation::menuInk(presentation::menuGold),
                                                1);
            ImGui::Dummy({0, 12 * s});
            ImGui::PushFont(menuFont_, 15);
            ImGui::TextWrapped("HOLD THE LINE. DEFEND THE MAUSOLEUM.");
            ImGui::PopFont();
            ImGui::Dummy({0, std::clamp(v->Size.y * 0.07F, 12.0F, 50.0F)});
            const float button = std::min(54 * s, v->Size.y * 0.09F);
            if (presentation::menuAction("PLAY", {-1, button}, true)) {
                selectedLevel_.reset();
                requestedScreen_ = Screen::worldMap;
            }
            if (presentation::menuAction("OPTIONS###Settings", {-1, button})) {
                settingsOpen_ = true;
            }
            if (presentation::menuAction("QUIT GAME###Quit", {-1, button}, false, true)) {
                quit_ = true;
            }
            progressNotice();
        }
        ImGui::End();
    } else {
        worldMap();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopFont();
}

void GameHud::worldMap() {
    const auto* v = ImGui::GetMainViewport();
    if (!engine_.scene() || engine_.scene()->name() != "Kingdom World Map") {
        try {
            engine_.setScene(engine_.loadScene(engine_.assetRoot() / "scenes/world_map.json"));
            engine_.setCameraNavigationEnabled(true);
            auto controls = engine_.cameraNavigationConfig();
            controls.strategyControls = true;
            controls.keyboardMovement = true;
            controls.panHalfExtent = glm::vec2{35, 28};
            controls.minPitch = 0.8F;
            controls.maxPitch = 1.3F;
            controls.minDistance = 20;
            controls.maxDistance = 70;
            engine_.configureCameraNavigation(controls);
            engine_.setObjectSelectionEnabled(false);
            mapLaunch_ = 0;
        } catch (const std::exception& e) {
            error_ = e.what();
            requestedScreen_ = Screen::mainMenu;
            return;
        }
    }
    auto& scene = *engine_.scene();
    auto camera = scene.resolvedCamera();
    glm::vec3 target{0, 1, 2};
    bool markerHovered = false;
    bool panelHovered = false;
    for (const auto& [id, object] : scene.objects()) {
        constexpr std::string_view prefix = "grimlock.map_location:";
        if (!scene.enabledInHierarchy(id) || !object.role.starts_with(prefix)) {
            continue;
        }
        const auto key = object.role.substr(prefix.size());
        const auto level = std::find_if(catalog_.levels.begin(), catalog_.levels.end(), [&](const auto& item) {
            return item.id == key;
        });
        const bool available = level != catalog_.levels.end() && levelUnlocked(*level, runtime_.progress());
        const bool selected = level != catalog_.levels.end() && selectedLevel_ == level->path;
        const glm::vec3 position = scene.worldTransform(id)[3];
        if (selected) {
            target = position * 0.65F + glm::vec3{0, 1, 0};
        }
        const auto clip =
            camera.projection(v->Size.x / v->Size.y) * camera.view() * glm::vec4{position + glm::vec3{0, 5, 0}, 1};
        if (clip.w <= 0) {
            continue;
        }
        const ImVec2 pixel{v->Pos.x + (clip.x / clip.w + 1) * v->Size.x * 0.5F,
                           v->Pos.y + (clip.y / clip.w + 1) * v->Size.y * 0.5F};
        ImGui::SetNextWindowPos({pixel.x - 24, pixel.y - 24});
        ImGui::SetNextWindowSize({48, 48});
        const auto window = "Map marker##" + std::to_string(id);
        ImGui::Begin(window.c_str(), nullptr, menuFlags | ImGuiWindowFlags_NoBackground);
        const bool clicked = ImGui::InvisibleButton("Location", {32, 32});
        const bool hovered = ImGui::IsItemHovered();
        markerHovered |= hovered;
        auto* draw = ImGui::GetWindowDrawList();
        const auto a = ImGui::GetItemRectMin();
        const ImVec2 center{a.x + 16, a.y + 16};
        draw->AddCircleFilled(center,
                              selected  ? 13.0F
                              : hovered ? 11.0F
                                        : 8.0F,
                              presentation::menuInk(available ? presentation::menuGold : presentation::stone));
        draw->AddCircle(center,
                        18 + std::sin(static_cast<float>(ImGui::GetTime()) * 2) * 2,
                        presentation::menuInk(presentation::ivory),
                        24,
                        1.5F);
        if (hovered) {
            ImGui::SetTooltip("%s%s",
                              object.name.c_str(),
                              available                        ? ""
                              : level != catalog_.levels.end() ? " (Locked)"
                                                               : " (Unavailable)");
        }
        if (clicked && !settingsOpen_ && mapLaunch_ == 0) {
            if (level != catalog_.levels.end()) {
                selectedLevel_ = level->path;
                mapFocus_ = true;
                error_.clear();
            } else {
                selectedLevel_.reset();
                error_ = "This location is unavailable.";
            }
        }
        scene.setTint(id, selected ? glm::vec4{1.3F, 1.2F, 0.75F, 1} : hovered ? glm::vec4{1.15F} : glm::vec4{1});
        ImGui::End();
    }
    if (selectedLevel_) {
        const auto level = std::find_if(catalog_.levels.begin(), catalog_.levels.end(), [&](const auto& entry) {
            return entry.path == *selectedLevel_;
        });
        const bool available = level != catalog_.levels.end() &&
                               std::any_of(scene.objects().begin(), scene.objects().end(), [&](const auto& entry) {
                                   return entry.second.role == "grimlock.map_location:" + level->id &&
                                          scene.enabledInHierarchy(entry.first);
                               });
        if (!available) {
            selectedLevel_.reset();
            mapLaunch_ = 0;
        }
    }
    if (mapFocus_ && selectedLevel_) {
        const auto& input = engine_.input();
        if (input.key(SDL_SCANCODE_W).held || input.key(SDL_SCANCODE_A).held || input.key(SDL_SCANCODE_S).held ||
            input.key(SDL_SCANCODE_D).held) {
            mapFocus_ = false;
        } else {
            engine_.followCameraTarget(target, ImGui::GetIO().DeltaTime);
        }
    }
    ImGui::SetNextWindowPos({v->Pos.x + 22, v->Pos.y + 22});
    ImGui::Begin("World Map Navigation", nullptr, menuFlags | ImGuiWindowFlags_AlwaysAutoResize);
    if (presentation::menuAction("BACK", {110, 40})) {
        mapLaunch_ = 0;
        requestedScreen_ = Screen::mainMenu;
    }
    panelHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::TextUnformatted("THE KINGDOM");
    if (!error_.empty()) {
        ImGui::TextWrapped("%s", error_.c_str());
    }
    ImGui::End();
    if (selectedLevel_) {
        const auto level = std::find_if(catalog_.levels.begin(), catalog_.levels.end(), [&](const auto& item) {
            return item.path == *selectedLevel_;
        });
        if (level != catalog_.levels.end()) {
            ImGui::SetNextWindowPos({v->Pos.x + v->Size.x / 2, v->Pos.y + v->Size.y - 24}, ImGuiCond_Always, {0.5F, 1});
            ImGui::SetNextWindowSize({360, 0});
            ImGui::Begin("Map location details", nullptr, menuFlags | ImGuiWindowFlags_AlwaysAutoResize);
            const auto panelPosition = ImGui::GetWindowPos();
            const auto panelSize = ImGui::GetWindowSize();
            panelHovered |= ImGui::IsMouseHoveringRect(panelPosition,
                                                       {panelPosition.x + panelSize.x, panelPosition.y + panelSize.y},
                                                       false);
            ImGui::TextWrapped("%s", level->name.c_str());
            const auto* record = runtime_.progress() ? runtime_.progress()->find(level->id) : nullptr;
            const bool unlocked = levelUnlocked(*level, runtime_.progress());
            ImGui::TextDisabled("%s",
                                !unlocked                     ? "Locked - complete the previous chapter"
                                : record && record->completed ? "Completed"
                                                              : "Unlocked");
            ImGui::TextWrapped("%s", level->description.c_str());
            const auto p = ImGui::GetCursorScreenPos();
            ImGui::BeginDisabled(!unlocked);
            const bool launch = ImGui::InvisibleButton("Launch selected level", {312, 48});
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(p,
                                {p.x + 312, p.y + 48},
                                presentation::menuInk(unlocked ? presentation::menuGold : presentation::stone),
                                4);
            draw->AddTriangleFilled({p.x + 140, p.y + 12},
                                    {p.x + 140, p.y + 36},
                                    {p.x + 163, p.y + 24},
                                    presentation::menuInk(presentation::panel));
            ImGui::EndDisabled();
            if (launch && unlocked && mapLaunch_ == 0) {
                mapLaunch_ = 0.001F;
            }
            ImGui::End();
        }
    }
    if (!panelHovered && !markerHovered && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) &&
        ImGui::IsMouseClicked(0) && mapLaunch_ == 0) {
        selectedLevel_.reset();
        int width = 0, height = 0, drawableWidth = 0, drawableHeight = 0;
        SDL_GetWindowSize(native_.window, &width, &height);
        SDL_GetWindowSizeInPixels(native_.window, &drawableWidth, &drawableHeight);
        const auto mouse = ImGui::GetIO().MousePos;
        const auto ray = mouseRay(camera,
                                  {mouse.x - v->Pos.x, mouse.y - v->Pos.y},
                                  {width, height},
                                  {drawableWidth, drawableHeight});
        if (ray) {
            auto hit = engine_.pickObject(*ray);
            while (hit) {
                const auto& picked = scene.object(*hit);
                if (picked.role.starts_with("grimlock.map_location:")) {
                    const auto key = picked.role.substr(std::string{"grimlock.map_location:"}.size());
                    const auto level =
                        std::find_if(catalog_.levels.begin(), catalog_.levels.end(), [&](const auto& entry) {
                            return entry.id == key;
                        });
                    if (level != catalog_.levels.end()) {
                        selectedLevel_ = level->path;
                        mapFocus_ = true;
                    }
                    break;
                }
                hit = picked.parent;
            }
        }
    }
    if (mapLaunch_ > 0) {
        mapLaunch_ += ImGui::GetIO().DeltaTime;
        ImGui::GetForegroundDrawList()->AddRectFilled(
            v->Pos,
            {v->Pos.x + v->Size.x, v->Pos.y + v->Size.y},
            IM_COL32(20, 24, 22, static_cast<int>(std::min(1.0F, mapLaunch_ / 0.35F) * 255)));
        if (mapLaunch_ >= 0.35F) {
            requestedLevel_ = selectedLevel_;
            mapLaunch_ = 0;
        }
    }
}

void GameHud::pauseMenu() {
    presentation::MenuTheme theme;
    ImGui::PushFont(menuFont_, 20);
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        viewport->Pos,
        {viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y},
        presentation::menuInk({35 / 255.0F, 30 / 255.0F, 48 / 255.0F, 0.72F}));
    if (settingsOpen_) {
        settings();
        ImGui::PopFont();
        return;
    }
    const auto* v = ImGui::GetMainViewport();
    const float s = layout_.scale;
    ImGui::SetNextWindowPos({v->Pos.x + v->Size.x * 0.5F, v->Pos.y + v->Size.y * 0.5F}, ImGuiCond_Always, {0.5F, 0.5F});
    ImGui::SetNextWindowSize({std::min(420 * s, v->Size.x - 32 * s), 0});
    if (ImGui::Begin("Pause Menu", nullptr, menuFlags | ImGuiWindowFlags_AlwaysAutoResize)) {
        presentation::menuHeading("PAUSED", "The forest holds its breath.");
        ImGui::Dummy({0, 14 * s});
        if (presentation::menuAction("RESUME###Resume", {-1, 46 * s}, true)) {
            runtime_.setPaused(false);
        }
        if (presentation::menuAction("RESTART LEVEL###Restart Level", {-1, 46 * s})) {
            restart_ = true;
        }
        if (presentation::menuAction("OPTIONS###Settings", {-1, 46 * s})) {
            settingsOpen_ = true;
        }
        if (presentation::menuAction("MAIN MENU###Main Menu", {-1, 46 * s})) {
            requestedScreen_ = Screen::mainMenu;
        }
    }
    ImGui::End();
    ImGui::PopFont();
}

void GameHud::saveOptions() {
    if (optionsPath_.empty()) {
        optionsError_ = "Settings folder is unavailable.";
        return;
    }
    try {
        std::filesystem::create_directories(optionsPath_.parent_path());
        std::ofstream output{optionsPath_};
        if (!(output << nlohmann::json{{"fullscreen", fullscreen_},
                                       {"resolution", resolution_},
                                       {"vsync", vsync_},
                                       {"msaa_samples", msaaSamples_},
                                       {"shadow_resolution", static_cast<std::uint32_t>(shadowQuality_)},
                                       {"ui_scale", uiScale_},
                                       {"health_bars", worldHealthBars_}}
                            .dump(2))) {
            throw std::runtime_error("Could not save display preferences.");
        }
        output.close();
        if (!output) {
            throw std::runtime_error("Could not finish saving display preferences.");
        }
    } catch (const std::exception& error) {
        optionsError_ = error.what();
    }
}

void GameHud::applyOptions() {
    optionsError_.clear();
    try {
        engine_.setVsync(vsync_);
        auto shadowSettings = engine_.shadowSettings();
        shadowSettings.quality = shadowQuality_;
        engine_.setShadowSettings(shadowSettings);
        shadowQuality_ = engine_.shadowSettings().quality;
        engine_.setMsaaSamples(msaaSamples_);
        msaaSamples_ = engine_.msaaSamples();
        if (!SDL_SetWindowFullscreen(native_.window, fullscreen_)) {
            throw std::runtime_error(SDL_GetError());
        }
        if (!fullscreen_) {
            const auto size = resolutions.at(resolution_);
            if (!SDL_SetWindowSize(native_.window, size.x, size.y)) {
                throw std::runtime_error(SDL_GetError());
            }
        }
    } catch (const std::exception& error) {
        optionsError_ = error.what();
    }
}

void GameHud::settings() {
    presentation::MenuTheme theme;
    ImGui::PushFont(menuFont_, 20);
    const auto* v = ImGui::GetMainViewport();
    const float s = layout_.scale;
    ImGui::SetNextWindowPos({v->Pos.x + v->Size.x * 0.5F, v->Pos.y + v->Size.y * 0.5F}, ImGuiCond_Always, {0.5F, 0.5F});
    ImGui::SetNextWindowSize({std::min(500 * s, v->Size.x - 32 * s), std::min(570 * s, v->Size.y - 32 * s)});
    if (ImGui::Begin("Settings", nullptr, menuFlags & ~ImGuiWindowFlags_NoScrollbar)) {
        presentation::menuHeading("OPTIONS", "Make yourself at home.");
        ImGui::Dummy({0, 14 * s});
        int mode = fullscreen_ ? 1 : 0;
        ImGui::SetNextItemWidth(-150 * s);
        if (ImGui::Combo("Display mode", &mode, "Windowed\0Fullscreen\0")) {
            fullscreen_ = mode == 1;
        }
        ImGui::TextDisabled("Fullscreen uses your desktop resolution.");
        ImGui::BeginDisabled(fullscreen_);
        ImGui::SetNextItemWidth(-150 * s);
        ImGui::Combo("Resolution", &resolution_, resolutionNames, 4);
        ImGui::EndDisabled();
        ImGui::Checkbox("VSync", &vsync_);
        const auto supported = engine_.supportedMsaaSamples();
        const auto sampleLabel = [](std::uint32_t count) {
            return count == 1 ? std::string{"Off"} : std::to_string(count) + "x";
        };
        ImGui::SetNextItemWidth(-150 * s);
        if (ImGui::BeginCombo("MSAA", sampleLabel(msaaSamples_).c_str())) {
            for (const auto count : supported) {
                if (ImGui::Selectable(sampleLabel(count).c_str(), count == msaaSamples_)) {
                    msaaSamples_ = count;
                }
            }
            ImGui::EndCombo();
        }
        const auto shadowLabel = [](ShadowQuality quality) {
            switch (quality) {
            case ShadowQuality::low:
                return "Low (1024)";
            case ShadowQuality::medium:
                return "Medium (2048)";
            case ShadowQuality::high:
                return "High (4096)";
            default:
                return "Off";
            }
        };
        ImGui::SetNextItemWidth(-150 * s);
        if (ImGui::BeginCombo("Shadows", shadowLabel(shadowQuality_))) {
            for (const auto quality : engine_.supportedShadowQualities()) {
                if (ImGui::Selectable(shadowLabel(quality), quality == shadowQuality_)) {
                    shadowQuality_ = quality;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(-100 * s);
        ImGui::SliderFloat("UI Scale", &uiScale_, 0.85F, 1.25F, "%.2fx");
        ImGui::Checkbox("World health bars", &worldHealthBars_);
        ImGui::Dummy({0, 14 * s});
        if (presentation::menuAction("APPLY", {-1, 42 * s}, true)) {
            applyOptions();
            if (optionsError_.empty()) {
                saveOptions();
            }
        }
        if (presentation::menuAction("BACK###Back", {-1, 42 * s}) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            settingsOpen_ = false;
        }
        if (!optionsError_.empty()) {
            ImGui::TextWrapped("%s", optionsError_.c_str());
        }
    }
    ImGui::End();
    ImGui::PopFont();
}
} // namespace grimlock::game
