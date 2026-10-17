#include <grimlock/engine.hpp>
#include <grimlock/scene.hpp>
#include <SDL3/SDL.h>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace grimlock;
namespace {
void require(bool value,const char* text) { if(!value){throw std::runtime_error(text);} }
bool near(glm::vec3 a,glm::vec3 b) { return glm::length(a-b)<1e-4F; }
void push(SDL_Event event) { require(SDL_PushEvent(&event),SDL_GetError()); }
void focus(SDL_WindowID id,bool gained) { SDL_Event e{};e.type=gained?SDL_EVENT_WINDOW_FOCUS_GAINED:SDL_EVENT_WINDOW_FOCUS_LOST;e.window.windowID=id;push(e); }
void button(SDL_WindowID id,Uint8 value,bool down) {
    SDL_Event e{};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;e.button.windowID=id;e.button.button=value;e.button.x=200;e.button.y=150;push(e);
}
void motion(SDL_WindowID id,float x,float y) { SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.windowID=id;e.motion.x=200+x;e.motion.y=150+y;e.motion.xrel=x;e.motion.yrel=y;push(e); }
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"expected output file");Engine engine{EngineConfig{.windowTitle="Navigation runtime checks",.width=640,.height=480}};
        auto scene=std::make_unique<Scene>();const auto parent=scene->createObject();
        scene->setTransform(parent,Transform{{1,0,2},glm::angleAxis(0.3F,glm::vec3{0,1,0}),{2,1.5F,0.7F}});
        SceneCamera attached;attached.object=parent;scene->setCamera("attached",attached);scene->setActiveCamera("attached");
        engine.setScene(std::move(scene));engine.setCameraNavigationEnabled();require(engine.update(),"initial update closed");
        int count=0;auto windows=SDL_GetWindows(&count);require(windows&&count==1,"expected Engine window");auto* window=windows[0];SDL_free(windows);
        const auto id=SDL_GetWindowID(window);focus(id,true);require(engine.update(),"focus update closed");
        const auto original=engine.scene()->resolvedCamera();button(id,SDL_BUTTON_RIGHT,true);motion(id,900,800);require(engine.update(),"press update closed");
        require(near(engine.scene()->resolvedCamera().position,original.position),"right press jumped");
        require(SDL_GetWindowRelativeMouseMode(window),"right drag did not acquire relative mode");
        motion(id,30,-20);require(engine.update(),"pan update closed");const auto pan=engine.scene()->resolvedCamera();
        require(!near(pan.position,original.position)&&near(pan.position-pan.target,original.position-original.target),"Engine pan failed");
        require(std::abs(pan.position.y-original.position.y)<1e-4F,"pan changed height");
        button(id,SDL_BUTTON_RIGHT,false);require(engine.update(),"release update closed");require(!SDL_GetWindowRelativeMouseMode(window),"release retained relative mode");
        button(id,SDL_BUTTON_MIDDLE,true);require(engine.update(),"middle press update closed");motion(id,35,20);require(engine.update(),"orbit update closed");
        const auto orbit=engine.scene()->resolvedCamera();require(!near(orbit.position,pan.position)&&near(orbit.target,pan.target),"Engine orbit failed");
        button(id,SDL_BUTTON_MIDDLE,false);require(engine.update(),"middle release update closed");
        SDL_Event e{};e.type=SDL_EVENT_MOUSE_WHEEL;e.wheel.windowID=id;e.wheel.y=1;push(e);require(engine.update(),"wheel update closed");
        auto zoom=engine.scene()->resolvedCamera();require(glm::length(zoom.position-zoom.target)<glm::length(orbit.position-orbit.target),"Engine zoom failed");
        require(engine.scene()->cameras().at("attached").object==parent,"navigation detached camera");
        e={};e.type=SDL_EVENT_KEY_DOWN;e.key.windowID=id;e.key.scancode=SDL_SCANCODE_W;push(e);button(id,SDL_BUTTON_LEFT,true);motion(id,100,100);
        require(engine.update(),"unmapped input update closed");require(near(engine.scene()->resolvedCamera().position,zoom.position),"keyboard/left drag moved camera");
        require(engine.input().key(SDL_SCANCODE_W).held&&engine.input().button(MouseButton::left).held,"unmapped state was not tracked");
        button(id,SDL_BUTTON_LEFT,false);button(id,SDL_BUTTON_RIGHT,true);require(engine.update(),"focus setup closed");
        focus(id,false);motion(id,1000,1000);require(engine.update(),"focus loss closed");
        require(!engine.input().focused&&!engine.input().button(MouseButton::right).held&&!engine.input().key(SDL_SCANCODE_W).held&&!SDL_GetWindowRelativeMouseMode(window),"focus loss stuck input/capture");
        focus(id,true);motion(id,1000,1000);require(engine.update(),"focus regain closed");
        require(near(engine.scene()->resolvedCamera().position,zoom.position),"focus regain jumped");
        button(id,SDL_BUTTON_MIDDLE,true);require(engine.update(),"resize setup closed");
        e={};e.type=SDL_EVENT_WINDOW_RESIZED;e.window.windowID=id;push(e);require(engine.update(),"resize closed");
        require(!engine.input().button(MouseButton::middle).held&&!SDL_GetWindowRelativeMouseMode(window),"resize retained drag");
        engine.saveScene(std::filesystem::path{argv[1]});auto saved=Scene::load(std::filesystem::path{argv[1]});
        require(near(saved->resolvedCamera().position,engine.scene()->resolvedCamera().position)&&saved->cameras().at("attached").object==parent,"navigated camera save/reload failed");
        SceneCamera other;other.camera.position={6,3,5};other.camera.target={1,0,0};engine.scene()->setCamera("other",other);engine.scene()->setActiveCamera("other");
        require(engine.update(),"camera switch closed");require(near(engine.cameraNavigationConfig().target,other.camera.target),"active camera switch did not resynchronize");
        button(id,SDL_BUTTON_RIGHT,true);require(engine.update(),"disable setup closed");engine.setCameraNavigationEnabled(false);
        require(!engine.cameraNavigationEnabled()&&!SDL_GetWindowRelativeMouseMode(window),"disable retained capture");
        engine.unloadScene();engine.setScene(std::make_unique<Scene>());engine.setCameraNavigationEnabled();require(engine.update(),"replacement scene closed");
        require(near(engine.cameraNavigationConfig().target,engine.scene()->resolvedCamera().target),"scene replacement kept stale orbit target");
        std::cout<<"PASS navigation runtime: scoped SDL input, pan/orbit/zoom, unmapped keys/left button, focus/resize/disable, camera switch, attachments, save/reload.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL navigation runtime: "<<e.what()<<'\n';return 1;}
}
