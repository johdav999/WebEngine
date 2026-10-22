#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace engine {
enum class ProfileCategory : std::uint8_t { Frame,Simulation,Physics,Animation,AI,Loading,Render,Other,Count };
const char* profileCategoryName(ProfileCategory) noexcept;
enum class ProfileRecordKind { Frame,Task };
struct ProfileEvent {
    std::array<char,48> name{};
    std::array<char,96> subject{};
    double startMs=0,durationMs=0;
    std::int16_t parent=-1;
    std::uint8_t depth=0;
    ProfileCategory category=ProfileCategory::Other;
};
struct ProfileRecord {
    std::uint64_t sequence=0;
    double startMs=0,durationMs=0,intervalMs=0;
    ProfileRecordKind kind=ProfileRecordKind::Frame;
    std::uint16_t count=0;
    std::uint32_t dropped=0;
    bool truncated=false;
    std::array<ProfileEvent,256> events{};
};
// Optional owner-thread CPU capture. Allocate on the heap: fixed storage is about
// 5 MiB. Disabled instrumentation reads no clocks and allocates nothing.
class FrameTimeline {
public:
    using Clock=double(*)() noexcept;
    static constexpr std::size_t capacity=120,eventCapacity=256,maxDepth=32;
    explicit FrameTimeline(Clock clock=nullptr);
    FrameTimeline(const FrameTimeline&)=delete;
    FrameTimeline& operator=(const FrameTimeline&)=delete;
    void setRecording(bool);
    bool recording() const {return recording_;}
    void reset();
    std::size_t size() const {return count_;}
    const ProfileRecord& record(std::size_t chronologicalIndex) const;
    std::string json() const;
    std::string traceJson() const; // Chrome/Perfetto complete CPU events, microseconds.
private:
    friend class ProfileScope;
    friend class ProfileFrame;
    struct Token {std::uint64_t generation=0;std::uint16_t index=65535;};
    bool begin(ProfileRecordKind);
    void finish();
    Token enter(ProfileCategory,std::string_view,std::string_view);
    void leave(Token);
    double now();
    Clock clock_;
    double origin_=0,lastNow_=0,lastFrame_=0;
    bool recording_=false,active_=false,haveFrame_=false;
    std::uint64_t sequence_=0,generation_=0,dropped_=0;
    std::size_t count_=0,depth_=0;
    std::array<std::uint16_t,maxDepth> stack_{};
    ProfileRecord current_;
    std::array<ProfileRecord,capacity> records_{};
};
// Borrows the recorder on this thread until destruction. Only one session per
// thread; all scopes must end before the session and recorder are released.
class ProfileSession {
public:
    explicit ProfileSession(FrameTimeline&);
    ~ProfileSession();
    ProfileSession(const ProfileSession&)=delete;
    ProfileSession& operator=(const ProfileSession&)=delete;
private:FrameTimeline* timeline_;
};
class ProfileScope {
public:
    ProfileScope(ProfileCategory,std::string_view name,std::string_view subject={});
    ~ProfileScope();
    ProfileScope(const ProfileScope&)=delete;
    ProfileScope& operator=(const ProfileScope&)=delete;
private:FrameTimeline* timeline_=nullptr;FrameTimeline::Token token_;bool task_=false;
};
class ProfileFrame {
public:
    ProfileFrame();
    ~ProfileFrame();
    ProfileFrame(const ProfileFrame&)=delete;
    ProfileFrame& operator=(const ProfileFrame&)=delete;
private:FrameTimeline* timeline_=nullptr;FrameTimeline::Token token_;bool owns_=false;
};
}
