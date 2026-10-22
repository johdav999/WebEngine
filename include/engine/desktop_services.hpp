#pragma once
#include "engine/assets.hpp"
#include <filesystem>
#include <stdexcept>

namespace engine {
inline constexpr std::size_t desktopFileLimit=512ull*1024*1024;
struct DesktopFileSettings {
    std::string organization="WebEngine",application="Game";
    std::filesystem::path assetDirectory; // Empty: executable directory / assets.
    std::filesystem::path userDirectory; // Empty: OS per-user organization/application directory.
};
enum class DesktopStorage { Saves, Settings };
// Synchronous bounded byte IO. Archives/decoders retain responsibility for validation.
std::vector<std::byte> readDesktopFile(const std::filesystem::path&,std::size_t maxBytes=desktopFileLimit);
void writeDesktopFileAtomic(const std::filesystem::path&,std::span<const std::byte>,std::size_t maxBytes=desktopFileLimit);
// Optional Windows adapter. Owns paths, no device/window or process-CWD dependency.
// Relative members reject traversal, Windows aliases and resolved escapes from their root.
class DesktopFiles {
public:
    explicit DesktopFiles(const DesktopFileSettings& settings={});
    const std::filesystem::path& assetDirectory() const {return assets_;}
    const std::filesystem::path& saveDirectory() const {return saves_;}
    const std::filesystem::path& settingsDirectory() const {return settings_;}
    std::filesystem::path assetPath(const std::filesystem::path& relative) const;
    std::filesystem::path userPath(DesktopStorage,const std::filesystem::path& relative) const;
    std::vector<std::byte> readAsset(const std::filesystem::path&,std::size_t maxBytes=desktopFileLimit) const;
    std::vector<std::byte> readUser(DesktopStorage,const std::filesystem::path&,std::size_t maxBytes=desktopFileLimit) const;
    void writeUser(DesktopStorage,const std::filesystem::path&,std::span<const std::byte>,std::size_t maxBytes=desktopFileLimit) const;
    // Copies directory configuration and decoder; safe after this object is destroyed.
    // IO/decode runs synchronously inside AssetManager::update; no scene captures.
    // For off-thread IO/decode use DesktopAssetExecutor in asset_desktop.hpp.
    // Decoder must return an owning immutable resource; its input span ends with the call.
    template<class T> AssetLoader assetLoader(std::filesystem::path relative,
        std::function<std::shared_ptr<const T>(std::span<const std::byte>)> decode,std::size_t maxBytes=desktopFileLimit) const {
        if(!decode)throw std::invalid_argument("Missing desktop asset decoder");
        (void)assetPath(relative);
        if(!maxBytes||maxBytes>desktopFileLimit)throw std::invalid_argument("Invalid desktop file budget");
        return [files=*this,relative=std::move(relative),decode=std::move(decode),maxBytes](AssetLoadContext context)->AssetCancel {
            if(context.cancelled())return {};
            const auto bytes=files.readAsset(relative,maxBytes);context.progress(bytes.size(),bytes.size());
            if(!context.cancelled())context.complete<T>(decode(bytes));return {};
        };
    }
private:
    std::filesystem::path assets_,saves_,settings_;
};
enum class DesktopDialogKind { Open, Save };
enum class DesktopDialogStatus { Selected, Cancelled, Failed };
struct DesktopDialogFilter {std::string name,extensions;}; // E.g. "Models", "glb;gltf"; '*' matches all.
struct DesktopFileDialogOptions {
    DesktopDialogKind kind=DesktopDialogKind::Open;
    std::string title="Choose a file",defaultExtension;
    std::filesystem::path initialPath;
    std::vector<DesktopDialogFilter> filters;
};
struct DesktopFileDialogResult {
    DesktopDialogStatus status=DesktopDialogStatus::Cancelled;
    std::filesystem::path path;
    std::string error;
};
}
