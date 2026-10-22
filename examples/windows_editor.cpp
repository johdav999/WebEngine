#include "engine/editor_desktop.hpp"
#include "engine/desktop_runtime.hpp"
#include <iostream>
int main(int argc,char** argv){try{engine::DesktopSettings settings;settings.title="WebEngine | Scene editor";settings.width=1440;settings.height=900;engine::DesktopHost host(settings);engine::DesktopEditor editor(host);if(argc==2){const std::string value=argv[1];if(value=="--help"){std::cout<<"webengine_editor [scene.wescene]\nNative Scene Outline, Details, transforms, history, GLB import, files and isolated Play.\n";return 0;}editor.open(std::filesystem::path(std::u8string(value.begin(),value.end())));}return editor.run();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';engine::showDesktopError("WebEngine editor",e.what());return 1;}}
