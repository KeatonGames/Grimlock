#include "grimlock/engine.hpp"
#include "grimlock/scene.hpp"
#include "assets/model_loader.hpp"
#include "assets/environment.hpp"
#include "core/startup_profile.hpp"
#include "platform/window.hpp"
#include "input/input.hpp"
#include "platform/runtime_paths.hpp"
#include "renderer/vulkan_renderer.hpp"
#include "scene/picking.hpp"
#include "grimlock/render_overlay.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <stdexcept>
#include <utility>

namespace grimlock {
struct Engine::Impl {
    struct Resource {
        std::shared_ptr<const assets::ModelData> asset;
        std::size_t gpuModel = 0;
        ModelInfo info;
        bool pinned = false; // Legacy ModelId API retains its documented Engine lifetime.
        picking::Geometry picking;
    };
    explicit Impl(const EngineConfig& config)
        : window(config.windowTitle.c_str(),config.width,config.height)
        , input(window.native())
        , environment(config.environmentFolder.empty()?std::nullopt
            :std::optional{assets::loadEnvironmentCube(platform::runtimePath(config.environmentFolder))})
        , renderer(window.native(),environment?&*environment:nullptr)
        , assetRoot(std::filesystem::weakly_canonical(platform::runtimePath(config.assetRoot))) { environment.reset(); }

