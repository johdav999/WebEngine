#include "windows_content_app.hpp"
#include <iostream>
int main(int argc,char** argv){try{
    engine::DesktopSettings settings;settings.title="WebEngine | Content iteration";settings.width=1280;settings.height=800;engine::DesktopHost host(settings);
    content_example::WindowsContentApp app(host,argc>1?std::filesystem::absolute(argv[1]):std::filesystem::path{},argc>2?std::filesystem::absolute(argv[2]):std::filesystem::path{});
    return host.run(app.callbacks());
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
