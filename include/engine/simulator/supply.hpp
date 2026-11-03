#pragma once
#include "runtime.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace engine::simulator {
using SupplyId = Id<struct SupplyTag>;
enum class SupplyState { Pending, Delivered, Cancelled };
struct SupplyOrder {
    SupplyId id;
    std::string supplier;
    InventoryId destination;
    std::vector<Amount> amounts;
    Milliseconds deadline = 0;
    SupplyState state = SupplyState::Pending;
    ReservationId capacity;
};
struct SupplySnapshot { std::uint64_t nextOrder = 1; std::vector<SupplyOrder> orders; };
struct ReorderPolicy {
    std::string supplier;
    InventoryId destination;
    ResourceId resource;
    Quantity threshold = 0, target = 0;
    Milliseconds leadTime = 0;
};

// Optional single-owner-thread helper. Runtime must outlive the board. The host
// owns supplier selection, prices, transport presentation, polling and persistence.
// Do not externally finish/forget capacity reservations owned by this board.
class SupplyBoard {
    Runtime& runtime_;
    std::uint64_t epoch_;
    std::size_t limit_;
    SupplySnapshot state_;
    static bool key(const std::string& s) {
        auto colon=s.find(':');
        return !s.empty() && s.size()<=64 && colon>0 && colon<s.size()-1 &&
            s.find(':',colon+1)==s.npos && std::all_of(s.begin(),s.end(),[](char c){
                return (c>='a' && c<='z') || (c>='0' && c<='9') || c==':' || c=='_' || c=='-' || c=='.';});
    }
    bool current() const {return epoch_==runtime_.epoch();}
    static bool same(const std::vector<Amount>& a,const std::vector<Amount>& b) {
        return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y){return x.resource==y.resource && x.quantity==y.quantity;});
    }
