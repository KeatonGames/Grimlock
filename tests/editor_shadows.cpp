#include "editor.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>

using namespace grimlock;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void tick(Engine& engine, int frames = 2) {
    for (int i = 0; i < frames; ++i) {
        require(engine.update(), "Editor shadow window closed");
    }
}

void click(Engine& engine, SDL_WindowID window, const char* label) {
    auto* inspector = ImGui::FindWindowByName("Inspector");
    require(inspector, "Inspector missing");
    const auto id = ImHashStr(label, 0, inspector->ID);
    for (float y = inspector->DC.CursorStartPos.y + 3; y < inspector->InnerClipRect.Max.y; y += 3) {
        const float x = inspector->DC.CursorStartPos.x + 30;
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = window;
        event.motion.x = x;
        event.motion.y = y;
        SDL_PushEvent(&event);
        tick(engine, 1);
        if (ImGui::GetHoveredID() != id) {
            continue;
        }
        event = {};
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.windowID = window;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = x;
        event.button.y = y;
        SDL_PushEvent(&event);
        tick(engine, 1);
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        SDL_PushEvent(&event);
        tick(engine, 3);
        return;
    }
    throw std::runtime_error(std::string{"Missing shadow control: "} + label);
}

void choose(Engine& engine, SDL_WindowID window, const char* label, int index) {
    click(engine, window, label);
    const auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
    require(!popups.empty() && popups.back().Window, "Rendering setting popup missing");
    const auto* popup = popups.back().Window;
    const float x = popup->DC.CursorStartPos.x + 30;
    const float y = popup->DC.CursorStartPos.y + (index + 0.5F) * ImGui::GetTextLineHeightWithSpacing();
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = window;
    event.motion.x = x;
    event.motion.y = y;
    SDL_PushEvent(&event);
    tick(engine);
    event = {};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.windowID = window;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    SDL_PushEvent(&event);
    tick(engine);
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    SDL_PushEvent(&event);
    tick(engine);
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected shadow editor output directory");
        Engine engine{EngineConfig{.windowTitle = "Editor shadow controls", .width = 1280, .height = 900}};
        const std::filesystem::path output{argv[1]};
        std::filesystem::create_directories(output);
        {
            editor::Editor editor{engine, engine.assetRoot() / "scenes/forest_defense.json"};
            tick(engine, 5);
            int count = 0;
            auto** windows = SDL_GetWindows(&count);
            require(windows && count == 1, "Editor shadow window missing");
            const auto window = SDL_GetWindowID(windows[0]);
            SDL_free(windows);
            const auto originalGraphics = engine.renderSettings();
            click(engine, window, "Rendering Settings");
            choose(engine, window, "Texture filtering", 0);
            require(engine.renderSettings().textureFiltering == TextureFiltering::bilinear,
                    "Editor bilinear preview did not apply");
            choose(engine, window, "Texture filtering", 1);
            require(engine.renderSettings().textureFiltering == TextureFiltering::trilinear,
                    "Editor trilinear preview did not apply");
            choose(engine, window, "Tone mapping", 0);
            require(engine.renderSettings().toneMapping == ToneMapping::linear,
                    "Editor linear tone preview did not apply");
            choose(engine, window, "Tone mapping", 1);
            require(engine.renderSettings().toneMapping == ToneMapping::reinhard,
                    "Editor Reinhard preview did not apply");
            choose(engine, window, "Tone mapping", 2);
            const auto supported = engine.supportedAnisotropy();
            if (supported.back() > 1) {
                choose(engine, window, "Texture filtering", 2);
                choose(engine, window, "Anisotropy", static_cast<int>(supported.size()) - 2);
                require(engine.renderSettings().textureFiltering == TextureFiltering::anisotropic &&
                            engine.renderSettings().anisotropy == supported.back(),
                        "Editor anisotropic preview did not apply");
            }
            require(engine.renderSettings().toneMapping == ToneMapping::filmic, "Editor Filmic preview did not apply");
            engine.setRenderSettings(originalGraphics);
            click(engine, window, "Rendering Settings");
            click(engine, window, "Lighting & Shadows");
            click(engine, window, "Sun enabled");
            require(!engine.directionalLight().enabled && !engine.shadowDebugInfo().active,
                    "Sun toggle did not disable shadow work");
            click(engine, window, "Sun enabled");
            require(engine.directionalLight().enabled && engine.shadowDebugInfo().active,
                    "Sun toggle did not restore shadows");
            if (engine.shadowDebugInfo().readbackAvailable) {
                click(engine, window, "Capture shadow map");
                const auto resolution = engine.shadowDebugInfo().resolution;
                const auto hasSnapshot = [&] {
                    for (const auto* texture : ImGui::GetPlatformIO().Textures) {
                        if (texture->Width == static_cast<int>(resolution) &&
                            texture->Height == static_cast<int>(resolution) && texture->BackendUserData) {
                            return true;
                        }
                    }
                    return false;
                };
                require(hasSnapshot(), "Editor did not upload its Engine shadow-map snapshot");
                click(engine, window, "Capture shadow map");
                require(hasSnapshot(), "Refreshing the shadow-map snapshot lost its texture");
                for (auto samples : engine.supportedMsaaSamples()) {
                    engine.setMsaaSamples(samples);
                    tick(engine, 3);
                    require(hasSnapshot(), "MSAA change invalidated Editor shadow preview");
                }
            }
            auto& scene = *engine.scene();
            ObjectId model = 0;
            for (const auto& [id, object] : scene.objects()) {
                if (object.model) {
                    model = id;
                    break;
                }
            }
            require(model != 0, "Shadow serialization fixture missing");
            scene.setShadowProperties(model, {false, false});
            editor.document().markChanged();
            const auto duplicate = editor.document().duplicate(model);
            require(!scene.object(duplicate).shadows.cast && !scene.object(duplicate).shadows.receive,
                    "Duplicate lost shadow properties");
            auto sun = engine.directionalLight();
            sun.enabled = false;
            engine.setDirectionalLight(sun);
            editor.document().save(output / "shadows.json");
            const auto loaded = Scene::load(output / "shadows.json");
            require(!loaded->object(model).shadows.cast && !loaded->object(model).shadows.receive &&
                        !loaded->lighting().values.directional.enabled,
                    "Scene roundtrip lost sun or shadow properties");
        }
        require(ImGui::GetCurrentContext() == nullptr, "Shadow preview retained Editor context");
        std::cout
            << "PASS Editor sun/shadow controls, depth snapshot refresh, MSAA, flags, serialization and cleanup.\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL Editor shadows: " << error.what() << '\n';
        return 1;
    }
}
