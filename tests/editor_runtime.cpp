#include "editor.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace grimlock;
namespace {
void require(bool value,const char* message) { if(!value) { throw std::runtime_error(message); } }
bool near(const glm::mat4& a,const glm::mat4& b) {
    for(int column=0;column<4;++column) { for(int row=0;row<4;++row) {
        if(std::abs(a[column][row]-b[column][row])>1e-4F*std::max(1.0F,std::abs(b[column][row]))) { return false; }
    } }return true;
}
template<class Function> void rejects(Function&& function) { bool failed=false;try { function(); }catch(const std::exception&) { failed=true; }require(failed,"invalid operation accepted"); }
void push(SDL_Event event) { require(SDL_PushEvent(&event),SDL_GetError()); }
void tick(Engine& engine,int frames=2) { for(int frame=0;frame<frames;++frame) { require(engine.update(),"editor unexpectedly closed"); } }
void button(SDL_WindowID id,Uint8 value,bool down,glm::vec2 point) {
    SDL_Event motion{};motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.windowID=id;motion.motion.x=point.x;motion.motion.y=point.y;push(motion);
    SDL_Event event{};event.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;event.button.windowID=id;event.button.button=value;event.button.x=point.x;event.button.y=point.y;push(event);
}
void motion(Engine& engine,SDL_WindowID id,glm::vec2 point) {
    SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=id;event.motion.x=point.x;event.motion.y=point.y;push(event);tick(engine);
}
void click(Engine& engine,SDL_WindowID id,glm::vec2 point) {
    motion(engine,id,point);button(id,SDL_BUTTON_LEFT,true,point);tick(engine);button(id,SDL_BUTTON_LEFT,false,point);tick(engine);
}
void drag(Engine& engine,SDL_WindowID id,glm::vec2 from,glm::vec2 to) {
    motion(engine,id,from);
    // Automated gestures finish faster than a human: don't turn consecutive drags into rename double-clicks.
    ImGui::GetIO().MouseClickedTime[0]=-1e10;
    button(id,SDL_BUTTON_LEFT,true,from);tick(engine);
    motion(engine,id,from+glm::vec2{12,0});require(ImGui::GetCurrentContext()->DragDropActive,"drag source did not start");motion(engine,id,to);
    button(id,SDL_BUTTON_LEFT,false,to);tick(engine,3);
}
ImGuiWindow* objectList() {
    for(auto* window:ImGui::GetCurrentContext()->Windows) {
        if(window->ParentWindow&&std::string_view{window->ParentWindow->Name}=="Scene Hierarchy"&&std::string_view{window->Name}.find("/Objects_")!=std::string_view::npos) { return window; }
    }
    throw std::runtime_error("hierarchy object list missing");
}
glm::vec2 inspectorControl(Engine& engine,SDL_WindowID windowId,ObjectId object,const char* label,float x=20) {
    const auto* inspector=ImGui::FindWindowByName("Inspector");require(inspector,"Inspector missing");
    const auto key=std::to_string(object);const auto id=ImHashStr(label,0,ImHashStr(key.c_str(),0,inspector->ID));
    // Probe stable widget IDs, not fixed pixel offsets that depend on model path wrapping/font metrics.
    for(float y=inspector->DC.CursorStartPos.y+5;y<inspector->ClipRect.Max.y;y+=6) {
        const glm::vec2 point{inspector->DC.CursorStartPos.x+x,y};motion(engine,windowId,point);
        if(ImGui::GetHoveredID()==id) { return point; }
    }
    throw std::runtime_error(std::string{"Inspector control missing: "}+label);
}
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"expected Game source and output paths");const std::filesystem::path game{argv[1]},output{argv[2]};
        std::filesystem::create_directories(output);
        Engine engine{EngineConfig{.windowTitle="Editor runtime checks",.assetRoot=game/"assets"}};
        {
            editor::Editor editor{engine};auto& document=editor.document();tick(engine);
            require(ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_DockingEnable,"docking not enabled");
            require(!(ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_ViewportsEnable),"multiple viewports enabled");
            require(!document.dirty(),"new empty document unexpectedly dirty");
            const auto empty=document.createGroup();require(engine.selectedObject()==empty,"group cannot be selected");
            document.rename(empty,"Parent");const auto child=engine.scene()->createObject("child",empty);
            require(engine.scene()->children(empty).size()==1,"public hierarchy children missing");
            Transform transform{{1,2,3},editor::SceneDocument::rotationQuaternion({23,71,-38}),{2,1,0}};
            document.setTransform(empty,transform);require(std::abs(engine.scene()->object(empty).local.scale.z)>=1e-4F,"zero scale not constrained");
            auto invalid=transform;invalid.position.x=std::numeric_limits<float>::quiet_NaN();rejects([&]{document.setTransform(empty,invalid);});
            const auto before=engine.scene()->object(empty).local.matrix();invalid=transform;invalid.scale={1e30F,1e30F,1e30F};rejects([&]{document.setTransform(empty,invalid);});
            require(engine.scene()->object(empty).local.matrix()==before,"invalid world transform was not rolled back");
            for(const auto angles:{glm::vec3{23,71,-38},glm::vec3{-50,89,170},glm::vec3{170,-50,-175}}) {
                const auto q=editor::SceneDocument::rotationQuaternion(angles);
                require(std::abs(glm::dot(q,editor::SceneDocument::rotationQuaternion(editor::SceneDocument::rotationDegrees(q))))>0.99999F,"Euler/quaternion convention mismatch");
            }
            const auto a=document.addModel("models/cube.obj"),b=document.duplicate(a);
            require(engine.scene()->animation(a).asset()==engine.scene()->animation(b).asset(),"editor duplicated shared resource");
            document.setEnabled(b,false);require(engine.selectedObject()==b,"hidden object cannot remain in inspector");tick(engine);
            document.setEnabled(b,true);document.remove(b);require(!engine.selectedObject()&&engine.scene()->contains(a),"delete broke shared instance");
            const auto count=engine.scene()->objects().size();rejects([&]{(void)document.addModel("models/missing.glb");});
            require(engine.scene()->objects().size()==count,"failed addition left placeholder");tick(engine);
            // Editor-only grouping preserves world space without changing Scene's local-TRS API.
            const auto attachment=engine.scene()->createObject("Attachment",a);
            document.setTransform(attachment,Transform{{0.3F,0.4F,-0.7F},editor::SceneDocument::rotationQuaternion({0,12,0}),{1,1,1}});
            const auto attachmentWorld=engine.scene()->worldTransform(attachment);
            const auto group=document.createGroup();document.rename(group,"Actors");
            document.setTransform(group,Transform{{3,-2,4},editor::SceneDocument::rotationQuaternion({10,35,-12}),{-2,2,2}});
            const auto world=engine.scene()->worldTransform(a);
            document.reparent(a,group);require(near(engine.scene()->worldTransform(a),world),"reparent changed mirrored world transform");
            require(near(engine.scene()->worldTransform(attachment),attachmentWorld),"reparent moved descendant");
            require(engine.scene()->object(a).parent==group&&engine.selectedObject()==a,"reparent did not retain selection/parent");
            const auto subgroup=document.groupObject(a);require(engine.scene()->object(subgroup).parent==group,"group selected lost original parent");
            require(engine.scene()->object(a).parent==subgroup&&near(engine.scene()->worldTransform(a),world),"group selected moved model");
            rejects([&]{document.reparent(group,subgroup);});rejects([&]{document.reparent(group,group);});
            rejects([&]{document.reparent(group,a);});rejects([&]{document.reparent(a,999999);});
            require(!engine.scene()->object(group).parent&&engine.scene()->object(a).parent==subgroup,"invalid reparent mutated hierarchy");
            document.save(output/"groups.json");document.reparent(a,{});document.open(output/"groups.json");
            require(engine.scene()->object(a).parent==subgroup&&engine.scene()->object(subgroup).parent==group,"group roundtrip lost nested parents");
            require(engine.scene()->object(group).name=="Actors"&&near(engine.scene()->worldTransform(a),world),"group roundtrip changed name/world transform");
            require(engine.scene()->object(attachment).parent==a&&near(engine.scene()->worldTransform(attachment),attachmentWorld),"roundtrip changed descendant transform");
            document.reparent(a,{});require(near(engine.scene()->worldTransform(a),world),"move to root changed world transform");
            const auto shearGroup=document.createGroup();
            document.setTransform(shearGroup,Transform{{-3,2,0},{1,0,0,0},{1,2,3}});
            document.reparent(a,shearGroup);require(near(engine.scene()->worldTransform(a),world),"aligned non-uniform scales failed");
            document.reparent(a,{});
            document.setTransform(shearGroup,Transform{{0,0,0},editor::SceneDocument::rotationQuaternion({0,37,0}),{1,2,3}});
            document.save(output/"unchanged.json");
            rejects([&]{document.reparent(a,shearGroup);});
            require(!document.dirty()&&!engine.scene()->object(a).parent&&near(engine.scene()->worldTransform(a),world),"unsupported shear was not rejected transactionally");
            document.remove(shearGroup);document.remove(group);document.remove(attachment);
            require(!engine.scene()->contains(subgroup)&&engine.scene()->contains(a),"group cascade deletion lost root model or retained child group");
            document.save(output/"saved.json");require(!document.dirty(),"save did not clear dirty state");
            document.rename(a,"Changed");require(document.dirty(),"rename did not dirty scene");auto* active=engine.scene();
            rejects([&]{document.open(output/"missing.json");});require(engine.scene()==active&&document.dirty(),"failed open discarded changes");
            document.open(output/"saved.json");require(!document.dirty()&&engine.scene()->contains(child),"scene reload lost hierarchy");
            require(engine.scene()->object(empty).local.matrix()==before,"scene roundtrip changed local transform");
            tick(engine);require(!document.dirty(),"idle playback marked scene dirty");
            // Real ImGui capture must cancel viewport gestures and selection.
            int windowCount=0;auto windows=SDL_GetWindows(&windowCount);require(windows&&windowCount==1,"expected editor window");auto* window=windows[0];SDL_free(windows);const auto id=SDL_GetWindowID(window);
            SDL_Event event{};event.type=SDL_EVENT_WINDOW_FOCUS_GAINED;event.window.windowID=id;push(event);
            const auto* hierarchy=ImGui::FindWindowByName("Scene Hierarchy");require(hierarchy!=nullptr,"hierarchy missing");
            const glm::vec2 panel{hierarchy->Pos.x+80,hierarchy->Pos.y+hierarchy->Size.y-70};
            const auto camera=engine.scene()->resolvedCamera();engine.scene()->selectObject(a,true);
            button(id,SDL_BUTTON_RIGHT,true,panel);tick(engine);
            require(!SDL_GetWindowRelativeMouseMode(window)&&!engine.input().button(MouseButton::right).held,"UI right drag acquired camera capture");
            event={};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=id;event.motion.x=panel.x;event.motion.y=panel.y;event.motion.xrel=90;event.motion.yrel=80;push(event);tick(engine);
            require(engine.scene()->resolvedCamera().position==camera.position,"UI drag moved camera");
            button(id,SDL_BUTTON_RIGHT,false,panel);button(id,SDL_BUTTON_LEFT,true,panel);tick(engine);button(id,SDL_BUTTON_LEFT,false,panel);tick(engine);
            require(engine.selectedObject()==a,"UI background clicked through to 3D selection");
            const auto* inspector=ImGui::FindWindowByName("Inspector");require(inspector!=nullptr,"inspector missing");
            // The hierarchy context menu consumes the first outside click, as standard ImGui popups do.
            const glm::vec2 dismiss{inspector->Pos.x+80,inspector->Pos.y+inspector->Size.y-70};
            button(id,SDL_BUTTON_LEFT,true,dismiss);tick(engine);button(id,SDL_BUTTON_LEFT,false,dismiss);tick(engine);
            const auto& inspectorStyle=ImGui::GetStyle();
            const glm::vec2 nameInput{inspector->DC.CursorStartPos.x+80,inspector->DC.CursorStartPos.y+
                ImGui::GetFontSize()*2+inspectorStyle.SeparatorTextPadding.y*2+inspectorStyle.ItemSpacing.y*2+ImGui::GetFrameHeight()*0.5F};
            event={};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=id;event.motion.x=nameInput.x;event.motion.y=nameInput.y;push(event);tick(engine);
            button(id,SDL_BUTTON_LEFT,true,nameInput);tick(engine);button(id,SDL_BUTTON_LEFT,false,nameInput);tick(engine);
            require(ImGui::GetIO().WantCaptureKeyboard,"name input did not capture keyboard");
            event={};event.type=SDL_EVENT_KEY_DOWN;event.key.windowID=id;event.key.scancode=SDL_SCANCODE_W;event.key.key=SDLK_W;push(event);
            ImGui::GetIO().AddInputCharactersUTF8("X");tick(engine);
            require(!engine.input().key(SDL_SCANCODE_W).held,"captured keyboard leaked into Engine input");
            require(engine.scene()->object(a).name.find('X')!=std::string::npos,"Inspector text edit did not update Scene");
            event.type=SDL_EVENT_KEY_UP;push(event);button(id,SDL_BUTTON_LEFT,true,panel);tick(engine);button(id,SDL_BUTTON_LEFT,false,panel);tick(engine);
            // Viewport gestures still work when the same backend is attached.
            const glm::vec2 viewport{640,580};button(id,SDL_BUTTON_RIGHT,true,viewport);tick(engine);
            require(SDL_GetWindowRelativeMouseMode(window),"viewport right drag failed with ImGui attached");
            event={};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=id;event.motion.x=viewport.x;event.motion.y=viewport.y;event.motion.xrel=25;event.motion.yrel=-10;push(event);tick(engine);
            require(engine.scene()->resolvedCamera().position!=camera.position,"viewport pan did not move camera");
            button(id,SDL_BUTTON_RIGHT,false,viewport);tick(engine);document.observeCamera();require(document.dirty(),"navigation camera edit not tracked");
            require(!editor.requestClose(),"unsaved close was not deferred");tick(engine);
            require(ImGui::IsPopupOpen("Unsaved Changes",ImGuiPopupFlags_AnyPopupId),"unsaved warning missing");
            const auto* warning=ImGui::FindWindowByName("Unsaved Changes");require(warning!=nullptr,"unsaved modal missing");
            const auto& style=ImGui::GetStyle();
            const float saveWidth=ImGui::CalcTextSize("Save and Continue").x+style.FramePadding.x*2;
            const float discardWidth=ImGui::CalcTextSize("Discard").x+style.FramePadding.x*2;
            const glm::vec2 cancel{warning->DC.CursorStartPos.x+saveWidth+discardWidth+style.ItemSpacing.x*2+10,
                warning->DC.CursorStartPos.y+ImGui::GetFontSize()+style.ItemSpacing.y+8};
            button(id,SDL_BUTTON_LEFT,true,cancel);tick(engine);button(id,SDL_BUTTON_LEFT,false,cancel);tick(engine);
            require(document.dirty()&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId),"Cancel discarded changes or left modal stuck");
            require(!editor.requestClose(),"second unsaved close was not deferred");tick(engine);
            // Lifecycle cleanup while the unsaved modal is open must also be safe.
        }
        require(ImGui::GetCurrentContext()==nullptr,"editor context leaked");
        tick(engine); // Detaching leaves a functioning non-editor Engine.
        {
            editor::Editor editor{engine};auto& document=editor.document();document.newScene();
            const auto group=document.createGroup(),model=document.addModel("models/cube.obj");tick(engine);
            int count=0;auto windows=SDL_GetWindows(&count);require(windows&&count==1,"expected grouping editor window");
            const auto id=SDL_GetWindowID(windows[0]);SDL_free(windows);
            const auto start=objectList()->DC.CursorStartPos;const auto rowHeight=ImGui::GetFrameHeightWithSpacing();
            const glm::vec2 groupRow{start.x+110,start.y+ImGui::GetFrameHeight()*0.5F},modelRow=groupRow+glm::vec2{0,rowHeight};
            const auto world=engine.scene()->worldTransform(model);
            drag(engine,id,modelRow,groupRow);
            require(engine.scene()->object(model).parent==group&&near(engine.scene()->worldTransform(model),world),"hierarchy drag into group failed");
            const glm::vec2 root{start.x+100,objectList()->Pos.y-ImGui::GetStyle().ItemSpacing.y-ImGui::GetFontSize()*0.5F};
            drag(engine,id,modelRow,root);
            require(!engine.scene()->object(model).parent&&near(engine.scene()->worldTransform(model),world),"hierarchy drag to Scene Root failed");
            click(engine,id,groupRow);click(engine,id,groupRow); // Double-click hierarchy label.
            require(ImGui::GetIO().WantCaptureKeyboard,"hierarchy rename did not focus text input");
            ImGui::GetIO().AddInputCharactersUTF8("Renamed group");tick(engine);
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter,true);tick(engine);ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter,false);tick(engine);
            require(engine.scene()->object(group).name=="Renamed group","hierarchy rename did not commit");
            document.reparent(model,group);document.save(output/"group-ui.json");engine.scene()->selectObject(group,true);tick(engine);
            const auto deleteMenu=[&] {
                motion(engine,id,groupRow);button(id,SDL_BUTTON_RIGHT,true,groupRow);tick(engine);button(id,SDL_BUTTON_RIGHT,false,groupRow);tick(engine);
                const auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;
                require(!popups.empty()&&popups.back().Window,"object context menu missing");const auto* popup=popups.back().Window;
                click(engine,id,{popup->DC.CursorStartPos.x+35,popup->DC.CursorPosPrevLine.y+ImGui::GetFontSize()*0.5F});
                require(ImGui::IsPopupOpen("Delete from Scene",ImGuiPopupFlags_AnyPopupId),"delete confirmation missing");
            };
            deleteMenu();
            auto* confirmation=ImGui::FindWindowByName("Delete from Scene");require(confirmation,"delete modal missing");
            const auto& style=ImGui::GetStyle();const float buttonY=confirmation->DC.CursorStartPos.y+
                3*ImGui::GetTextLineHeightWithSpacing()+style.ItemSpacing.y+ImGui::GetFrameHeight()*0.5F;
            click(engine,id,{confirmation->DC.CursorStartPos.x+ImGui::CalcTextSize("Delete").x+style.FramePadding.x*2+style.ItemSpacing.x+15,buttonY});
            require(engine.scene()->contains(group)&&engine.scene()->contains(model)&&!document.dirty(),"Cancel changed group contents or dirty state");
            deleteMenu();confirmation=ImGui::FindWindowByName("Delete from Scene");
            click(engine,id,{confirmation->DC.CursorStartPos.x+15,buttonY});
            require(!engine.scene()->contains(group)&&!engine.scene()->contains(model)&&document.dirty(),"confirmed deletion did not remove group and descendants");
        }
        require(ImGui::GetCurrentContext()==nullptr,"grouping editor context leaked");
        {
            editor::Editor editor{engine};auto& document=editor.document();document.newScene();
            const auto a=document.addModel("models/blackrat.glb"),b=document.duplicate(a);
            require(engine.scene()->availableAnimations(a).size()==13,"actual imported animation list missing");
            require(engine.scene()->animation(a).asset()==engine.scene()->animation(b).asset(),"editor animation instances did not share resources");
            engine.scene()->selectObject(a,true);document.save(output/"animation-ui.json");tick(engine);
            int count=0;auto windows=SDL_GetWindows(&count);require(windows&&count==1,"expected animation editor window");
            auto* window=windows[0];const auto id=SDL_GetWindowID(window);SDL_free(windows);
            require(SDL_SetWindowSize(window,1280,480),SDL_GetError());tick(engine,8);
            (void)inspectorControl(engine,id,a,"##Playback Speed",80);
            // Auto-reveal is one-shot; users can still scroll back to General/Transform.
            auto* inspector=ImGui::FindWindowByName("Inspector");require(inspector->Scroll.y>0,"compact animated Inspector was not revealed");
            ImGui::SetScrollY(inspector,0);tick(engine,3);require(inspector->Scroll.y==0,"Inspector fought manual scrolling");
            engine.scene()->selectObject(b,true);tick(engine,4);
            (void)inspectorControl(engine,id,b,"##Animation Clip",80);
            engine.scene()->selectObject(a,true);tick(engine,4);
            click(engine,id,inspectorControl(engine,id,a,"##Animation Clip",80));
            const auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;require(!popups.empty()&&popups.back().Window,"animation clip dropdown missing");
            const auto* popup=popups.back().Window;
            click(engine,id,{popup->DC.CursorStartPos.x+50,popup->DC.CursorStartPos.y+ImGui::GetTextLineHeightWithSpacing()+ImGui::GetFontSize()*0.5F});
            require(engine.scene()->animationConfiguration(a).clip==1&&engine.scene()->animationSettings(a).clip==1&&document.dirty(),"clip selection did not update pose/configuration");
            require(engine.scene()->animationSettings(b).restPose,"clip selection affected another instance");
            document.save(output/"animation-ui.json");
            click(engine,id,inspectorControl(engine,id,a,"Resume"));
            require(engine.scene()->animation(a).playing()&&!document.dirty(),"Resume preview changed persistent configuration");
            click(engine,id,inspectorControl(engine,id,a,"Pause"));const auto time=engine.scene()->animationSettings(a).time;
            const auto pose=engine.scene()->animation(a).pose().world;tick(engine,3);
            require(engine.scene()->animationSettings(a).time==time&&engine.scene()->animation(a).pose().world==pose&&!document.dirty(),"Pause preview changed pose or dirtied scene");
            click(engine,id,inspectorControl(engine,id,a,"Stop",80));
            require(engine.scene()->animationSettings(a).restPose&&!document.dirty(),"Stop preview changed persistent configuration");
            click(engine,id,inspectorControl(engine,id,a,"Loop"));
            require(!engine.scene()->animationConfiguration(a).loop&&document.dirty(),"Loop checkbox did not persist");
            click(engine,id,inspectorControl(engine,id,a,"Autoplay",100));
            require(engine.scene()->animationConfiguration(a).autoplay&&engine.scene()->animationSettings(a).restPose,"Autoplay checkbox started preview or failed to persist");
            const auto speedInput=inspectorControl(engine,id,a,"##Playback Speed",80);
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,true);tick(engine);click(engine,id,speedInput);
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,false);tick(engine);
            ImGui::GetIO().AddInputCharactersUTF8("1.25");tick(engine);
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter,true);tick(engine);ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter,false);tick(engine);
            require(engine.scene()->animationConfiguration(a).speed==1.25F&&document.dirty(),"Playback Speed input did not update saved configuration");
            auto configuration=engine.scene()->animationConfiguration(a);
            rejects([&]{configuration.speed=std::numeric_limits<float>::quiet_NaN();document.setAnimationConfiguration(a,configuration);});
            const auto duplicate=document.duplicate(a);
            require(engine.scene()->animationConfiguration(duplicate).autoplay&&engine.scene()->animation(duplicate).playing(),"duplicate did not copy authored animation configuration");
            document.save(output/"animation-ui.json");engine.scene()->stopAnimation(a);tick(engine);
            require(!document.dirty(),"normal preview playback dirtied saved scene");
            document.open(output/"animation-ui.json");
            require(engine.scene()->animation(a).playing()&&engine.scene()->animationSettings(a).clip==1&&engine.scene()->animationSettings(a).speed==1.25F&&!engine.scene()->animationSettings(a).loop,"saved autoplay configuration was not restored");
            require(engine.scene()->animationSettings(b).restPose,"independent disabled autoplay was not preserved");
            require(SDL_SetWindowSize(window,1280,720),SDL_GetError());tick(engine,6);
            const auto staticModel=document.addModel("models/cube.obj");tick(engine,4);
            require(engine.scene()->availableAnimations(staticModel).empty(),"static model unexpectedly has animations");
            const auto beforeSelection=document.dirty();engine.scene()->selectObject(a,true);tick(engine,4);
            inspector=ImGui::FindWindowByName("Inspector");require(inspector->Scroll.y==0,"tall Inspector retained stale selection scroll");
            require(document.dirty()==beforeSelection,"Inspector reveal dirtied the scene");
        }
        require(ImGui::GetCurrentContext()==nullptr,"animation editor context leaked");
        {
            editor::Editor editor{engine,output/"saved.json"};auto& document=editor.document();tick(engine);
            document.rename(3,"Saved by confirmation");require(!editor.requestClose(),"dirty second close not deferred");tick(engine);
            const auto* warning=ImGui::FindWindowByName("Unsaved Changes");require(warning!=nullptr,"save confirmation missing");
            const glm::vec2 saveButton{warning->DC.CursorStartPos.x+20,warning->DC.CursorStartPos.y+ImGui::GetFontSize()+ImGui::GetStyle().ItemSpacing.y+8};
            int count=0;auto windows=SDL_GetWindows(&count);require(windows&&count==1,"expected reattached editor window");const auto id=SDL_GetWindowID(windows[0]);SDL_free(windows);
            button(id,SDL_BUTTON_LEFT,true,saveButton);tick(engine);button(id,SDL_BUTTON_LEFT,false,saveButton);
            bool running=true;for(int frame=0;frame<3&&running;++frame) { running=engine.update(); }
            require(!running&&!document.dirty(),"Save and Continue did not save then close");
            const auto saved=Scene::load(output/"saved.json");require(saved->object(3).name=="Saved by confirmation","confirmation save lost edits");
        }
        require(ImGui::GetCurrentContext()==nullptr,"reattached editor leaked its context");
        std::cout<<"PASS editor: document commands, shared resources, hierarchy/TRS roundtrips, mirrored world-preserving grouping, cycle/shear rollback, real hierarchy drag/drop/rename/delete confirmation, docking, input capture, viewport pan, dirty close and deterministic detach.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL editor: "<<error.what()<<'\n';return 1; }
}
