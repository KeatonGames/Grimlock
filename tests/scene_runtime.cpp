#include <grimlock/engine.hpp>
#include <grimlock/scene.hpp>
#include <nlohmann/json.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace grimlock;
namespace {
void require(bool condition,const char* message) { if (!condition) { throw std::runtime_error(message); } }
template<class F> void rejects(F&& operation) {
    bool rejected=false; try { operation(); } catch(const std::exception&) { rejected=true; } require(rejected,"invalid scene activation accepted");
}
std::unique_ptr<Scene> single(const char* model) {
    auto scene=std::make_unique<Scene>(); const auto id=scene->createObject(); scene->setModel(id,model); return scene;
}
void stressFixture(const std::filesystem::path& output) {
    using J=nlohmann::json;
    const float vertices[]={-0.5F,0,0,0.5F,0,0,0,0.5F,0};
    {std::ofstream file(output/"triangle.bin",std::ios::binary);file.write(reinterpret_cast<const char*>(vertices),sizeof(vertices));}
    J primitives=J::array(); for(int i=0;i<64;++i) { primitives.push_back({{"attributes",{{"POSITION",0}}}}); }
    J gltf={{"asset",{{"version","2.0"}}},{"buffers",J::array({{{"uri","triangle.bin"},{"byteLength",sizeof(vertices)}}})},
        {"bufferViews",J::array({{{"buffer",0},{"byteLength",sizeof(vertices)}}})},
        {"accessors",J::array({{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"},{"min",{-0.5,0,0}},{"max",{0.5,0.5,0}}}})},
        {"meshes",J::array({{{"primitives",primitives}}})},{"nodes",J::array({{{"mesh",0}}})},
        {"scenes",J::array({{{"nodes",{0}}}})},{"scene",0}};
    std::ofstream file(output/"stress.gltf");file<<gltf.dump(); require(static_cast<bool>(file),"stress fixture write failed");
    gltf["meshes"][0]["primitives"]=J::array({primitives[0]});
    gltf["extensionsUsed"]={"KHR_lights_punctual"};
    gltf["extensions"]={{"KHR_lights_punctual",{{"lights",J::array({{{"type","directional"},{"intensity",1}},{{"type","point"},{"intensity",2}},{{"type","spot"},{"intensity",3}}})}}}};
    gltf["extensions"]["KHR_lights_punctual"]["lights"][2]["spot"]={{"innerConeAngle",0},{"outerConeAngle",0.785398163F}};
    gltf["nodes"]=J::array({{{"mesh",0},{"children",{1,2,3}}}});
    for(int i=0;i<3;++i) { gltf["nodes"].push_back({{"extensions",{{"KHR_lights_punctual",{{"light",i}}}}}}); }
    std::ofstream lights(output/"lights.gltf");lights<<gltf.dump();require(static_cast<bool>(lights),"lights fixture write failed");
}
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"expected source/output paths"); const std::filesystem::path root{argv[1]},output{argv[2]};
        std::filesystem::create_directories(output);
        for(const auto* name:{"blackrat.glb","cube.obj"}) { std::filesystem::copy_file(root/"assets/models"/name,output/name,std::filesystem::copy_options::overwrite_existing); }
        stressFixture(output);
        Engine engine{EngineConfig{.windowTitle="Scene runtime checks",.assetRoot=output}};
        auto scene=std::make_unique<Scene>("Animated scene"); const auto group=scene->createObject("group");
        const auto a=scene->createObject("rat A",group),b=scene->createObject("rat B",group);
        scene->setModel(a,"blackrat.glb"); scene->setModel(b,"./blackrat.glb");
        scene->setAnimation(a,AnimationSettings{.clip=1,.time=0.1F,.paused=true});
        scene->setAnimation(b,AnimationSettings{.clip=1,.speed=0.75F});
        engine.setScene(std::move(scene));
        auto* active=engine.scene(); require(active!=nullptr,"no active scene");
        require(active->animation(a).asset()==active->animation(b).asset(),"scene instances duplicated resources");
        std::weak_ptr<const assets::ModelData> resource=active->animation(a).asset();
        const auto& model=active->animation(a).model(); const auto dimensions=model.maximum-model.minimum;
        const float scale=2/std::max({dimensions.x,dimensions.y,dimensions.z});
        Transform fit; fit.position=-(model.minimum+dimensions*0.5F)*scale; fit.scale=glm::vec3{scale};
        active->setTransform(a,fit); fit.position.x+=2; active->setTransform(b,fit);
        for(int i=0;i<30;++i) { require(engine.update(),"closed unexpectedly"); }
        require(std::abs(active->animation(a).time()-0.1F)<1e-5F&&active->animation(b).time()>0,"independent scene animation update failed");
        auto bad=single("cube.obj"); const auto missing=bad->createObject();bad->setModel(missing,"missing.glb");
        rejects([&]{engine.setScene(std::move(bad));}); require(engine.scene()==active,"failed load replaced active scene");
        auto invalidClip=single("blackrat.glb"); invalidClip->setAnimation(1,AnimationSettings{.clip=999});
        rejects([&]{engine.setScene(std::move(invalidClip));}); require(engine.scene()==active,"bad animation replaced active scene");
        auto invalidMaterial=single("cube.obj"); invalidMaterial->setMaterialOverride(1,999);
        rejects([&]{engine.setScene(std::move(invalidMaterial));});
        active->animation(b).pause(); const auto time=active->animation(b).time(); engine.saveScene(output/"live.json");
        engine.setScene(engine.loadScene(output/"live.json")); active=engine.scene();
        require(!resource.expired(),"replacement discarded shared resource");
        require(std::abs(active->animation(b).time()-time)<1e-5F&&active->animation(b).paused(),"live animation roundtrip failed");
        active->setEnabled(group,false); require(engine.update(),"empty enabled set failed to render");
        require(!active->enabledInHierarchy(a)&&!active->enabledInHierarchy(b),"disabled hierarchy remained visible");
        active->setEnabled(group,true); active->removeObject(a); require(engine.update(),"object deletion failed");
        require(!resource.expired(),"deleting one instance freed a shared resource");
        auto retained=active->animation(b).asset(); engine.unloadScene(); require(engine.scene()==nullptr&&!resource.expired(),"retained CPU reference was invalidated");
        retained.reset(); require(engine.update(),"unloaded scene failed to render"); require(resource.expired(),"scene unload retained CPU resource");
        // 65 * 64 = 4160 primitive descriptor allocations: exceeds the entire pool
        // unless old geometry, sets and slots are actually released and reusable.
        for(int i=0;i<65;++i) {
            engine.setScene(single("stress.gltf")); require(engine.update(),"stress render failed");
            std::weak_ptr<const assets::ModelData> previous=engine.scene()->animation(1).asset();
            engine.unloadScene(); require(previous.expired(),"unload leaked model ownership");
        }
        engine.setScene(std::make_unique<Scene>()); require(engine.update(),"empty scene activation failed"); engine.unloadScene();
        auto lights=single("lights.gltf"); SceneLighting values; values.values.directional.intensity=0;
        values.values.pointCount=1; values.values.spotCount=1; lights->setLighting(values);
        engine.setScene(std::move(lights)); require(engine.update(),"combined imported/scene lights failed");
        auto* lit=engine.scene(); auto overflow=single("lights.gltf"); values.values.pointCount=maxPointLights; overflow->setLighting(values);
        rejects([&]{engine.setScene(std::move(overflow));}); require(engine.scene()==lit,"combined light overflow replaced scene"); engine.unloadScene();
        std::cout<<"PASS Scene runtime: shared resources/independent playback, disabled parents, save/reload, rollback, retention, unload and 4160 descriptor allocations.\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL Scene runtime: "<<e.what()<<'\n';return 1; }
}
