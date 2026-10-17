#include <grimlock/scene.hpp>
#include <nlohmann/json.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace grimlock;
using Json=nlohmann::json;
namespace {
void require(bool condition,const char* text) { if (!condition) { throw std::runtime_error(text); } }
bool near(const glm::mat4& a,const glm::mat4& b) {
    for (int c=0;c<4;++c) for (int r=0;r<4;++r) { if (std::abs(a[c][r]-b[c][r])>1e-5F) { return false; } } return true;
}
template<class F> void rejects(F&& operation) {
    bool rejected=false; try { operation(); } catch (const std::exception&) { rejected=true; } require(rejected,"invalid operation accepted");
}
Json read(const std::filesystem::path& path) { std::ifstream file(path); return Json::parse(file); }
void write(const std::filesystem::path& path,const Json& j) { std::ofstream file(path); file<<j.dump(2); if (!file) { throw std::runtime_error("fixture write failed"); } }
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"expected source and output paths");
        const std::filesystem::path root{argv[1]},output{argv[2]}; std::filesystem::create_directories(output);
        Scene scene{"Round trip \"scene\" / Unicode \xE2\x98\x83"};
        const auto parent=scene.createObject("parent"),child=scene.createObject("child",parent),leaf=scene.createObject("leaf",child);
        const SceneObject* stable=&scene.object(child);
        Transform p{{2,3,4},glm::angleAxis(0.7F,glm::vec3{0,1,0}),{2,3,4}};
        Transform c{{1,2,-1},glm::angleAxis(-0.4F,glm::vec3{1,0,0}),{1,2,1}};
        scene.setTransform(parent,p); scene.setTransform(child,c);
        require(near(scene.worldTransform(child),p.matrix()*c.matrix()),"parent/local composition failed");
        for (int i=0;i<50;++i) { require(near(scene.worldTransform(leaf),p.matrix()*c.matrix()),"unchanged cached transform drifted"); }
        p.position.x+=7; scene.setTransform(parent,p);
        require(near(scene.worldTransform(leaf),p.matrix()*c.matrix()),"descendant invalidation failed");
        require(stable==&scene.object(child),"insertion invalidated object reference");
        rejects([&]{scene.setParent(parent,leaf);}); rejects([&]{scene.setParent(parent,parent);}); rejects([&]{scene.setParent(child,9999);});
        require(scene.object(child).parent==parent,"failed parenting corrupted hierarchy");
        scene.setParent(child,{}); require(near(scene.worldTransform(child),c.matrix()),"reparent did not retain local TRS"); scene.setParent(child,parent);
        scene.setEnabled(parent,false); require(!scene.enabledInHierarchy(leaf),"disabled parent did not hide child"); scene.setEnabled(parent,true);
        scene.setEnabled(leaf,false); require(!scene.enabledInHierarchy(leaf)&&scene.enabledInHierarchy(child),"independent enabled state failed");
        scene.setModel(child,"models/blackrat.glb"); scene.setAnimation(child,AnimationSettings{.clip=1,.time=0.25F,.speed=-0.75F,.paused=true});
        scene.setTint(child,{0.3F,0.4F,0.5F,1}); scene.setMaterialOverride(child,1);
        Transform bad; bad.scale.x=0; rejects([&]{scene.setTransform(parent,bad);});
        bad={}; bad.rotation={0,0,0,0}; rejects([&]{scene.setTransform(parent,bad);});
        rejects([&]{scene.setModel(child,"../outside.glb");});
        SceneCamera camera; camera.object=parent; scene.setCamera("attached",camera); scene.setActiveCamera("attached");
        const auto worldCamera=scene.resolvedCamera();
        SceneLighting light; light.directionalObject=parent;
        light.values.pointCount=1; light.values.points[0].position={1,0,0}; light.pointObjects[0]=parent;
        light.values.spotCount=1; light.spotObjects[0]=child; scene.setLighting(light);
        const auto worldLight=scene.resolvedLighting();
        require(glm::length(worldLight.points[0].position-glm::vec3{p.matrix()*glm::vec4{1,0,0,1}})<1e-5F,"attached point light transform failed");
        scene.save(output/"roundtrip.json"); auto loaded=Scene::load(output/"roundtrip.json");
        require(loaded->name()==scene.name()&&loaded->object(child).parent==parent,"identity/name/parent roundtrip failed");
        require(near(loaded->worldTransform(leaf),scene.worldTransform(leaf)),"TRS roundtrip failed");
        require(loaded->object(child).model==scene.object(child).model&&loaded->object(child).materialOverride==1,"asset/material roundtrip failed");
        require(loaded->animationSettings(child).clip==1&&loaded->animationSettings(child).paused,"animation settings roundtrip failed");
        loaded->save(output/"roundtrip-again.json"); require(read(output/"roundtrip.json")==read(output/"roundtrip-again.json"),"semantic JSON roundtrip changed data");
        scene.removeObject(parent); require(!scene.contains(child)&&!scene.contains(leaf),"cascade deletion failed");
        rejects([&]{(void)scene.object(child);});
        require(glm::length(scene.resolvedCamera().position-worldCamera.position)<1e-5F,"camera detach changed world position");
        require(glm::length(scene.resolvedLighting().points[0].position-worldLight.points[0].position)<1e-5F,"light detach changed world position");
        scene.save(output/"deleted.json"); auto deleted=Scene::load(output/"deleted.json"); require(deleted->createObject()>leaf,"deleted IDs reused after reload");
        const auto demo=root/"assets/scenes/demo.json"; auto example=Scene::load(demo);
        require(example->objects().size()==12&&example->object(5).parent==4,"demo object/hierarchy count changed");
        auto baseline=read(demo);
        const auto invalid=[&](Json document,const char* field) {
            const auto path=output/"invalid.json"; write(path,document); bool rejected=false;
            try { (void)Scene::load(path); } catch (const std::exception& e) {
                const std::string text=e.what(); require(text.find("invalid.json")!=std::string::npos,"error omitted file path");
                require(text.find(field)!=std::string::npos,"error omitted expected JSON field"); rejected=true;
            } require(rejected,"malformed scene accepted");
        };
        auto j=baseline; j["version"]=2; invalid(j,"version");
        j=baseline; j["objects"][1]["id"]=j["objects"][0]["id"]; invalid(j,".id");
        j=baseline; j["objects"][0]["parent"]="9999"; invalid(j,".parent");
        j=baseline; j["objects"][0]["parent"]="2"; j["objects"][1]["parent"]="1"; invalid(j,".parent");
        j=baseline; j["objects"][0]["transform"]["rotation"]={0,0,0,0}; invalid(j,".transform");
        j=baseline; j["objects"][0]["transform"]["scale"]={0,1,1}; invalid(j,".transform");
        j=baseline; j["objects"][0]["transform"]["position"]={1,2}; invalid(j,".position");
        j=baseline; j["objects"][0]["enabled"]="yes"; invalid(j,".enabled");
        j=baseline; j["objects"][0]["model"]="../outside.glb"; invalid(j,".model");
        j=baseline; j["objects"][0]["animation"]={{"clip",-1}}; invalid(j,".clip");
        j=baseline; j["objects"][0]["typo"]=true; invalid(j,".typo");
        j=baseline; j["active_camera"]="missing"; invalid(j,"active_camera");
        j=baseline; j["cameras"][0]["near"]=-1; invalid(j,"cameras");
        j=baseline; j["lighting"]["spots"]=Json::array({{{"inner_cone",1},{"outer_cone",0.5}}}); invalid(j,"spots");
        j=baseline; j["lighting"]["points"][0]["object"]="9999"; invalid(j,"lighting");
        j=baseline; j["lighting"]["points"]=Json::array(); for(int i=0;i<9;++i)j["lighting"]["points"].push_back(Json::object()); invalid(j,"points");
        j=baseline; j["objects"][0]["transform"]["position"]={1e200,0,0}; invalid(j,"position");
        {std::ofstream f(output/"duplicate.json");f<<"{\"version\":1,\"version\":1}";} rejects([&]{(void)Scene::load(output/"duplicate.json");});
        {std::ofstream f(output/"syntax.json");f<<"{";} rejects([&]{(void)Scene::load(output/"syntax.json");});
        write(output/"defaults.json",{{"version",1},{"objects",Json::array({{{"id","1"}}})}});
        auto defaults=Scene::load(output/"defaults.json"); require(defaults->object(1).enabled&&near(defaults->worldTransform(1),glm::mat4{1}),"optional defaults failed");
        std::cout<<"PASS Scene: hierarchy/cache, reparent/cycles, stable IDs/deletion, attached lights/camera, JSON roundtrips/defaults/negative cases.\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL Scene: "<<e.what()<<'\n';return 1; }
}