    std::size_t store(std::shared_ptr<const assets::ModelData> asset,const std::string& name,bool pin) {
        auto resource=std::make_unique<Resource>(); resource->asset=std::move(asset); resource->pinned=pin;
        resource->picking=picking::Geometry{*resource->asset};
        auto& info=resource->info;
        info={resource->asset->minimum,resource->asset->maximum,resource->asset->meshes.size(),resource->asset->materials.size(),{}};
        for (const auto& clip:resource->asset->animations) { info.animationNames.push_back(clip.name); }
        const auto free=std::find(resources.begin(),resources.end(),nullptr);
        const auto slot=static_cast<std::size_t>(free-resources.begin());
        if (free==resources.end()) { resources.emplace_back(); }
        resource->gpuModel=renderer.uploadModel(*resource->asset);
        if (!resource->asset->warnings.empty()) { std::clog<<"Model warning: "<<resource->asset->warnings; }
        std::cout<<"Loaded model: "<<name<<" ("<<info.primitives<<" primitives, "<<resource->asset->nodes.size()
            <<" nodes, "<<info.animationNames.size()<<" clips)"<<std::endl;
        resources[slot]=std::move(resource); return slot;
    }
    std::size_t load(const std::filesystem::path& path,bool pin) {
        const auto canonical=std::filesystem::weakly_canonical(platform::runtimePath(path));
        if (const auto found=modelPaths.find(canonical);found!=modelPaths.end()) {
            diagnostics::StartupSpan profile{"asset.resource_cache_hit"};
            auto& resource=*resources.at(found->second); resource.pinned=resource.pinned||pin; return found->second;
        }
        const auto index=store(std::make_shared<assets::ModelData>(assets::loadModel(canonical)),canonical.string(),pin);
        modelPaths.emplace(canonical,index); return index;
    }
    Resource& resourceFor(const assets::ModelInstance& instance) {
        for (const auto& value:resources) { if (value&&value->asset==instance.asset()) { return *value; } }
        throw std::logic_error("Scene model belongs to a different Engine; reload its scene definition before activation.");
    }
    void prepare(Scene& scene) {
        diagnostics::StartupSpan profile{"scene.bind_resources"};
        scene.updateTransforms(); (void)scene.resolvedCamera(); (void)scene.resolvedLighting();
        for (const auto& [id,object]:scene.objects_) {
            if (!object.model) { continue; }
            try {
                // No filesystem or asset work on unchanged frames.
                if (object.instance_) { (void)resourceFor(*object.instance_); continue; }
                const auto path=std::filesystem::weakly_canonical(assetRoot/ *object.model);
                const auto relative=path.lexically_relative(assetRoot);
                if (relative.empty()||relative.is_absolute()||*relative.begin()=="..") { throw std::invalid_argument("Model resolves outside the configured asset root."); }
                const auto model=load(path,false); scene.bindModel(id,resources[model]->asset);
            } catch (const std::exception& e) {
                const auto origin=scene.sourcePath_.empty()?scene.name():scene.sourcePath_.string();
                throw std::runtime_error("Scene '"+origin+"', object "+std::to_string(id)+" ('"+object.name+"'), model '"
                    +object.model->string()+"', fields model/animation/material_override: "+e.what());
            }
        }
        std::size_t primitiveDraws=0,joints=0,weights=0;
        for (const auto& [id,object]:scene.objects_) {
            (void)id;
            if (!object.instance_||!object.visible_) { continue; }
            const auto& pose=object.instance_->pose();
            for (const auto node:pose.activeNodes) {
                const auto count=object.instance_->model().nodes[node].meshes.size();
                primitiveDraws+=count; joints+=count*pose.joints[node].size(); weights+=count*pose.weights[node].size();
                if (primitiveDraws>4096||joints>65536||weights>65536) { throw std::runtime_error("Scene exceeds existing frame limits: 4096 primitive draws, 65536 joint matrices or morph weights."); }
            }
        }
        (void)sceneLighting(scene); // Reject combined scene/imported-light overflow before activation.
    }
    Lighting sceneLighting(const Scene& scene) {
        auto result=scene.resolvedLighting();
        bool directionalUsed=result.directional.intensity>0;
        for (const auto& [id,object]:scene.objects_) {
            (void)id;
            if (object.instance_&&object.visible_) {
                renderer::addModelLights(result,object.instance_->model(),object.instance_->pose(),object.world_,directionalUsed);
            }
        }
        validateLighting(result); return result;
    }
    void collectUnused() {
        std::vector<std::size_t> gpu;
        for (const auto& value:resources) {
            if (value&&!value->pinned&&value->asset.use_count()==1) { gpu.push_back(value->gpuModel); }
        }
        if (gpu.empty()) { return; }
        renderer.releaseModels(gpu);
        for (auto& value:resources) { if (value&&!value->pinned&&value->asset.use_count()==1) { value.reset(); } }
        std::erase_if(modelPaths,[&](const auto& entry){return !resources.at(entry.second);});
    }
    void sceneFrame(double delta,Camera& frameCamera,Lighting& frameLighting) {
        prepare(*activeScene); activeScene->updateAnimations(delta);
        sceneDraws.clear(); sceneDraws.reserve(activeScene->objects_.size());
        frameCamera=activeScene->resolvedCamera(); frameLighting=sceneLighting(*activeScene);
        for (const auto& [id,object]:activeScene->objects_) {
            if (!object.instance_||!object.visible_) { continue; }
            const auto& resource=resourceFor(*object.instance_);
            renderer::ModelDraw draw{resource.gpuModel,object.world_,object.tint,object.materialOverride,&object.instance_->pose()};
            sceneDraws.push_back(draw);
        }
    }
    Camera currentCamera() const { return activeScene?activeScene->resolvedCamera():camera; }
    void updateSelection(const Camera& view) {
        if (!activeScene) { return; }
        const auto& state=input.state();
        const auto navigationButton=[&](MouseButton button) {
            const auto& value=state.button(button); return value.held||value.pressed||value.released;
        };
        if (selectionEnabled&&state.focused&&state.button(MouseButton::left).pressed&&state.leftPressPosition
            &&!navigationButton(MouseButton::right)&&!navigationButton(MouseButton::middle)) {
            glm::ivec2 windowSize{},drawableSize{};
            SDL_GetWindowSize(window.native(),&windowSize.x,&windowSize.y);
            SDL_GetWindowSizeInPixels(window.native(),&drawableSize.x,&drawableSize.y);
            if (const auto ray=picking::mouseRay(view,*state.leftPressPosition,windowSize,drawableSize)) {
                std::optional<ObjectId> selected;
                double nearest=ray->maximum;
                for (const auto& [id,object]:activeScene->objects_) {
                    if (!object.instance_||!object.visible_||!object.selectable) { continue; }
                    auto& resource=resourceFor(*object.instance_);
                    if (const auto hit=resource.picking.intersect(*ray,*object.instance_,object.world_,object.materialOverride,nearest,object.tint.a)) {
                        nearest=*hit; selected=id;
                    }
                }
                activeScene->selectObject(selected);
            }
        }
        const auto selected=activeScene->selectedObject();
        std::size_t draw=0;
        for (const auto& [id,object]:activeScene->objects_) {
            if (object.instance_&&object.visible_) { sceneDraws.at(draw++).selected=selected==id; }
        }
    }
    void storeCamera(Camera& view) {
        if (activeScene) { activeScene->setResolvedCamera(view); view=activeScene->resolvedCamera(); }
        else { camera=view; }
        lastNavigationCamera=view;
    }
    void resetNavigation() {
        input.cancelNavigation(); navigation.cancelDrag(); navigationSynchronized=false;
    }
    void navigate(Camera& view) {
        if (!navigationEnabled) { return; }
        const auto id=activeScene?activeScene->activeCamera():std::string{};
        const bool synchronize=!navigationSynchronized||id!=navigationCameraId||view.position!=lastNavigationCamera.position
            ||view.target!=lastNavigationCamera.target||view.up!=lastNavigationCamera.up;
        if (synchronize) {
            resetNavigation(); navigation.synchronize(view); navigationCameraId=id; navigationSynchronized=true;
        }
        int width=0,height=0; SDL_GetWindowSize(window.native(),&width,&height);
        const bool changed=navigation.update(view,input.state(),static_cast<float>(height));
        if (synchronize||changed) { storeCamera(view); }
        else { lastNavigationCamera=view; }
    }
    bool update() {
        input.beginFrame();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (overlay) { overlay->processEvent(event); }
            input.handleEvent(event);
            switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (!overlay||overlay->requestClose()) { running=false; }
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_RESTORED: renderer.requestSwapchainRecreation(); break;
            default: break;
            }
        }
        if (!running) { return false; }
        const auto capture=overlay?overlay->frame():OverlayInput{};
        if (capture.close) { running=false; return false; }
        input.capture(capture.mouse,capture.keyboard);
        if (capture.mouse) { navigation.cancelDrag(); }
        input.finishFrame(navigationEnabled&&!capture.mouse&&running);
        const auto now=std::chrono::steady_clock::now();
        const double delta=previousFrame?std::chrono::duration<double>(now-*previousFrame).count():0;
        previousFrame=now;
        auto frameCamera=camera; auto frameLighting=lighting;
        std::span<const renderer::ModelDraw> frameDraws=draws;
        if (activeScene) { sceneFrame(delta,frameCamera,frameLighting); frameDraws=sceneDraws; }
        else {
            for (const auto& instance:instances) { instance->update(delta); }
            bool imported=false;
            for (const auto& instance:instances) {
                for (const auto node:instance->pose().activeNodes) { imported=imported||instance->model().nodes[node].light.has_value(); }
            }
            if (imported) {
                frameLighting.directional.intensity=0; frameLighting.pointCount=frameLighting.spotCount=0;
                bool directionalUsed=false;
                for (std::size_t i=0;i<instances.size();++i) { renderer::addModelLights(frameLighting,instances[i]->model(),instances[i]->pose(),draws[i].transform,directionalUsed); }
            }
        }
        navigate(frameCamera); updateSelection(frameCamera); collectUnused();
        if (renderer.drawFrame(frameCamera,frameDraws,frameLighting)) { startup.firstPresent(); }
        else { SDL_Delay(16); }
        return true;
    }
    // Reverse destruction: scene/instances/CPU assets -> renderer/device -> SDL.
    diagnostics::StartupProfile startup;
    platform::Window window;
    platform::Input input;
    std::optional<assets::EnvironmentCube> environment;
    renderer::VulkanRenderer renderer;
    std::filesystem::path assetRoot;
    Camera camera;
    Lighting lighting;
    OrbitCameraController navigation;
    Camera lastNavigationCamera;
    std::string navigationCameraId;
    bool navigationEnabled=false,navigationSynchronized=false;
    bool selectionEnabled=false;
    std::vector<std::unique_ptr<Resource>> resources;
    std::map<std::filesystem::path,std::size_t> modelPaths;
    std::vector<std::unique_ptr<assets::ModelInstance>> instances;
    std::vector<renderer::ModelDraw> draws,sceneDraws;
    std::unique_ptr<Scene> activeScene;
    std::optional<std::chrono::steady_clock::time_point> previousFrame;
    bool running=true;
    RenderOverlay* overlay=nullptr;
};
Engine::Engine(const EngineConfig& config) : impl_(std::make_unique<Impl>(config)) {}
Engine::~Engine()=default;
ModelId Engine::loadModel(const std::filesystem::path& path) { return ModelId{impl_->load(path,true)}; }
ModelId Engine::addModel(assets::ModelData model) {
    assets::updateModelBounds(model); const auto name=model.meshes.empty()?"generated model":model.meshes.front().name;
    return ModelId{impl_->store(std::make_shared<assets::ModelData>(std::move(model)),name,true)};
}
ModelInfo Engine::modelInfo(ModelId model) const { return impl_->resources.at(model.index_)->info; }
InstanceId Engine::createInstance(ModelId model) {
    const auto& resource=*impl_->resources.at(model.index_);
    auto instance=std::make_unique<assets::ModelInstance>(resource.asset);
    renderer::ModelDraw draw{resource.gpuModel,glm::mat4{1}}; draw.pose=&instance->pose();
    const auto size=impl_->instances.size()+1;
    if (impl_->instances.capacity()<size) { impl_->instances.reserve(size*2); }
    if (impl_->draws.capacity()<size) { impl_->draws.reserve(size*2); }
    impl_->instances.push_back(std::move(instance)); impl_->draws.push_back(draw); return InstanceId{size-1};
}
void Engine::setTransform(InstanceId instance,const glm::mat4& transform) { impl_->draws.at(instance.index_).transform=transform; }
void Engine::setTransform(InstanceId instance,const Transform& transform) { setTransform(instance,transform.matrix()); }
glm::mat4 Engine::transform(InstanceId instance) const { return impl_->draws.at(instance.index_).transform; }
void Engine::setTint(InstanceId instance,const glm::vec4& color) { impl_->draws.at(instance.index_).baseColor=color; }
void Engine::setMaterialOverride(InstanceId instance,std::optional<std::size_t> material) { impl_->draws.at(instance.index_).materialOverride=material; }
assets::ModelInstance& Engine::animation(InstanceId instance) { return *impl_->instances.at(instance.index_); }
Camera& Engine::camera() noexcept { return impl_->camera; }
Lighting& Engine::lighting() noexcept { return impl_->lighting; }
std::unique_ptr<Scene> Engine::loadScene(const std::filesystem::path& path) const { return Scene::load(platform::runtimePath(path)); }
void Engine::saveScene(const std::filesystem::path& path) const {
    if (!impl_->activeScene) { throw std::logic_error("No active scene to save."); }
    impl_->activeScene->save(platform::runtimePath(path));
}
void Engine::setScene(std::unique_ptr<Scene> scene) {
    if (!scene) { unloadScene(); return; }
    try {
        impl_->prepare(*scene);
        if (impl_->navigationEnabled) {
            auto controller=impl_->navigation; auto view=scene->resolvedCamera(); controller.synchronize(view); scene->setResolvedCamera(view);
        }
    }
    catch (...) { scene.reset(); impl_->collectUnused(); throw; }
    impl_->sceneDraws.clear(); impl_->activeScene=std::move(scene); impl_->previousFrame.reset(); impl_->resetNavigation(); impl_->collectUnused();
}
Scene* Engine::scene() noexcept { return impl_->activeScene.get(); }
const Scene* Engine::scene() const noexcept { return impl_->activeScene.get(); }
const std::filesystem::path& Engine::assetRoot() const noexcept { return impl_->assetRoot; }
void Engine::prepareScene(Scene& scene) { impl_->prepare(scene); }
void Engine::attachOverlay(RenderOverlay& overlay) {
    if (impl_->overlay) { throw std::logic_error("A render overlay is already attached."); }
    impl_->renderer.attachOverlay(overlay); impl_->overlay=&overlay; impl_->resetNavigation();
}
void Engine::detachOverlay() noexcept {
    impl_->renderer.detachOverlay(); impl_->overlay=nullptr; impl_->resetNavigation();
}
void Engine::unloadScene() {
    impl_->sceneDraws.clear(); impl_->activeScene.reset(); impl_->previousFrame.reset(); impl_->resetNavigation(); impl_->collectUnused();
}
const InputState& Engine::input() const noexcept { return impl_->input.state(); }
void Engine::setObjectSelectionEnabled(bool enabled) {
    impl_->selectionEnabled=enabled;
    if (!enabled) { clearSelection(); }
}
bool Engine::objectSelectionEnabled() const noexcept { return impl_->selectionEnabled; }
std::optional<ObjectId> Engine::selectedObject() const { return impl_->activeScene?impl_->activeScene->selectedObject():std::nullopt; }
void Engine::clearSelection() noexcept { if (impl_->activeScene) { impl_->activeScene->clearSelection(); } }
void Engine::setCameraNavigationEnabled(bool enabled) {
    if (impl_->navigationEnabled==enabled) { return; }
    if (enabled) {
        auto controller=impl_->navigation; auto view=impl_->currentCamera(); controller.synchronize(view);
        impl_->storeCamera(view); impl_->navigation=controller;
    }
    impl_->resetNavigation(); impl_->navigationEnabled=enabled;
    if (enabled) {
        impl_->navigationCameraId=impl_->activeScene?impl_->activeScene->activeCamera():std::string{};
        impl_->navigationSynchronized=true;
    }
}
bool Engine::cameraNavigationEnabled() const noexcept { return impl_->navigationEnabled; }
const OrbitCameraConfig& Engine::cameraNavigationConfig() const noexcept { return impl_->navigation.config(); }
void Engine::configureCameraNavigation(const OrbitCameraConfig& config) {
    auto controller=impl_->navigation; controller.configure(config);
    auto view=impl_->currentCamera(); controller.apply(view); impl_->storeCamera(view);
    impl_->navigation=controller; impl_->resetNavigation();
    impl_->navigationCameraId=impl_->activeScene?impl_->activeScene->activeCamera():std::string{};
    impl_->navigationSynchronized=true;
}
bool Engine::update() { return impl_->update(); }
}
