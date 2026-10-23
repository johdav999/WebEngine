#include "controller.hpp"
#include "engine/renderer.hpp"
#include <emscripten.h>

// clang-format off
EM_JS(int, readSave, (unsigned char* out,int capacity), {
    try {const text=localStorage.getItem('webengine.quest.host.v1');if(text===null)return 0;
        if(text.length>capacity*2)throw Error('Save too large');const bytes=atob(text);
        if(bytes.length>capacity)throw Error('Save too large');
        for(let i=0;i<bytes.length;i++)HEAPU8[out+i]=bytes.charCodeAt(i);return bytes.length||-1;
    }catch(error){return -1;}
});
EM_JS(int, writeSave, (const unsigned char* data,int size), {
    try {let text="";for(let i=0;i<size;i++)text+=String.fromCharCode(HEAPU8[data+i]);
        localStorage.setItem('webengine.quest.host.v1',btoa(text));return 1;
    }catch(error){return 0;}
});
EM_JS(void, downloadSave, (const unsigned char* data,int size), {
    const url=URL.createObjectURL(new Blob([HEAPU8.slice(data,data+size)],{type:'application/octet-stream'}));
    const a=document.createElement('a');a.href=url;a.download='quest-session.qhost';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
});
// clang-format on
namespace {
engine::Application app;
std::unique_ptr<engine::Renderer> renderer;
std::unique_ptr<quest_example::Controller> controller;
int frames = 0;
void publish() {
    const auto &s = controller->session();
    const auto &p = s.game.runtime.view("demo.delivery");
    const int apples = p.counts.contains("apples") ? int(p.counts.at("apples")) : 0;
    // clang-format off
    EM_ASM({window.questDemoState=({stage:UTF8ToString($0),wallet:$1,elapsed:$2,apples:$3,location:$4,paused:!!$5,generation:$6,epoch:$7,scene:$8,sceneCount:$9,frames:$10,backend:$11,message:UTF8ToString($12),loads:$13,saves:$14,suspended:!!$15});window.refreshQuestUi(window.questDemoState);},
        s.game.label().c_str(),s.game.wallet,double(p.elapsedMs),apples,int(s.location),s.paused,
        double(s.game.runtime.state().generation),controller->epoch(),double(app.activeSceneId().value),int(app.loadedSceneCount()),frames,
        renderer?int(renderer->capabilities().backend):-1,controller->message.c_str(),int(controller->loads),int(controller->saves),app.suspended());
    // clang-format on
}
void frame() {
    try {
        controller->drain();
        renderer->serviceRecovery();
        if (renderer->ready()) {
            app.advance(emscripten_get_now() / 1000, controller->callbacks());
            renderer->resizeCanvas();
            renderer->render();
            ++frames;
        } else
            app.resetClocks(); // graphics outages never become quest catch-up time
        publish();
    } catch (const std::exception &e) {
        controller->freeze();
        controller->message = std::string("Host error: ") + e.what();
        publish();
    }
}
} // namespace
extern "C" {
EMSCRIPTEN_KEEPALIVE int quest_demo_command(int command) {
    if (!controller)
        return 0;
    try {
        if (command == 10) {
            auto bytes = controller->saveBytes();
            downloadSave(bytes.data(), int(bytes.size()));
            controller->message = "Exported";
        } else if (command >= 0 && command <= 9)
            controller->enqueue(quest_example::Action(command));
        else
            throw std::invalid_argument("Unknown command");
        return 1;
    } catch (const std::exception &e) {
        controller->message = e.what();
        return 0;
    }
}
EMSCRIPTEN_KEEPALIVE unsigned quest_demo_epoch() { return controller ? controller->epoch() : 0; }
EMSCRIPTEN_KEEPALIVE int quest_demo_import(const unsigned char *bytes, int size, unsigned epoch) {
    if (!controller)
        return 0;
    try {
        if (size < 0 || size > int(quest_example::Session::saveLimit))
            throw std::invalid_argument("Save too large");
        controller->load({bytes, std::size_t(size)}, epoch);
        return 1;
    } catch (const std::exception &e) {
        controller->message = std::string("Load failed: ") + e.what();
        return 0;
    }
}
EMSCRIPTEN_KEEPALIVE void quest_demo_suspend(int hidden) {
    if (controller) {
        controller->cancelInput();
        app.setSuspended(hidden != 0);
    }
}
EMSCRIPTEN_KEEPALIVE void quest_demo_shutdown() {
    emscripten_cancel_main_loop();
    app.shutdown();
    controller.reset();
}
}
int main() {
    // clang-format off
    const int backend=EM_ASM_INT({return new URLSearchParams(location.search).get('backend')==='webgl2'?1:0;});
    // clang-format on
    app.setSceneBinding([backend](engine::Scene *scene) {
        if (!scene) {
            renderer.reset();
            return;
        }
        auto &view = dynamic_cast<quest_example::QuestScene &>(*scene);
        engine::RenderView renderView{&view.cameraView()};
        if (renderer)
            renderer->setScene(view, {&renderView, 1});
        else {
            auto candidate = std::make_unique<engine::Renderer>(view, view.cameraView());
            candidate->initialize(backend ? engine::RenderBackendPreference::WebGl2
                                          : engine::RenderBackendPreference::WebGpu);
            renderer = std::move(candidate);
        }
    });
    quest_example::Storage storage;
    storage.read = []() -> std::optional<std::vector<std::uint8_t>> {
        std::vector<std::uint8_t> bytes(quest_example::Session::saveLimit);
        const int count = readSave(bytes.data(), int(bytes.size()));
        if (count < 0)
            throw std::runtime_error("Browser storage unavailable or invalid");
        if (!count)
            return {};
        bytes.resize(count);
        return bytes;
    };
    storage.write = [](std::span<const std::uint8_t> bytes) {
        if (!writeSave(bytes.data(), int(bytes.size())))
            throw std::runtime_error("Browser save failed; previous save retained");
    };
    controller = std::make_unique<quest_example::Controller>(
        app, std::move(storage), [](auto, auto &) {},
        [](bool paused) {
            if (paused)
                app.pause();
            else
                app.resume();
        });
    controller->start();
    app.setSuspended(EM_ASM_INT({ return document.hidden ? 1 : 0; }) != 0);
    emscripten_set_main_loop(frame, 0, true);
    return 0;
}
