#include "engine/renderer.hpp"
#ifdef WEBENGINE_MULTIPLAYER_SCENE
#include "scene_shared.hpp"
#else
#include "shared.hpp"
#endif
#include <emscripten.h>
namespace {
#ifdef WEBENGINE_MULTIPLAYER_SCENE
using namespace multiplayer_scene;
#else
using namespace multiplayer_example;
#endif
Application app;
View view;
Client client;
std::unique_ptr<Renderer> renderer;
int frames = 0;
void frame() {
  const auto now = emscripten_get_now() / 1000;
  ApplicationFrameCallbacks callbacks;
  callbacks.input = [](Scene &, const InputSystem &input,
                       const ApplicationFrame &) {
    auto v = input.action(InputActions::Move).value;
    client.x = int(v.x * 1000);
    client.z = int(v.y * 1000);
    client.interact |= input.action(InputActions::Interact).delta.x > 0;
  };
  app.advance(now, callbacks);
  client.update(now);
#ifdef WEBENGINE_MULTIPLAYER_SCENE
  if (client.pending) {
    auto document = view.install(client);
    const auto old = app.activeSceneId();
    auto *scene = document.scene.get();
    auto id = app.loadScene(std::move(document.scene));
    renderer->setScene(*scene, document.views);
    app.requestTransition(id);
    app.commitTransition();
    app.unloadScene(old);
  }
#endif
  #ifdef WEBENGINE_MULTIPLAYER_SCENE
view.project(*app.activeScene(), client, 1. / 60);
#else
view.project(*app.activeScene(), client.session, 1. / 60);
#endif
  renderer->serviceRecovery();
  if (renderer->ready()) {
    renderer->resizeCanvas();
    renderer->render();
    ++frames;
  }
#ifdef WEBENGINE_MULTIPLAYER_SCENE
  // Invitation is deliberately separate from the diagnostic JSON/evidence.
  EM_ASM({const field=document.getElementById('lobby-invitation');if(field)field.value=UTF8ToString($0);},client.lobbyCredentials.invitation.c_str());
#endif
  const auto json = client.json();
  // clang-format off
  EM_ASM(
      {
        window.multiplayerState = JSON.parse(UTF8ToString($0));
        Object.assign(window.multiplayerState,
                      {frames : $1, backend : $2, status : UTF8ToString($3)});
        window.refreshMultiplayer?.(window.multiplayerState);
      },
      json.c_str(), frames, int(renderer->capabilities().backend),
      client.status.c_str());
  // clang-format on
}
} // namespace
extern "C" {
#ifdef WEBENGINE_MULTIPLAYER_SCENE
EMSCRIPTEN_KEEPALIVE int multiplayer_scene_load(const void *data, int count) {
  try {
    if (count <= 0 || count > int(net::maxContent))
      throw std::invalid_argument("Scene package limit");
    const auto *p = static_cast<const std::uint8_t *>(data);
    net::Bytes candidate(p, p + count);
    (void)net::loadNetworkScene(candidate,multiplayer_gameplay::registry(),multiplayer_gameplay::invocations());
    client.publication = std::move(candidate);
    return 1;
  } catch (const std::exception &e) {
    client.status = e.what();
    return 0;
  }
}
#endif
EMSCRIPTEN_KEEPALIVE int multiplayer_join(const char *url) {
  try {
    client.join(url, emscripten_get_now() / 1000);
    return 1;
  } catch (const std::exception &e) {
    client.status = e.what();
    return 0;
  }
}
EMSCRIPTEN_KEEPALIVE void multiplayer_leave() {
#ifdef WEBENGINE_MULTIPLAYER_SCENE
  client.leave(true);
#else
  client.leave();
#endif
  app.resetClocks();
}
#ifdef WEBENGINE_MULTIPLAYER_SCENE
EMSCRIPTEN_KEEPALIVE int multiplayer_lobby(int operation,const char *guest,const char *room,const char *invitation){
  try{net::LobbyRequest request;request.operation=net::LobbyOperation(operation);request.guestName=guest?guest:"Guest";request.roomName=room?room:"Game";request.invitation=invitation?invitation:"";
    net::LobbyMessage message;message.request=request;(void)net::encodeLobby(message);client.configureLobby(std::move(request));return 1;
  }catch(const std::exception &){return 0;}
}
#endif
EMSCRIPTEN_KEEPALIVE void multiplayer_input(int x, int z, int interact) {
#ifdef WEBENGINE_MULTIPLAYER_SCENE
  client.spawnRequested |= interact==2;client.despawnRequested |= interact==3;client.pulseRequested |= interact==4;client.impulseRequested |= interact==5;
  interact=interact==1;
#endif
  InputPacket p[2]{};
  p[0].type = double(InputEventType::ActionValue);
  p[0].id = InputActions::Move;
  p[0].source = 1;
  p[0].x = std::clamp(x, -1000, 1000) / 1000.;
  p[0].y = std::clamp(z, -1000, 1000) / 1000.;
  p[1].type = double(InputEventType::ActionDelta);
  p[1].id = InputActions::Interact;
  p[1].source = 2;
  p[1].x = interact ? 1 : 0;
  app.submitInput(p);
}
}
int main() {
#ifdef WEBENGINE_MULTIPLAYER_SCENE
  client.connector=net::connectWebSocket;
#endif
  auto scene = view.make();
  auto *s = scene.get();
  auto id = app.loadScene(std::move(scene));
  renderer = std::make_unique<Renderer>(
      *s, *dynamic_cast<CameraComponent *>(s->findComponent(view.camera)));
  // clang-format off
  const bool gl = EM_ASM_INT({return new URLSearchParams(location.search).get('backend') === 'webgl2';});
  // clang-format on
  renderer->initialize(gl ? RenderBackendPreference::WebGl2
                          : RenderBackendPreference::WebGpu);
  app.start(id);
  emscripten_set_main_loop(frame, 0, true);
}
