#include "grimlock/scene.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace grimlock {
namespace {
template<glm::length_t N> bool finite(const glm::vec<N,float>& value) {
    for (glm::length_t i=0;i<N;++i) { if (!std::isfinite(value[i])) { return false; } }
    return true;
}
Transform checked(Transform value) {
    const float length=glm::dot(value.rotation,value.rotation);
    if (!finite(value.position)||!finite(value.scale)||!std::isfinite(length)||length<1e-12F
        ||glm::any(glm::lessThanEqual(glm::abs(value.scale),glm::vec3{1e-8F}))) {
        throw std::invalid_argument("Transform requires finite TRS, a nonzero quaternion and nonzero scales.");
    }
    if (std::abs(length-1)>1e-6F) { value.rotation=glm::normalize(value.rotation); }
    return value;
}
void checkAnimation(const AnimationSettings& value) {
    if (!std::isfinite(value.time)||!std::isfinite(value.speed)) { throw std::invalid_argument("Animation time/speed must be finite."); }
}
void checkPath(const std::filesystem::path& value) {
    const auto utf8=value.generic_u8string();
    const std::string text{utf8.begin(),utf8.end()};
    if (value.empty()||value.is_absolute()||value.has_root_name()||text.find('\\')!=std::string::npos||text.find(':')!=std::string::npos) {
        throw std::invalid_argument("Model path must be portable, relative to the asset root, with '/' separators.");
    }
    for (const auto& part:value) { if (part=="..") { throw std::invalid_argument("Model path must stay inside the asset root."); } }
}
}
Scene::Scene(std::string name) : name_(std::move(name)) { cameras_.emplace("main",SceneCamera{}); }
void Scene::setName(std::string name) { name_=std::move(name); }
void Scene::insertObject(ObjectId id,std::string name) {
    if (!id||objects_.contains(id)||id==std::numeric_limits<ObjectId>::max()) { throw std::invalid_argument("Object ID must be unique and in [1, 18446744073709551614]."); }
    SceneObject object; object.id=id; object.name=std::move(name);
    objects_.emplace(id,std::move(object)); nextId_=std::max(nextId_,id+1);
}
ObjectId Scene::createObject(std::string name,std::optional<ObjectId> parent) {
    if (parent) { (void)object(*parent); }
    const auto id=nextId_;
    insertObject(id,std::move(name));
    try { setParent(id,parent); } catch (...) { objects_.erase(id); throw; }
    return id;
}
const SceneObject& Scene::object(ObjectId id) const {
    const auto found=objects_.find(id);
    if (found==objects_.end()) { throw std::out_of_range("Unknown scene object ID "+std::to_string(id)); }
    return found->second;
}
void Scene::setName(ObjectId id,std::string name) { objects_.at(id).name=std::move(name); }
void Scene::invalidate(ObjectId id,bool transforms) {
    scratch_.clear(); scratch_.push_back(id);
    for (std::size_t i=0;i<scratch_.size();++i) {
        auto& value=objects_.at(scratch_[i]); value.dirty_=value.dirty_||transforms; value.visibilityDirty_=true;
        scratch_.insert(scratch_.end(),value.children_.begin(),value.children_.end());
    }
}
void Scene::setTransform(ObjectId id,const Transform& value) {
    auto& object=objects_.at(id); const auto local=checked(value);
    if (object.local.position==local.position&&object.local.rotation==local.rotation&&object.local.scale==local.scale) { return; }
    object.local=local; invalidate(id);
}
void Scene::setParent(ObjectId id,std::optional<ObjectId> parent) {
    auto& value=objects_.at(id);
    for (auto ancestor=parent;ancestor;ancestor=object(*ancestor).parent) {
        if (*ancestor==id) { throw std::invalid_argument("Circular parent relationship for object "+std::to_string(id)); }
    }
    if (value.parent==parent) { return; }
    if (parent) { objects_.at(*parent).children_.push_back(id); }
    if (value.parent) { std::erase(objects_.at(*value.parent).children_,id); }
    value.parent=parent; invalidate(id);
}
void Scene::setEnabled(ObjectId id,bool enabled) {
    auto& value=objects_.at(id); if (value.enabled==enabled) { return; }
    value.enabled=enabled; invalidate(id,false);
}
void Scene::setModel(ObjectId id,std::optional<std::filesystem::path> model) {
    auto& value=objects_.at(id);
    if (model) { checkPath(*model); *model=model->lexically_normal(); }
    if (value.model==model) { return; }
    value.model=std::move(model); value.instance_.reset();
    if (!value.model) { value.initialAnimation.reset(); value.materialOverride.reset(); }
}
void Scene::setTint(ObjectId id,const glm::vec4& tint) {
    if (!finite(tint)) { throw std::invalid_argument("Tint must be finite."); }
    objects_.at(id).tint=tint;
}
void Scene::setMaterialOverride(ObjectId id,std::optional<std::size_t> material) {
    auto& value=objects_.at(id);
    if (material&&!value.model) { throw std::invalid_argument("Material override requires a model."); }
    if (material&&value.instance_&&*material>=value.instance_->model().materials.size()) { throw std::out_of_range("Material override exceeds model material count."); }
    value.materialOverride=material;
}
void Scene::applyAnimation(SceneObject& value) {
    if (!value.instance_) { return; }
    auto& instance=*value.instance_;
    if (!value.initialAnimation) { instance.stop(); return; }
    const auto& settings=*value.initialAnimation;
    instance.play(settings.clip,settings.loop); instance.seek(settings.time); instance.setSpeed(settings.speed);
    if (settings.restPose) { instance.stop(); }
    else if (!settings.playing) { instance.stop(false); }
    if (settings.paused) { instance.pause(); }
}
void Scene::setAnimation(ObjectId id,std::optional<AnimationSettings> settings) {
    auto& value=objects_.at(id);
    if (settings&&!value.model) { throw std::invalid_argument("Animation requires a model."); }
    if (settings) {
        checkAnimation(*settings);
        if (value.instance_&&settings->clip>=value.instance_->model().animations.size()) { throw std::out_of_range("Animation clip exceeds model clip count."); }
    }
    value.initialAnimation=settings; applyAnimation(value);
}
assets::ModelInstance& Scene::animation(ObjectId id) {
    auto& value=objects_.at(id);
    if (!value.instance_) { throw std::logic_error("Object has no bound model; activate/update the scene first."); }
    return *value.instance_;
}
AnimationSettings Scene::animationSettings(ObjectId id) const {
    const auto& value=object(id);
    if (!value.instance_) { return value.initialAnimation.value_or(AnimationSettings{}); }
    const auto& a=*value.instance_;
    return {a.clip(),a.time(),a.speed(),a.looping(),a.running(),a.paused(),a.restPose()};
}
void Scene::bindModel(ObjectId id,std::shared_ptr<const assets::ModelData> asset) {
    auto& value=objects_.at(id);
    if (value.instance_&&value.instance_->asset()==asset) { return; }
    if (value.materialOverride&&*value.materialOverride>=asset->materials.size()) { throw std::out_of_range("Material override exceeds model material count."); }
    if (value.initialAnimation&&value.initialAnimation->clip>=asset->animations.size()) { throw std::out_of_range("Animation clip exceeds model clip count."); }
    value.instance_=std::make_unique<assets::ModelInstance>(std::move(asset)); applyAnimation(value);
}
glm::mat4 Scene::worldTransform(ObjectId id) const {
    const auto& value=object(id);
    scratch_.clear();
    auto current=std::optional{id};
    while (current&&(object(*current).dirty_||object(*current).visibilityDirty_)) { scratch_.push_back(*current); current=object(*current).parent; }
    for (auto i=scratch_.rbegin();i!=scratch_.rend();++i) {
        const auto& node=object(*i);
        if (node.dirty_) {
            node.world_=(node.parent?object(*node.parent).world_:glm::mat4{1})*node.local.matrix();
            for (int c=0;c<4;++c) { if (!finite(node.world_[c])) { throw std::invalid_argument("World transform overflow at object "+std::to_string(*i)); } }
            const float determinant=glm::determinant(node.world_);
            if (!std::isfinite(determinant)||std::abs(determinant)<1e-12F) { throw std::invalid_argument("Singular world transform at object "+std::to_string(*i)); }
        }
        node.visible_=node.enabled&&(!node.parent||object(*node.parent).visible_); node.dirty_=node.visibilityDirty_=false;
    }
    return value.world_;
}
bool Scene::enabledInHierarchy(ObjectId id) const { (void)worldTransform(id); return object(id).visible_; }
void Scene::updateTransforms() const { for (const auto& [id,value]:objects_) { if (value.dirty_||value.visibilityDirty_) { (void)worldTransform(id); } } }
void Scene::updateAnimations(double delta) {
    updateTransforms();
    for (auto& [id,value]:objects_) { (void)id; if (value.instance_&&value.visible_) { value.instance_->update(delta); } }
}
void Scene::setCamera(std::string id,const SceneCamera& value) {
    if (id.empty()) { throw std::invalid_argument("Camera identifier cannot be empty."); }
    if (value.object) { (void)object(*value.object); }
    (void)value.camera.view(); (void)value.camera.projection(1);
    cameras_.insert_or_assign(std::move(id),value);
}
void Scene::setActiveCamera(const std::string& id) { (void)cameras_.at(id); activeCamera_=id; }
Camera Scene::resolvedCamera() const {
    auto value=cameras_.at(activeCamera_);
    if (value.object) {
        const auto world=worldTransform(*value.object);
        value.camera.position=glm::vec3{world*glm::vec4{value.camera.position,1}};
        value.camera.target=glm::vec3{world*glm::vec4{value.camera.target,1}};
        value.camera.up=glm::vec3{world*glm::vec4{value.camera.up,0}};
    }
    (void)value.camera.view(); (void)value.camera.projection(1); return value.camera;
}
void Scene::setResolvedCamera(const Camera& camera) {
    (void)camera.view(); (void)camera.projection(1);
    auto value=cameras_.at(activeCamera_); value.camera=camera;
    if (value.object) {
        const auto inverse=glm::inverse(worldTransform(*value.object));
        value.camera.position=glm::vec3{inverse*glm::vec4{camera.position,1}};
        value.camera.target=glm::vec3{inverse*glm::vec4{camera.target,1}};
        value.camera.up=glm::vec3{inverse*glm::vec4{camera.up,0}};
    }
    setCamera(activeCamera_,value);
}
void Scene::setLighting(const SceneLighting& value) {
    validateLighting(value.values);
    if (value.directionalObject) { (void)object(*value.directionalObject); }
    for (std::uint32_t i=0;i<value.values.pointCount;++i) { if (value.pointObjects[i]) { (void)object(*value.pointObjects[i]); } }
    for (std::uint32_t i=0;i<value.values.spotCount;++i) { if (value.spotObjects[i]) { (void)object(*value.spotObjects[i]); } }
    lighting_=value;
}
Lighting Scene::resolvedLighting() const {
    auto result=lighting_.values;
    if (lighting_.directionalObject) {
        const auto id=*lighting_.directionalObject;
        result.directional.direction=glm::vec3{worldTransform(id)*glm::vec4{result.directional.direction,0}};
        if (!enabledInHierarchy(id)) { result.directional.intensity=0; }
    }
    result.pointCount=result.spotCount=0;
    for (std::uint32_t i=0;i<lighting_.values.pointCount;++i) {
        auto light=lighting_.values.points[i];
        if (const auto id=lighting_.pointObjects[i]) {
            if (!enabledInHierarchy(*id)) { continue; }
            light.position=glm::vec3{worldTransform(*id)*glm::vec4{light.position,1}};
        }
        result.points[result.pointCount++]=light;
    }
    for (std::uint32_t i=0;i<lighting_.values.spotCount;++i) {
        auto light=lighting_.values.spots[i];
        if (const auto id=lighting_.spotObjects[i]) {
            if (!enabledInHierarchy(*id)) { continue; }
            const auto world=worldTransform(*id);
            light.position=glm::vec3{world*glm::vec4{light.position,1}};
            light.direction=glm::vec3{world*glm::vec4{light.direction,0}};
        }
        result.spots[result.spotCount++]=light;
    }
    validateLighting(result); return result;
}
void Scene::removeObject(ObjectId rootId) {
    (void)object(rootId); updateTransforms();
    std::vector<ObjectId> removed{rootId};
    for (std::size_t i=0;i<removed.size();++i) {
        const auto& node=object(removed[i]); removed.insert(removed.end(),node.children_.begin(),node.children_.end());
    }
    const auto deleting=[&](std::optional<ObjectId> ref) { return ref&&std::find(removed.begin(),removed.end(),*ref)!=removed.end(); };
    // Attached cameras/lights survive as world-space settings, never dangling IDs.
    for (auto& [name,camera]:cameras_) {
        (void)name;
        if (deleting(camera.object)) {
            const auto world=worldTransform(*camera.object);
            camera.camera.position=glm::vec3{world*glm::vec4{camera.camera.position,1}};
            camera.camera.target=glm::vec3{world*glm::vec4{camera.camera.target,1}};
            camera.camera.up=glm::vec3{world*glm::vec4{camera.camera.up,0}}; camera.object.reset();
        }
    }
    if (deleting(lighting_.directionalObject)) {
        const auto id=*lighting_.directionalObject;
        lighting_.values.directional.direction=glm::vec3{worldTransform(id)*glm::vec4{lighting_.values.directional.direction,0}};
        if (!enabledInHierarchy(id)) { lighting_.values.directional.intensity=0; }
        lighting_.directionalObject.reset();
    }
    for (std::uint32_t i=0;i<lighting_.values.pointCount;++i) {
        if (!deleting(lighting_.pointObjects[i])) { continue; }
        const auto id=*lighting_.pointObjects[i];
        auto& light=lighting_.values.points[i]; light.position=glm::vec3{worldTransform(id)*glm::vec4{light.position,1}};
        if (!enabledInHierarchy(id)) { light.intensity=0; } lighting_.pointObjects[i].reset();
    }
    for (std::uint32_t i=0;i<lighting_.values.spotCount;++i) {
        if (!deleting(lighting_.spotObjects[i])) { continue; }
        const auto id=*lighting_.spotObjects[i]; const auto world=worldTransform(id); auto& light=lighting_.values.spots[i];
        light.position=glm::vec3{world*glm::vec4{light.position,1}}; light.direction=glm::vec3{world*glm::vec4{light.direction,0}};
        if (!enabledInHierarchy(id)) { light.intensity=0; } lighting_.spotObjects[i].reset();
    }
    if (object(rootId).parent) { std::erase(objects_.at(*object(rootId).parent).children_,rootId); }
    for (const auto removedId:removed) { objects_.erase(removedId); }
}
}
