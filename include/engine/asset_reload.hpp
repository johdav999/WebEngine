#pragma once
#include "engine/assets.hpp"
#include <map>

namespace engine {
// Change detection only, not a cryptographic integrity/authentication digest.
struct AssetFingerprint {std::uint64_t hash=0,bytes=0;bool operator==(const AssetFingerprint&) const=default;};
AssetFingerprint fingerprintAsset(std::span<const std::byte>);
struct AssetSourceInfo {std::string source;std::vector<AssetId> assets;AssetProgress progress;bool baseline=false;};
// Explicit owner-thread polling. Owns probe requests and borrows the target
// manager, which must outlive it. No scene callbacks or implicit frame owner.
class AssetHotReload {
public:
    explicit AssetHotReload(AssetManager&,double intervalSeconds=.5);
    void watch(AssetId source,AssetLoader fingerprintLoader,std::vector<AssetId> affectedAssets);
    void unwatch(const AssetId& source);
    void setEnabled(bool);
    bool enabled() const {return enabled_;}
    void poll(double monotonicSeconds); // Drive even when application simulation is paused.
    std::vector<AssetSourceInfo> sources() const;
private:
    struct Source {std::vector<AssetId> assets;AssetRequest request;AssetFingerprint fingerprint;bool baseline=false,pending=false;double next=0;};
    AssetManager& assets_;AssetManager probes_;std::map<std::string,Source> sources_;
    std::map<std::string,std::shared_ptr<AssetLoader>> dispatches_; // At most 64 distinct source identities per owner.
    double interval_,last_=-1;bool enabled_=false;
};
}
