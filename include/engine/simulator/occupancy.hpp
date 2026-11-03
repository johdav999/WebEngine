#pragma once
#include "runtime.hpp"
#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>

namespace engine::simulator {
// Points are host-authored stable indices, not positions or scene handles.
template<class Owner=CustomerId> class OccupancyPool {
public:
    static constexpr std::size_t waiting=std::numeric_limits<std::size_t>::max();
    struct Claim {
        Owner owner;
        std::size_t point=waiting;
        std::uint64_t token=0;
        bool occupied=false;
    };
    explicit OccupancyPool(std::size_t points,std::size_t limit=128,bool compact=false)
        :points_(points),limit_(limit),compact_(compact) {
        if(!points || points>128 || limit<points || limit>4096)throw std::invalid_argument("Invalid occupancy limits");
    }
    // Disabled points retain stable indices. Reject disabling a claimed point.
    Status setEnabled(std::span<const std::uint8_t> enabled) {
        if(enabled.size()!=points_ || std::any_of(enabled.begin(),enabled.end(),[](auto n){return n>1;}))return {{},Error::InvalidQuantity};
        if(compact_ && std::find(enabled.begin(),enabled.end(),0)!=enabled.end())return {{},Error::InvalidQuantity};
        for(const auto& c:claims_)if(c.point!=waiting && !enabled[c.point])return {{},Error::InvalidSnapshot};
        std::vector<std::uint8_t> candidate(enabled.begin(),enabled.end());
        auto old=enabled_;enabled_.swap(candidate);
        try {auto status=publish(claims_);if(!status)enabled_.swap(old);return status;}
        catch(...) {enabled_.swap(old);throw;}
    }
    const std::vector<Claim>& claims() const noexcept {return claims_;}
    std::optional<Claim> claim(Owner owner) const {
        auto i=std::find_if(claims_.begin(),claims_.end(),[&](const auto& c){return c.owner==owner;});
        return i==claims_.end()?std::nullopt:std::optional<Claim>(*i);
    }
    Status request(Owner owner) {
        if(!owner)return {{},Error::InvalidId};
        if(claim(owner))return {}; // Retry preserves priority and token.
        if(claims_.size()==limit_)return {{},Error::LimitReached};
        auto next=claims_;next.push_back({owner});return publish(std::move(next));
    }
    Status release(Owner owner) {
        if(!claim(owner))return {{},Error::AlreadyTerminal};
        auto next=claims_;std::erase_if(next,[&](const auto& c){return c.owner==owner;});
        return publish(std::move(next));
    }
    Status occupy(Owner owner,std::size_t point,std::uint64_t token) {
        for(auto& c:claims_)if(c.owner==owner) {
            if(c.point==waiting || c.point!=point || c.token!=token)return {{},Error::StaleAssignment};
            c.occupied=true;return {};
        }
        return {{},Error::InvalidId};
    }
    // Host validates/rebases owners. Incoming tokens are discarded. Failed restore
    // preserves live claims; success invalidates all old arrival responses.
    Status restore(std::span<const Claim> records) {
        if(records.size()>limit_)return {{},Error::LimitReached};
        std::vector<Claim> next(records.begin(),records.end());
        std::vector<bool> used(points_,false);
        for(std::size_t i=0;i<points_;++i)used[i]=!enabled_[i];
        bool hasWaiting=false;
        for(std::size_t i=0;i<next.size();++i) {
            auto& c=next[i];
            if(!c.owner)return {{},Error::InvalidId};
            for(std::size_t j=0;j<i;++j)if(c.owner==next[j].owner)return {{},Error::InvalidSnapshot};
            if(c.point==waiting) {if(c.occupied)return {{},Error::InvalidSnapshot};hasWaiting=true;}
            else {
                if(c.point>=points_ || used[c.point] || hasWaiting)return {{},Error::InvalidSnapshot};
                used[c.point]=true;
            }
            if(compact_ && c.point!=(i<points_?i:waiting))return {{},Error::InvalidSnapshot};
        }
        if(hasWaiting && std::find(used.begin(),used.end(),false)!=used.end())return {{},Error::InvalidSnapshot};
        return publish(std::move(next),true);
    }
private:
    std::size_t points_,limit_;
    bool compact_;
    std::vector<Claim> claims_;
    std::vector<std::uint8_t> enabled_=std::vector<std::uint8_t>(points_,1);
    std::uint64_t sequence_=0;
    Status publish(std::vector<Claim> next,bool restoring=false) {
        auto sequence=sequence_;
        std::vector<bool> used(points_,false);
        for(std::size_t i=0;i<points_;++i)used[i]=!enabled_[i];
        if(!compact_)for(const auto& c:next)if(c.point!=waiting)used[c.point]=true;
        for(std::size_t i=0;i<next.size();++i) {
            auto& c=next[i];auto point=c.point;
            if(compact_)point=i<points_?i:waiting;
            else if(point==waiting) {
                auto free=std::find(used.begin(),used.end(),false);
                if(free!=used.end()){point=std::size_t(free-used.begin());*free=true;}
            }
            if(restoring || point!=c.point || !c.token) {
                if(sequence==std::numeric_limits<std::uint64_t>::max())return {{},Error::Overflow};
                c.token=++sequence;
                if(!restoring)c.occupied=false;
            }
            c.point=point;
        }
        claims_.swap(next);sequence_=sequence;return {};
    }
};
}
