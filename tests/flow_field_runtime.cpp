#include "editor.hpp"
#include "navigation/navigation_debug.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
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

void tick(Engine& engine, int count = 2) {
    for (int i = 0; i < count; ++i) {
        require(engine.update(), "navigation editor closed");
    }
}

void mouse(SDL_WindowID window, Uint32 type, glm::vec2 point) {
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.windowID = window;
        event.motion.x = point.x;
        event.motion.y = point.y;
    } else {
        event.button.windowID = window;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = point.x;
        event.button.y = point.y;
    }
    require(SDL_PushEvent(&event), SDL_GetError());
}

void click(Engine& engine, SDL_WindowID window, glm::vec2 point) {
    mouse(window, SDL_EVENT_MOUSE_MOTION, point);
    tick(engine);
    mouse(window, SDL_EVENT_MOUSE_MOTION, point);
    mouse(window, SDL_EVENT_MOUSE_BUTTON_DOWN, point);
    tick(engine);
    mouse(window, SDL_EVENT_MOUSE_MOTION, point);
    mouse(window, SDL_EVENT_MOUSE_BUTTON_UP, point);
    tick(engine);
}

void navigationControl(Engine& engine, SDL_WindowID window, const char* label) {
    std::cout << "Control: " << label << std::endl;
    click(engine, window, {130, 11});
    auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
    require(!popups.empty() && popups.back().Window, "Navigation menu missing");
    const auto* popup = popups.back().Window;
    const auto control = ImHashStr(label, 0, popup->ID);
    bool found = false;
    for (float y = popup->DC.CursorStartPos.y + 4; y < popup->ClipRect.Max.y; y += 4) {
        for (const float x : {60.0F, 230.0F}) {
            const glm::vec2 point{popup->DC.CursorStartPos.x + x, y};
            mouse(window, SDL_EVENT_MOUSE_MOTION, point);
            tick(engine, 1);
            if (ImGui::GetHoveredID() == control) {
                click(engine, window, point);
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
    }
    require(found, label);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    tick(engine);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    tick(engine);
    // Escape leaves keyboard focus on the menu bar; return to the viewport before reopening it.
    click(engine, window, {700, 680});
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected Game source and output scene paths");
        const std::filesystem::path game{argv[1]}, output{argv[2]};
        Engine engine{EngineConfig{.windowTitle = "Flow Field runtime checks", .assetRoot = game / "assets"}};
        game::NavigationDebug debug;
        editor::Editor editor{engine, game / "assets/scenes/flow_field.json", &debug};
        tick(engine, 4);
        auto* scene = engine.scene();
        auto* grid = scene->navigationGrid();
        const auto objectCount = scene->objects().size();
        require(grid && objectCount == 4, "corridor scenario missing");
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        require(windows && count == 1, "navigation window missing");
        auto* window = windows[0];
        const auto id = SDL_GetWindowID(window);
        SDL_free(windows);
        SDL_Event focus{};
        focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        focus.window.windowID = id;
        require(SDL_PushEvent(&focus), SDL_GetError());
        tick(engine);

        std::cout << "Checking direction and cost controls..." << std::endl;
        const auto arrows = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport())->VtxBuffer.Size;
        navigationControl(engine, id, "Show Flow Directions");
        require(!debug.view(engine).showDirections &&
                    ImGui::GetBackgroundDrawList(ImGui::GetMainViewport())->VtxBuffer.Size < arrows,
                "direction toggle did not remove arrows");
        navigationControl(engine, id, "Show Integration Costs");
        require(debug.view(engine).showCosts, "cost toggle failed");
        navigationControl(engine, id, "Show Flow Directions");
        std::cout << "Checking test agent spawn..." << std::endl;
        navigationControl(engine, id, "Spawn Test Agents");
        require(debug.demo().agents().size() == 6 && scene->objects().size() == objectCount &&
                    !editor.document().dirty(),
                "spawn failed or added serialized/dirty scene objects");
        const auto generation = debug.demo().field().generation();
        const auto animationTime = scene->animation(4).time();
        std::cout << "Checking shared agent movement..." << std::endl;
        for (int frame = 0; frame < 32; ++frame) {
            require(engine.update([&](double) {
                debug.update(engine, 2);
            }),
                    "movement frame failed");
            for (const auto& agent : debug.demo().agents()) {
                require(game::segmentWalkable(*grid,
                                              agent.movement.position,
                                              agent.movement.position,
                                              agent.movement.radius),
                        "runtime agent entered a blocked cell or wall");
            }
        }
        for (const auto& agent : debug.demo().agents()) {
            require(agent.movement.status == game::AgentStatus::arrived, "runtime agent did not arrive");
        }
        require(debug.demo().field().generation() == generation && scene->animation(4).time() > animationTime,
                "shared field rebuilt every frame or existing animation stopped");

        std::cout << "Checking live wall and destination edits..." << std::endl;
        editor.document().setGridWalkable(*debug.demo().destination(), false);
        require(engine.update([&](double) {
            debug.update(engine, 1);
        }),
                "edited grid frame failed");
        require(debug.demo().field().generation() == generation + 1 &&
                    debug.demo().agents()[0].movement.status == game::AgentStatus::blocked,
                "edited destination did not synchronize navigation and wall rendering");
        editor.document().setGridWalkable(*debug.demo().destination(), true);
        debug.demo().setDestination(GridCell{0, 0});
        require(engine.update([&](double) {
            debug.update(engine, 0.1);
        }),
                "destination update failed");
        require(debug.view(engine).destination == GridCell{0, 0} && debug.demo().field().cost({0, 0}) == 0,
                "destination marker and integration field diverged");
        engine.saveScene(output);
        require(Scene::load(output)->objects().size() == objectCount, "test agents leaked into scene serialization");
        std::cout << "Checking test agent reset..." << std::endl;
        navigationControl(engine, id, "Reset Test Agents");
        require(debug.demo().agents().empty(), "reset left active runtime instances");
        require(engine.update([&](double) {
            debug.update(engine, 0);
        }),
                "reset frame failed");

        std::cout << "Checking scene replacement..." << std::endl;
        debug.demo().spawn(engine);
        editor.document().open(game / "assets/scenes/navigation.json");
        require(engine.update([&](double) {
            debug.update(engine, 0);
        }),
                "scene replacement frame failed");
        require(debug.demo().agents().empty(), "scene replacement kept stale runtime agents");
        std::cout
            << "PASS navigation editor controls, shared runtime instances, walls, destination, reset and save/load.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL navigation runtime: " << error.what() << '\n';
        return 1;
    }
}
