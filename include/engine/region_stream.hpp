#pragma once
#include "engine/assets.hpp"
#include "engine/scene_archive.hpp"

namespace engine {
struct RegionBounds {
    Vec3 minimum{},maximum{};
    void validate() const;
    bool intersects(const RegionBounds&) const;
    bool contains(const RegionBounds&,float tolerance=0) const;
};
enum class RegionPart { Visual, Collision };
struct RegionRecord {
    std::string key,visualAsset,collisionAsset;
    RegionBounds bounds;
    std::uint32_t visualBytes=1024*1024,collisionBytes=1024*1024;
};
struct RegionManifestData {
    std::string scope="world";
    std::vector<RegionRecord> regions;
    std::vector<std::string> requiredOnRestore;
    void validate() const;
};
class RegionManifest final {
public:
    static std::shared_ptr<const RegionManifest> create(RegionManifestData);
    const RegionManifestData& data() const{return data_;}
private:explicit RegionManifest(RegionManifestData data):data_(std::move(data)){} RegionManifestData data_;
};
// Authored metadata only. No automatic ticking, asset registry, requests or resident children.
class RegionManifestComponent final : public Component {
public:
    RegionManifestComponent(std::string name,std::shared_ptr<const RegionManifest>);
    std::string_view typeName()const noexcept override{return "RegionManifestComponent";}
    const auto& manifest()const{return manifest_;}
    void configure(std::shared_ptr<const RegionManifest>);
    void validate()const override;
private:std::shared_ptr<const RegionManifest> manifest_;
};
// Immutable transport of an existing built-in scene archive. No live scene or IO callbacks.
class RegionSource final {
public:
    static std::shared_ptr<const RegionSource> create(RegionPart,std::vector<std::byte>);
    static std::shared_ptr<const RegionSource> capture(RegionPart,std::span<const Actor* const>);
    RegionPart part()const{return part_;}
    std::span<const std::byte> bytes()const{return bytes_;}
private:RegionSource(RegionPart part,std::vector<std::byte> bytes):part_(part),bytes_(std::move(bytes)){} RegionPart part_;std::vector<std::byte> bytes_;
};
struct RegionStreamSettings {
    std::uint32_t maxResidentRegions=8,maxInFlight=4,maxStarts=2,maxPublications=2,maxNodesPerPart=512,maxTrackedBodies=1024;
    std::size_t maxReservedBytes=64*1024*1024;
    float bodyPinMargin=2;
    void validate()const;
};
struct RegionInterest {std::string key;int priority=0;bool visual=true,collision=true;};
enum class RegionStatus { Absent,WaitingBudget,Loading,Ready,Failed };
struct RegionResidency {
    std::string key;RegionStatus visual=RegionStatus::Absent,collision=RegionStatus::Absent;
    bool pinned=false;std::string visualError,collisionError;
};
struct RegionStreamStats {
    std::size_t residentRegions=0,inFlight=0,reservedBytes=0,pinnedRegions=0,waitingBudget=0,failed=0;
    std::uint64_t started=0,published=0,cancelled=0,evicted=0;
    bool safetyBlocked=false,restoring=false;std::string error;
};
// One owner per manifest scope. Scene and AssetManager outlive this owner; owner-thread idle calls.
// Application drives assets.update() before update(), and gates simulation on collisionReady().
class RegionStream final {
public:
    RegionStream(Scene&,AssetManager&,std::shared_ptr<const RegionManifest>,RegionStreamSettings={});
    ~RegionStream();
    RegionStream(const RegionStream&)=delete;RegionStream& operator=(const RegionStream&)=delete;
    void update(std::span<const RegionInterest>,std::span<const RegionBounds> extraPins={});
    void retryFailed();
    bool clear(); // Refuses while simulated bodies/extra pins need resident collision.
    bool collisionReady(const RegionBounds&) const; // Bounded union coverage; false on holes/exhaustion.
    bool visualReady(std::string_view key) const;
    bool restorationReady()const;
    bool finishRestore(); // Explicit playability handoff; false while required collision is missing.
    const Node* resolve(std::string_view key,RegionPart,std::string_view sourceSavedId)const;
    std::string regionOf(ActorId)const;
    const RegionStreamStats& stats()const;
    const std::vector<RegionResidency>& residency()const;
    const std::shared_ptr<const RegionManifest>& manifest()const;
    std::vector<std::byte> checkpoint(std::span<const std::string> required,
        std::span<const std::byte> applicationData={},std::span<const RenderView> views={},
        const AssetLibrary& assets={},const ComponentCodecs& codecs={})const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace engine
