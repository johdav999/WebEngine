#pragma once
// Optional WebEngine::WindowsRuntime. No SDL, renderer or Windows headers.
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine {
enum class DesktopLogLevel { Info, Warning, Error };
struct DesktopLogSettings {
    std::string organization="WebEngine",application="Game";
    std::filesystem::path directory; // Empty: LocalAppData / organization / application / logs.
    bool redirectStandardStreams=false,crashDumps=true;
    unsigned retainedLogs=8,retainedDumps=3;
};
// One session per process, owned by the entry point. Outlives host/devices/user
// callbacks. Installs process crash/terminate hooks and restores them on teardown.
// Crash writes are best effort and do not invoke application callbacks.
class DesktopSessionLog {
public:
    explicit DesktopSessionLog(const DesktopLogSettings& settings={});
    ~DesktopSessionLog();
    DesktopSessionLog(const DesktopSessionLog&)=delete;
    DesktopSessionLog& operator=(const DesktopSessionLog&)=delete;
    const std::filesystem::path& file() const;
    const std::filesystem::path& directory() const;
    void write(DesktopLogLevel,std::string_view message) noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
std::filesystem::path desktopExecutablePath();
struct DesktopPackageReport {
    std::string version;
    std::vector<std::string> errors;
    std::size_t verifiedFiles=0;
    bool ready() const{return errors.empty();}
};
// Bounded SHA-256/size checks of package.manifest, with root-confined paths.
// Integrity detection, not a publisher signature or an OS/driver compatibility claim.
DesktopPackageReport inspectDesktopPackage(const std::filesystem::path& directory);
// Native UTF-8 failure dialog for GUI entry points. Log path is included for support.
void showDesktopError(std::string_view title,std::string_view message,const std::filesystem::path& log={}) noexcept;
}
