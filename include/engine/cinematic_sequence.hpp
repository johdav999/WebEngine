#pragma once
#include "engine/math.hpp"
#include <memory>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine {
enum class CinematicTrackType { Transform, Camera, Animation, Audio, Effect };
enum class CinematicInterpolation { Linear, Smooth, Step };
struct CinematicKey {
    double time=0;
    Transform transform;
    float lens=pi/3; // Camera's existing projection: vertical radians or ortho height.
    CinematicInterpolation interpolation=CinematicInterpolation::Smooth;
};
struct CinematicTrack {
    std::string id,name="Track",target,resource; // Saved node ID; animation clip name or audio URI.
    CinematicTrackType type=CinematicTrackType::Transform;
    double start=0,end=8,offset=0;
    float speed=1,volume=1;
    bool loop=false,enabled=true;
    std::vector<CinematicKey> keys;
};
struct CinematicSequenceData {
    std::string name="Cinematic";
    double duration=8;
    std::uint32_t framesPerSecond=30;
    std::vector<CinematicTrack> tracks;
};
class CinematicSequence final {
public:
    static std::shared_ptr<const CinematicSequence> create(CinematicSequenceData);
    const CinematicSequenceData& data() const { return data_; }
    bool active(const CinematicTrack&,double time) const;
    CinematicKey sample(std::size_t track,double time) const;
private:
    explicit CinematicSequence(CinematicSequenceData data):data_(std::move(data)) {}
    CinematicSequenceData data_;
};
class CinematicSequenceArchive {
public:
    // WECINE1 bounded authoring document; saved references, never executable callbacks.
    static std::string save(const CinematicSequence&);
    static std::shared_ptr<const CinematicSequence> load(std::string_view);
};
enum class CinematicPlaybackState { Stopped, Paused, Playing };
// Portable clock/evaluation policy; no Scene, callback, device or renderer ownership.
class CinematicPlayer {
public:
    explicit CinematicPlayer(std::shared_ptr<const CinematicSequence>);
    const auto& sequence() const { return sequence_; }
    double time() const { return time_; }
    CinematicPlaybackState state() const { return state_; }
    bool looping=false;
    void seek(double);
    void play();
    void pause() { state_=CinematicPlaybackState::Paused; }
    void stop() { time_=0;state_=CinematicPlaybackState::Stopped; }
    void advance(double seconds); // Explicit eligible host delta, 0..0.25 seconds.
private:
    std::shared_ptr<const CinematicSequence> sequence_;
    double time_=0;
    CinematicPlaybackState state_=CinematicPlaybackState::Stopped;
};
}
