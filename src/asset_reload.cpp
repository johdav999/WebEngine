#include "engine/asset_reload.hpp"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace engine {
AssetFingerprint fingerprintAsset(std::span<const std::byte> bytes){std::uint64_t hash=14695981039346656037ull;for(auto b:bytes){hash^=std::to_integer<unsigned char>(b);hash*=1099511628211ull;}return {hash,bytes.size()};}
AssetHotReload::AssetHotReload(AssetManager& assets,double interval):assets_(assets),interval_(interval){if(!std::isfinite(interval)||interval<.1||interval>3600)throw std::invalid_argument("Invalid source polling interval");}
void AssetHotReload::watch(AssetId id,AssetLoader loader,std::vector<AssetId> assets){
    if(!loader||assets.empty()||assets.size()>64||sources_.contains(id.value())||(!dispatches_.contains(id.value())&&dispatches_.size()>=64))throw std::invalid_argument("Invalid/duplicate source watch or source budget");
    for(const auto& key:assets)(void)assets_.dependencies(key);
    // Registration dispatch holds no watcher borrow; rewatch replaces it only
    // after the old probe generation has been cancelled by unwatch.
    auto at=dispatches_.find(id.value());
    if(at==dispatches_.end()){auto state=std::make_shared<AssetLoader>(std::move(loader));probes_.registerAsset<AssetFingerprint>(id,[state](AssetLoadContext c){return (*state)(c);});dispatches_.emplace(id.value(),std::move(state));}
    else *at->second=std::move(loader);
    Source source;source.assets=std::move(assets);
    sources_.emplace(id.value(),std::move(source));
}
void AssetHotReload::unwatch(const AssetId& id){auto at=sources_.find(id.value());if(at==sources_.end())return;probes_.unload(id);sources_.erase(at);}
void AssetHotReload::setEnabled(bool value){if(enabled_==value)return;enabled_=value;for(auto& [key,source]:sources_){if(!value){source.request.cancel();probes_.unload(AssetId(key));source.pending=false;}source.next=0;}}
void AssetHotReload::poll(double now){
    if(!std::isfinite(now)||now<0||now<last_)throw std::invalid_argument("Hot reload requires monotonic time");last_=now;if(!enabled_)return;
    probes_.update();std::vector<AssetId> changed;
    for(auto& [key,source]:sources_){(void)key;
        if(source.pending){const auto p=source.request.progress();if(p.status==AssetStatus::Ready){const auto next=*source.request.get<AssetFingerprint>();
                if(!source.baseline||source.fingerprint!=next)for(const auto& asset:source.assets)if(std::find(changed.begin(),changed.end(),asset)==changed.end())changed.push_back(asset);
                source.fingerprint=next;source.baseline=true;source.pending=false;source.next=now+interval_;
            }else if(p.status==AssetStatus::Failed||p.status==AssetStatus::Cancelled){source.pending=false;source.next=now+interval_;}}
        if(!source.pending&&now>=source.next){const AssetId probe(key);probes_.unload(probe);source.request=probes_.load<AssetFingerprint>(probe);source.pending=true;}
    }
    if(!changed.empty())assets_.reload(changed);
}
std::vector<AssetSourceInfo> AssetHotReload::sources() const {std::vector<AssetSourceInfo> out;for(const auto& [key,s]:sources_)out.push_back({key,s.assets,s.request.progress(),s.baseline});return out;}
}
