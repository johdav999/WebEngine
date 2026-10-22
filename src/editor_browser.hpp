#pragma once
#include "engine/camera_component.hpp"
#include "engine/scene.hpp"
#include "engine/scene_archive.hpp"
#include "engine/editor_play.hpp"
#include "engine/effect_preview.hpp"
#include "engine/orbit_controller.hpp"
namespace engine {
class EditorSession;
EditorSession* browserEditorSession();
Scene* browserEditorScene();
void refreshBrowserEditor();
void invalidateBrowserAnimationBinding();
void invalidateBrowserPrefabBinding();
void invalidateBrowserContentIteration();
bool browserEditorActive();
void setBrowserEditorEnabled(bool enabled);
void bindBrowserEditor(Scene& scene,CameraComponent& camera,OrbitController& orbit,AssetLibrary& assets,std::span<const RenderView> views={});
void updateBrowserEditorCamera(const InputSystem& input,float dt,float height);
void unbindBrowserEditor(); // Before the borrowed scene, camera or orbit controller is destroyed.
void tickBrowserEditor();
bool browserEditorPlaying();
bool browserEditorRegionActive();
Scene& browserEditorRegionScene();
CameraComponent& browserEditorRegionCamera();
void startBrowserEditorRegion();
void stopBrowserEditorRegion();
void advanceBrowserEditorRegion();
bool browserEditorFxActive();
EffectPreview& browserEditorFxPreview();
void startBrowserEditorFx();
void stopBrowserEditorFx();
void updateBrowserEditorFx(const InputSystem& input,float dt,float height);

EditorPlaySession& browserEditorPlaySession();
void startBrowserEditorPlay(std::span<const RenderView> views);
void stopBrowserEditorPlay();
double advanceBrowserEditorPlay(double timestamp,const InputSystem& input,float inputHeight,bool suspended);
void prepareBrowserEditorSave();
void markBrowserEditorSaved();
}
