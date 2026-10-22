#pragma once
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace engine::desktop_audio {
std::filesystem::path streamPath(const std::string& uri);
class Stream {
public:
    Stream(std::filesystem::path,double offset,bool loop);
    ~Stream();
    Stream(const Stream&)=delete;
    // Called only by the mixer; underruns produce silence without advancing source time.
    bool render(std::span<float> stereo,float rate) noexcept;
    bool seek(double) noexcept;
    double position() const noexcept;
    bool playing() const noexcept;
    std::size_t buffered() const noexcept;
    std::uint64_t decoded() const noexcept;
    std::uint64_t underruns() const noexcept;
    std::string error() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
