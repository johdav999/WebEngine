#pragma once
#include "engine/animation.hpp"
#include <memory>
#include <string>

namespace engine {
// Pose/event transport only. No solver state, callbacks, resources or live handles.
struct ReplayFrame {
    double time=0;
    std::vector<JointPose> poses;
    std::vector<float> values;
    bool cut=false; // Hold the preceding pose until this frame (teleports/resets).
};
struct ReplayEvent { double time=0;std::string name,payload; };
struct ReplayData {
    std::string name="Recording",context;
    std::vector<std::string> poses,values;
    std::vector<ReplayFrame> frames;
    std::vector<ReplayEvent> events;
};
JointPose replayPose(const Mat4&); // Positive TRS, rejects shear/reflection/singular data.
Transform replayTransform(const JointPose&);
class ReplayRecording {
public:
    static std::shared_ptr<const ReplayRecording> create(ReplayData);
    const ReplayData& data() const { return data_; }
    double duration() const { return data_.frames.back().time; }
    ReplayFrame sample(double time) const;
    // Diagnostics only; (begin,end], ordered, no callback dispatch or consumption.
    std::span<const ReplayEvent> eventsBetween(double begin,double end) const;
private:
    explicit ReplayRecording(ReplayData d):data_(std::move(d)){}
    ReplayData data_;
};
class ReplayRecorder {
public:
    // Schema only; frames/events must be empty. Caller owns capture timing.
    explicit ReplayRecorder(ReplayData schema);
    bool capture(ReplayFrame); // Budget exhaustion returns false without mutation.
    bool event(ReplayEvent);   // Events must refer to an already captured time.
    const ReplayData& data() const { return data_; }
    std::shared_ptr<const ReplayRecording> finish() const;
private:
    ReplayData data_;
};
class ReplayArchive {
public:
    static std::string save(const ReplayRecording&);
    static std::shared_ptr<const ReplayRecording> load(std::string_view);
};
class ReplayPlayer {
public:
    explicit ReplayPlayer(std::shared_ptr<const ReplayRecording>);
    const auto& recording() const { return recording_; }
    double time() const { return time_; }
    bool playing() const { return playing_; }
    void seek(double); // Pauses; never dispatches events.
    void play();void pause(){playing_=false;}
    void stop(){time_=0;playing_=false;}
    void setSpeed(float);float speed() const{return speed_;}
    bool looping=false;
    void advance(double); // Explicit eligible clock, 0..0.25s; end pauses.
private:
    std::shared_ptr<const ReplayRecording> recording_;
    double time_=0;float speed_=1;bool playing_=false;
};
}
