#pragma once
#include "engine/cinematic_sequence.hpp"
#include "engine/scene_archive.hpp"
#include "engine/audio.hpp"

namespace engine {
class CinematicSequenceEditor {
public:
    explicit CinematicSequenceEditor(std::shared_ptr<const CinematicSequence>);
    const auto& asset() const { return asset_; }
    bool replace(CinematicSequenceData);
    bool undo();bool redo();
    std::size_t undoCount() const { return undo_.size(); }
    std::size_t redoCount() const { return redo_.size(); }
    bool dirty() const { return asset_!=saved_; }
    void markSaved() { saved_=asset_; }
private:
    std::shared_ptr<const CinematicSequence> asset_,saved_;
    std::vector<std::shared_ptr<const CinematicSequence>> undo_,redo_;
};
// Isolated optional authoring owner. Renderer must release its scene/views before
// this object is destroyed. Source is borrowed only during construction.
class CinematicPreview final {
public:
    CinematicPreview(const Scene&,std::span<const RenderView>,std::shared_ptr<const CinematicSequence>,
        std::unique_ptr<AudioBackend> backend={},const AssetLibrary& assets={},const ComponentCodecs& codecs={});
    ~CinematicPreview();
    Scene& scene();
    std::span<const RenderView> views() const;
    CinematicPlayer& player();
    const CinematicPlayer& player() const;
    AudioSystem* audio();
    void seek(double); // Silent, pauses, reconstructs only bounded active FX windows.
    void play();void pause();void stop();
    void advance(double seconds); // 0..0.25; only the supplied timeline clock advances.
    std::size_t particleCount() const;
    std::uint32_t replaySteps() const;
    const std::string& error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