public:
    explicit SupplyBoard(Runtime& runtime,std::size_t limit=64):runtime_(runtime),epoch_(runtime.epoch()),limit_(limit) {
        if(!limit || limit>4096)throw std::invalid_argument("Invalid supply limit");
    }
    SupplyBoard(const SupplyBoard&)=delete;
    SupplyBoard& operator=(const SupplyBoard&)=delete;
    SupplyBoard(SupplyBoard&&)=delete;
    SupplyBoard& operator=(SupplyBoard&&)=delete;
    // Borrowed records expire on any board mutation.
    SupplySnapshot snapshot() const {return state_;}
    const std::vector<SupplyOrder>& orders() const {return state_.orders;}
    Result<SupplyOrder> order(SupplyId id) const {
        if(!current())return {{},Error::InvalidId};
        for(const auto& o:state_.orders)if(o.id==id)return {o};
        return {{},Error::InvalidId};
    }
    Result<SupplyId> place(std::string supplier,InventoryId destination,std::span<const Amount> amounts,Milliseconds leadTime) {
        if(!current())return {{},Error::InvalidId};
        if(amounts.empty() || amounts.size()>64)return {{},Error::InvalidQuantity};
        if(!key(supplier))return {{},Error::InvalidDefinition};
        if(leadTime<=0)return {{},Error::InvalidQuantity};
        if(leadTime>std::numeric_limits<Milliseconds>::max()-runtime_.time() || state_.nextOrder==std::numeric_limits<std::uint64_t>::max())return {{},Error::Overflow};
        if(state_.orders.size()>=limit_)return {{},Error::LimitReached};
        // All helper allocations precede the authoritative reservation mutation.
        auto candidate=state_;
        SupplyId id{candidate.nextOrder++,epoch_};
        candidate.orders.push_back({id,std::move(supplier),destination,{amounts.begin(),amounts.end()},runtime_.time()+leadTime,SupplyState::Pending,{}});
        auto& o=candidate.orders.back();
        std::sort(o.amounts.begin(),o.amounts.end(),[](const auto& a,const auto& b){return a.resource<b.resource;});
        auto hold=runtime_.reserveOutput(destination,amounts);
        if(!hold)return {{},hold.error};
        o.capacity=hold.value;
        std::swap(state_,candidate);
        return {id};
    }
    Status cancel(SupplyId id) {
        auto found=order(id);if(!found)return {{},found.error};
        if(found.value.state!=SupplyState::Pending)return {{},Error::AlreadyTerminal};
        auto candidate=state_;
        for(auto& o:candidate.orders)if(o.id==id)o.state=SupplyState::Cancelled;
        auto result=runtime_.release(found.value.capacity);if(!result)return result;
        std::swap(state_,candidate);return {};
    }
    // Due orders commit by (deadline, ID), once each. Failure preserves that
    // order; earlier successful deliveries remain published and are counted.
    Result<std::size_t> poll() {
        if(!current())return {0,Error::InvalidId};
        std::size_t completed=0;
        for(;;) {
            const SupplyOrder* next=nullptr;
            for(const auto& o:state_.orders)if(o.state==SupplyState::Pending && o.deadline<=runtime_.time() &&
                (!next || o.deadline<next->deadline || (o.deadline==next->deadline && o.id<next->id)))next=&o;
            if(!next)return {completed};
            auto candidate=state_;
            for(auto& o:candidate.orders)if(o.id==next->id)o.state=SupplyState::Delivered;
            auto result=runtime_.commit(next->capacity);if(!result)return {completed,result.error};
            std::swap(state_,candidate);++completed;
        }
    }
    Status forget(SupplyId id) {
        auto found=order(id);if(!found)return {{},found.error};
        if(found.value.state==SupplyState::Pending)return {{},Error::ActiveReservation};
        auto candidate=state_;
        std::erase_if(candidate.orders,[&](const auto& o){return o.id==id;});
        auto result=runtime_.forget(found.value.capacity);if(!result)return result;
        std::swap(state_,candidate);return {};
    }
    // Position includes all incoming capacity. Repeated calls cannot order the
    // same deficit twice. A zero ID means no order is needed.
    Result<SupplyId> reorder(const ReorderPolicy& p) {
        if(!current())return {{},Error::InvalidId};
        if(!key(p.supplier) || p.threshold<0 || p.target<=p.threshold || p.leadTime<=0)return {{},Error::InvalidDefinition};
        auto stock=runtime_.stock(p.destination,p.resource);if(!stock)return {{},stock.error};
        if(p.target>stock.value.capacity)return {{},Error::CapacityExceeded};
        auto position=stock.value.total+stock.value.reservedCapacity;
        if(position>p.threshold)return {};
        const Amount amount{p.resource,p.target-position};
        return place(p.supplier,p.destination,std::span{&amount,1},p.leadTime);
    }
    // Use a fresh board after core restore and host ID rebasing. Failure leaves
    // this board untouched. Runtime restore invalidates existing boards.
    Status restore(const SupplySnapshot& saved) {
        if(!current())return {{},Error::InvalidId};
        if(!state_.orders.empty() || !saved.nextOrder || saved.orders.size()>limit_)return {{},Error::InvalidSnapshot};
        std::uint64_t previous=0;
        std::vector<ReservationId> holds;
        for(const auto& o:saved.orders) {
            if(o.id.epoch!=epoch_ || o.id.value<=previous || o.id.value>=saved.nextOrder || !key(o.supplier) ||
               o.destination.epoch!=epoch_ || o.deadline<=0 || unsigned(o.state)>2)return {{},Error::InvalidSnapshot};
            previous=o.id.value;
            if(std::find(holds.begin(),holds.end(),o.capacity)!=holds.end())return {{},Error::InvalidSnapshot};
            holds.push_back(o.capacity);
            auto r=runtime_.reservation(o.capacity);
            auto expected=o.state==SupplyState::Pending?ReservationState::Active:o.state==SupplyState::Delivered?ReservationState::Committed:ReservationState::Released;
            if(!r || r.value.inventory!=o.destination || r.value.kind!=ReservationKind::OutputCapacity || r.value.state!=expected ||
               !same(o.amounts,r.value.amounts) || (o.state==SupplyState::Delivered && o.deadline>runtime_.time()))return {{},Error::InvalidSnapshot};
        }
        auto candidate=saved;std::swap(state_,candidate);return {};
    }
};
}
