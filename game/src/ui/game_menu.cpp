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
    const auto fontPath = engine_.assetRoot() / "ui/karla.ttf";
    if (std::filesystem::exists(fontPath)) {
        menuFont_ = ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 20);
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
        // The ImGui backend uploads UNORM textures; compensate only these sRGB screenshots
        // when the final color attachment performs the sRGB encoding.
        if (native_.target.colorFormat == VK_FORMAT_B8G8R8A8_SRGB ||
            native_.target.colorFormat == VK_FORMAT_R8G8B8A8_SRGB) {
            std::array<unsigned char, 256> linearChannel;
            for (std::size_t channel = 0; channel < linearChannel.size(); ++channel) {
                linearChannel[channel] = static_cast<unsigned char>(
                    std::round(presentation::menuColor({channel / 255.0F, 0, 0, 1}).x * 255));
            }
            for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height * 4; i += 4) {
                pixels[i] = linearChannel[pixels[i]];
                pixels[i + 1] = linearChannel[pixels[i + 1]];
                pixels[i + 2] = linearChannel[pixels[i + 2]];
            }
        }
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
    imageCover(draw, menuImage(engine_.assetRoot() / "ui/menu_background.png"), a, b);
    if (darkened) {
        draw->AddRectFilled(a, b, presentation::menuInk({23 / 255.0F, 43 / 255.0F, 38 / 255.0F, 170 / 255.0F}));
    } else {
        draw->AddRectFilledMultiColor(a,
                                      b,
                                      presentation::menuInk({23 / 255.0F, 43 / 255.0F, 38 / 255.0F, 248 / 255.0F}),
                                      presentation::menuInk({23 / 255.0F, 43 / 255.0F, 38 / 255.0F, 0}),
                                      presentation::menuInk({23 / 255.0F, 43 / 255.0F, 38 / 255.0F, 30 / 255.0F}),
                                      presentation::menuInk({23 / 255.0F, 43 / 255.0F, 38 / 255.0F, 248 / 255.0F}));
    }
    // A short fade-in hides abrupt screen changes without keeping a live menu scene.
    draw->AddRectFilled(a,
                        b,
                        presentation::menuInk({23 / 255.0F, 43 / 255.0F, 38 / 255.0F, (1 - entrance_) * 120 / 255.0F}));
}

