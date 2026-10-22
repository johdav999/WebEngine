#pragma once
#include "engine/desktop_services.hpp"
namespace engine {
// Private Windows owner handle; never exposed by the public host.
DesktopFileDialogResult showWindowsFileDialog(void* owner,const DesktopFileDialogOptions&,const std::function<void()>& beforeShow);
}
