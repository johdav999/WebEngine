#pragma once
#include "quest_rewards.hpp"
#include <algorithm>

namespace example {
// Sample-owned envelope and publication policy. Storage/backup/migration are host jobs.
class SavedGame {
public:
    SavedGame(std::shared_ptr<const r::Catalog> rpg,std::shared_ptr<const q::Catalog> quests,
              bridge::Policy policy=rewardPolicy())
        :rpg_(std::move(rpg)),quests_(std::move(quests)),policy_(std::move(policy)),live_(fresh()){}
    bridge::Coordinator& game(){return *live_->game;}
    const bridge::Coordinator& game()const{return *live_->game;}
    std::uint64_t checkpoint()const{return live_->checkpoint;}
    void checkpoint(std::uint64_t value){live_->checkpoint=value;}
    std::vector<std::uint8_t> save() const {
        auto payload=live_->game->save();std::vector<std::uint8_t>b{'R','P','G','E','X','S','A','V'};
        number(b,1);number(b,live_->checkpoint);number(b,payload.size());b.insert(b.end(),payload.begin(),payload.end());number(b,hash(b));
        require(b.size()<=r::snapshotByteLimit);return b;
    }
    void load(std::span<const std::uint8_t>b){
        require(b.size()>=40&&b.size()<=r::snapshotByteLimit);
        constexpr std::string_view magic="RPGEXSAV";require(std::equal(magic.begin(),magic.end(),b.begin()));
        std::size_t cursor=8;require(read(b,cursor)==1);auto checkpoint=read(b,cursor);auto n=read(b,cursor);
        require(n==b.size()-40);auto payload=b.subspan(cursor,static_cast<std::size_t>(n));cursor+=static_cast<std::size_t>(n);
        require(read(b,cursor)==hash(b.first(b.size()-8)));
        auto candidate=fresh();candidate->checkpoint=checkpoint;candidate->game->restore(payload);
        live_.swap(candidate); // One no-throw publication joins both kernels and sample state.
    }
private:
    struct Bundle {std::uint64_t checkpoint=0;std::unique_ptr<bridge::Coordinator> game;};
    static void require(bool ok){if(!ok)throw r::Error(r::ErrorCode::InvalidSnapshot,"Invalid sample save envelope");}
    static std::uint64_t hash(std::span<const std::uint8_t>b){std::uint64_t h=14695981039346656037ull;for(auto x:b){h^=x;h*=1099511628211ull;}return h;}
    static void number(std::vector<std::uint8_t>&b,std::uint64_t n){for(unsigned i=0;i<8;++i)b.push_back(static_cast<std::uint8_t>(n>>(8*i)));}
    static std::uint64_t read(std::span<const std::uint8_t>b,std::size_t&cursor){require(b.size()-cursor>=8);std::uint64_t n=0;for(unsigned i=0;i<8;++i)n|=std::uint64_t(b[cursor++])<<(8*i);return n;}
    std::unique_ptr<Bundle> fresh() const {
        auto result=std::make_unique<Bundle>();result->game=std::make_unique<bridge::Coordinator>(rpg_,quests_,r::SessionId(1),r::ContentId("example.quest-awards"),policy_,42);return result;
    }
    std::shared_ptr<const r::Catalog> rpg_;
    std::shared_ptr<const q::Catalog> quests_;
    bridge::Policy policy_;
    std::unique_ptr<Bundle> live_;
};
} // namespace example