void GameHud::menu() {
    presentation::MenuTheme theme;
    ImGui::PushFont(menuFont_, 20);
    const auto* v = ImGui::GetMainViewport();
    const float s = layout_.scale;
    const bool selecting = screen_ == Screen::levels;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (settingsOpen_) {
            settingsOpen_ = false;
        } else if (selecting) {
            requestedScreen_ = Screen::mainMenu;
        }
    }
    menuBackground(selecting);
    if (catalog_.levels.empty() && refreshRemaining_ <= 0) {
        refreshLevels();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4F + 0.6F * entrance_);
    ImGui::BeginDisabled(settingsOpen_);
    if (!selecting) {
        const float width = std::min(410 * s, v->Size.x - 48 * s);
        ImGui::SetNextWindowPos({v->Pos.x + v->Size.x * 0.055F, v->Pos.y + v->Size.y * 0.13F});
        ImGui::SetNextWindowSize({width, v->Size.y * 0.82F});
        if (ImGui::Begin("Main Menu", nullptr, menuFlags | ImGuiWindowFlags_NoBackground)) {
            ImGui::PushFont(menuFont_, std::min(58.0F, width / (5.8F * s)));
            ImGui::TextColored(presentation::menuColor(presentation::ivory), "GRIMLOCK");
            ImGui::PopFont();
            const auto p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(p,
                                                {p.x + std::min(260 * s, width - 32 * s), p.y},
                                                presentation::menuInk(presentation::menuGold),
                                                1);
            ImGui::Dummy({0, 12 * s});
            ImGui::PushFont(menuFont_, 15);
            ImGui::TextWrapped("A Tactical Tower Defense Adventure");
            ImGui::PopFont();
            ImGui::Dummy({0, std::clamp(v->Size.y * 0.07F, 12.0F, 50.0F)});
            const float button = std::min(54 * s, v->Size.y * 0.09F);
            if (presentation::menuAction("START GAME", {-1, button}, true)) {
                refreshLevels();
                const auto level = std::find_if(catalog_.levels.begin(), catalog_.levels.end(), [](const auto& item) {
                    return item.path.filename() == "forest_defense.json";
                });
                if (level != catalog_.levels.end()) {
                    requestedLevel_ = level->path;
                } else {
                    error_ = "Level 1 is unavailable.";
                    requestedScreen_ = Screen::levels;
                }
            }
            if (presentation::menuAction("LEVELS###Choose Level", {-1, button})) {
                requestedScreen_ = Screen::levels;
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
        ImGui::SetNextWindowPos({v->Pos.x + v->Size.x * 0.5F, v->Pos.y + v->Size.y * 0.5F},
                                ImGuiCond_Always,
                                {0.5F, 0.5F});
        ImGui::SetNextWindowSize({std::min(1100 * s, v->Size.x - 32 * s), v->Size.y - 32 * s});
        if (ImGui::Begin("Level Selection", nullptr, menuFlags | ImGuiWindowFlags_NoBackground)) {
            ImGui::PushFont(menuFont_, 32);
            ImGui::TextColored(presentation::menuColor(presentation::ivory), "SELECT LEVEL");
            ImGui::PopFont();
            ImGui::TextDisabled("Choose where your watch begins.");
            ImGui::Dummy({0, 12 * s});
            refreshRemaining_ -= ImGui::GetIO().DeltaTime;
            if (refreshRemaining_ <= 0) {
                refreshLevels();
            }
            const float footer = 94 * s;
            if (ImGui::BeginChild("Available Levels", {0, -footer})) {
                const auto available = ImGui::GetContentRegionAvail();
                const float padding = 6 * s;
                const float gap = 20 * s;
                const float captionHeight = 72 * s;
                const float preferredWidth = (catalog_.levels.size() == 1 ? 420 : 300) * s;
                const float heightWidth = std::max(80.0F, (available.y - 2 * padding - captionHeight) * 16 / 9);
                const float cardWidth =
                    std::max(1.0F, std::min({preferredWidth, available.x - 2 * padding, heightWidth}));
                const float cardHeight = cardWidth * 9 / 16 + captionHeight;
                const int columns =
                    std::max(1,
                             std::min(static_cast<int>(catalog_.levels.size()),
                                      static_cast<int>((available.x - 2 * padding + gap) / (cardWidth + gap))));
                auto start = ImGui::GetCursorPos();
                start.x += std::max(0.0F, (available.x - columns * cardWidth - (columns - 1) * gap) * 0.5F);
                start.y += padding;
                if (catalog_.levels.size() == 1) {
                    start.y += std::max(0.0F, (available.y - cardHeight - 2 * padding) * 0.5F);
                }
                std::size_t index = 0;
                for (const auto& level : catalog_.levels) {
                    ImGui::SetCursorPos({start.x + (index % columns) * (cardWidth + gap),
                                         start.y + (index / columns) * (cardHeight + gap)});
                    ++index;
                    ImGui::PushID(level.path.generic_string().c_str());
                    const bool selected = selectedLevel_ == level.path;
                    // The card owns all its visuals; a Selectable would draw a second label/highlight underneath.
                    const bool clicked =
                        ImGui::InvisibleButton(level.name.c_str(), {cardWidth, cardHeight}, ImGuiButtonFlags_EnableNav);
                    const bool doubleClicked =
                        ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                    if (clicked || doubleClicked) {
                        selectedLevel_ = level.path;
                        error_.clear();
                        if (doubleClicked) {
                            requestedLevel_ = level.path;
                        }
                    }
                    const auto a = ImGui::GetItemRectMin();
                    const ImVec2 b{a.x + cardWidth, a.y + cardHeight};
                    auto* storage = ImGui::GetStateStorage();
                    const auto id = ImGui::GetID("hover");
                    float hover = storage->GetFloat(id);
                    const bool active = ImGui::IsItemHovered() || ImGui::IsItemFocused();
                    hover += (static_cast<float>(active) - hover) * (1 - std::exp(-12 * ImGui::GetIO().DeltaTime));
                    storage->SetFloat(id, hover);
                    const float growth = 2 * s * hover;
                    auto* draw = ImGui::GetWindowDrawList();
                    draw->AddRectFilled({a.x - growth, a.y - growth},
                                        {b.x + growth, b.y + growth},
                                        presentation::menuInk(presentation::panel),
                                        6);
                    auto preview = engine_.assetRoot() / "previews" / level.path.stem();
                    preview += ".png";
                    if (!std::filesystem::exists(preview)) {
                        preview = engine_.assetRoot() / "ui/menu_background.png";
                    }
                    const ImVec2 imageEnd{b.x, a.y + cardWidth * 9 / 16};
                    imageCover(draw, menuImage(preview), a, imageEnd);
                    draw->AddRect(
                        {a.x - growth, a.y - growth},
                        {b.x + growth, b.y + growth},
                        presentation::menuInk(selected || active ? presentation::menuGold : presentation::stone),
                        6.0F);
                    const float textWidth = std::max(1.0F, cardWidth - 24 * s);
                    float nameSize = (cardWidth < 260 * s ? 18 : 21) * s;
                    ImVec2 nameBounds;
                    do {
                        ImGui::PushFont(menuFont_, nameSize / s);
                        nameBounds = ImGui::CalcTextSize(level.name.c_str(), nullptr, false, textWidth);
                        ImGui::PopFont();
                        if (nameBounds.y <= 2 * nameSize + 1 || nameSize <= 12 * s) {
                            break;
                        }
                        nameSize -= s;
                    } while (true);
                    const float nameHeight = std::min(nameBounds.y, 2 * nameSize);
                    draw->PushClipRect({a.x + 8 * s, imageEnd.y}, {b.x - 8 * s, b.y - 4 * s}, true);
                    draw->PushClipRect({a.x + 8 * s, imageEnd.y + 10 * s},
                                       {b.x - 8 * s, imageEnd.y + 10 * s + nameHeight},
                                       true);
                    draw->AddText(menuFont_ ? menuFont_ : ImGui::GetFont(),
                                  nameSize,
                                  {a.x + 12 * s, imageEnd.y + 10 * s},
                                  presentation::menuInk(presentation::ivory),
                                  level.name.c_str(),
                                  nullptr,
                                  textWidth);
                    draw->PopClipRect();
                    const auto* record = runtime_.progress() ? runtime_.progress()->find(level.id) : nullptr;
                    const char* state = record && record->completed  ? "Completed"
                                        : record && record->attempts ? "Your watch continues"
                                                                     : "Unexplored";
                    const float stateSize =
                        std::min(15 * s,
                                 textWidth / std::max(ImGui::CalcTextSize(state).x, 1.0F) * ImGui::GetFontSize());
                    draw->AddText(menuFont_ ? menuFont_ : ImGui::GetFont(),
                                  stateSize,
                                  {a.x + 12 * s, imageEnd.y + 16 * s + nameHeight},
                                  presentation::menuInk(presentation::secondaryText),
                                  state);
                    draw->PopClipRect();
                    if (ImGui::IsItemHovered() && record) {
                        ImGui::BeginTooltip();
                        ImGui::Text("Attempts %llu | Victories %llu",
                                    static_cast<unsigned long long>(record->attempts),
                                    static_cast<unsigned long long>(record->victories));
                        if (record->bestSeconds) {
                            ImGui::Text("Best time %.2f s", *record->bestSeconds);
                        }
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
                if (catalog_.levels.empty()) {
                    ImGui::TextWrapped("No playable levels available.");
                }
            }
            ImGui::EndChild();
            ImGui::BeginDisabled(!selectedLevel_);
            if (presentation::menuAction("PLAY###Play", {160 * s, 42 * s}, true)) {
                requestedLevel_ = selectedLevel_;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (presentation::menuAction("REFRESH###Refresh", {125 * s, 42 * s})) {
                refreshLevels();
            }
            ImGui::SameLine();
            if (presentation::menuAction("BACK###Main Menu", {125 * s, 42 * s})) {
                requestedScreen_ = Screen::mainMenu;
            }
            if (!error_.empty()) {
                ImGui::TextColored(presentation::menuColor(presentation::danger), "%s", error_.c_str());
            }
            progressNotice();
        }
        ImGui::End();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopFont();
}

void GameHud::pauseMenu() {
    presentation::MenuTheme theme;
    ImGui::PushFont(menuFont_, 20);
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        viewport->Pos,
        {viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y},
        presentation::menuInk({23 / 255.0F, 43 / 255.0F, 38 / 255.0F, 0.72F}));
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
