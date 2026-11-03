#pragma once
#include "runtime.hpp"
#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace engine::simulator {
struct LedgerStream {
    std::string key;
    std::uint64_t sequence=0;
    Quantity credits=0,debits=0;
};
struct LedgerEntry {
    std::uint64_t id=0;
    std::string stream;
    std::uint64_t sequence=0;
    Quantity amount=0; // signed integer minor units
    Milliseconds time=0;
};
struct LedgerSnapshot {
    Quantity opening=0,balance=0;
    std::uint64_t nextEntry=1;
    std::vector<LedgerStream> archived;
    std::vector<LedgerEntry> entries;
};
// Optional single-owner-thread cash ledger. The host owns prices, currencies,
// clock, source sequences, wage accrual and cross-system command ordering.
class Ledger {
    struct State {
        LedgerSnapshot saved;
        std::vector<LedgerStream> streams;
        Quantity credits=0,debits=0;
    };
    const std::size_t historyLimit_,streamLimit_;
    const Quantity moneyLimit_;
    std::shared_ptr<const Unit> identity_=std::make_shared<Unit>();
    std::uint64_t revision_=1;
    std::unique_ptr<State> state_;
    static bool key(std::string_view s) {
        auto colon=s.find(':');
        return !s.empty() && s.size()<=64 && colon>0 && colon<s.size()-1 && s.find(':',colon+1)==s.npos &&
            std::all_of(s.begin(),s.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c==':'||c=='_'||c=='-'||c=='.';});
    }
    static LedgerStream* find(std::vector<LedgerStream>& streams,std::string_view value) {
        for(auto& s:streams)if(s.key==value)return &s;return nullptr;
    }
    static LedgerStream& ensure(std::vector<LedgerStream>& streams,const std::string& value) {
        if(auto* s=find(streams,value))return *s;
        streams.push_back({value});std::sort(streams.begin(),streams.end(),[](const auto& a,const auto& b){return a.key<b.key;});
        return *find(streams,value);
    }
    Error apply(State& s,const LedgerEntry& e) const {
        if(!key(e.stream) || !e.sequence || e.time<0 || !e.amount || e.amount>moneyLimit_ || e.amount < -moneyLimit_)return Error::InvalidQuantity;
        auto* stream=find(s.streams,e.stream);
        auto previous=stream?stream->sequence:0;
        if(e.sequence<=previous)return Error::AlreadyTerminal;
        if(e.sequence-1!=previous)return Error::InvalidState;
        if(!stream && s.streams.size()>=streamLimit_)return Error::LimitReached;
        if(e.amount<0 && -e.amount>s.saved.balance)return Error::InsufficientFunds;
        if(e.amount>0 && e.amount>moneyLimit_-s.saved.balance)return Error::Overflow;
        auto quantity=e.amount>0?e.amount:-e.amount;
        auto total=e.amount>0?s.credits:s.debits;
        if(quantity>moneyLimit_-total)return Error::Overflow;
        auto& target=ensure(s.streams,e.stream);target.sequence=e.sequence;
        if(e.amount>0){s.credits+=quantity;target.credits+=quantity;}
        else{s.debits+=quantity;target.debits+=quantity;}
        s.saved.balance+=e.amount;return Error::None;
    }
public:
    class Prepared {
        friend class Ledger;
        std::shared_ptr<const Unit> owner_;
        std::uint64_t revision_=0;
        std::unique_ptr<State> candidate_;
    public:
        Prepared()=default;
        Prepared(Prepared&&)=default;
        Prepared& operator=(Prepared&&)=default;
        Prepared(const Prepared&)=delete;
        Prepared& operator=(const Prepared&)=delete;
    };
    explicit Ledger(Quantity opening=0,std::size_t historyLimit=128,std::size_t streamLimit=16,Quantity moneyLimit=1000000000)
        :historyLimit_(historyLimit),streamLimit_(streamLimit),moneyLimit_(moneyLimit),state_(std::make_unique<State>()) {
        if(!historyLimit || historyLimit>4096 || !streamLimit || streamLimit>64 || moneyLimit<=0 ||
           moneyLimit>std::numeric_limits<Quantity>::max()/4 || opening<0 || opening>moneyLimit)throw std::invalid_argument("Invalid ledger limits or opening balance");
        state_->saved.opening=state_->saved.balance=opening;
    }
    Ledger(const Ledger&)=delete;Ledger& operator=(const Ledger&)=delete;
    Ledger(Ledger&&)=delete;Ledger& operator=(Ledger&&)=delete;
    Quantity balance() const {return state_->saved.balance;}
    Quantity credits() const {return state_->credits;}
    Quantity debits() const {return state_->debits;}
    // Borrowed views expire on publication or restore.
    const std::vector<LedgerEntry>& entries() const {return state_->saved.entries;}
    const std::vector<LedgerStream>& streams() const {return state_->streams;}
    const LedgerStream* stream(std::string_view name) const {return find(state_->streams,name);}
    std::uint64_t sequence(std::string_view name) const {auto* s=stream(name);return s?s->sequence:0;}
    LedgerSnapshot snapshot() const {return state_->saved;}
    Result<Prepared> prepare(std::string stream,std::uint64_t sequence,Quantity amount,Milliseconds time) const {
        if(revision_==std::numeric_limits<std::uint64_t>::max() || state_->saved.nextEntry==std::numeric_limits<std::uint64_t>::max())return {{},Error::Overflow};
        if(!state_->saved.entries.empty() && time<state_->saved.entries.back().time)return {{},Error::InvalidQuantity};
        auto candidate=std::make_unique<State>(*state_);
        LedgerEntry e{candidate->saved.nextEntry,std::move(stream),sequence,amount,time};
        if(auto error=apply(*candidate,e);error!=Error::None)return {{},error};
        candidate->saved.entries.push_back(std::move(e));++candidate->saved.nextEntry;
        if(candidate->saved.entries.size()>historyLimit_) {
            const auto& old=candidate->saved.entries.front();auto& archived=ensure(candidate->saved.archived,old.stream);
            archived.sequence=old.sequence;
            if(old.amount>0)archived.credits+=old.amount;else archived.debits-=old.amount;
            candidate->saved.entries.erase(candidate->saved.entries.begin());
        }
        Prepared p;p.owner_=identity_;p.revision_=revision_;p.candidate_=std::move(candidate);return {std::move(p)};
    }
    // No allocation/callback can fail after another subsystem has committed.
    // The host must preserve exclusive ledger ownership between prepare/commit.
    Status commit(Prepared&& p) noexcept {
        if(p.owner_!=identity_ || p.revision_!=revision_ || !p.candidate_)return {{},Error::StaleTransaction};
        state_.swap(p.candidate_);++revision_;p.owner_.reset();return {};
    }
    Status post(std::string stream,std::uint64_t sequence,Quantity amount,Milliseconds time) {
        auto p=prepare(std::move(stream),sequence,amount,time);if(!p)return {{},p.error};return commit(std::move(p.value));
    }
    Status restore(const LedgerSnapshot& saved) {
        if(revision_==std::numeric_limits<std::uint64_t>::max())return {{},Error::Overflow};
        if(saved.opening<0 || saved.opening>moneyLimit_ || saved.balance<0 || saved.balance>moneyLimit_ || !saved.nextEntry ||
           saved.entries.size()!=std::min<std::uint64_t>(saved.nextEntry-1,historyLimit_) || saved.archived.size()>streamLimit_)return {{},Error::InvalidSnapshot};
        auto candidate=std::make_unique<State>();candidate->saved=saved;candidate->saved.balance=saved.opening;
        std::uint64_t archivedCount=0;std::string previous;
        for(const auto& s:saved.archived) {
            if(!key(s.key) || s.key<=previous || !s.sequence || s.sequence>saved.nextEntry-1-archivedCount || s.credits<0 || s.debits<0 ||
               s.credits>moneyLimit_-candidate->credits || s.debits>moneyLimit_-candidate->debits ||
               s.sequence>std::uint64_t(s.credits+s.debits))return {{},Error::InvalidSnapshot};
            previous=s.key;archivedCount+=s.sequence;candidate->credits+=s.credits;candidate->debits+=s.debits;
            candidate->streams.push_back(s);
        }
        if(archivedCount!=saved.nextEntry-1-saved.entries.size())return {{},Error::InvalidSnapshot};
        candidate->saved.balance=saved.opening+candidate->credits-candidate->debits;
        if(candidate->saved.balance<0 || candidate->saved.balance>moneyLimit_)return {{},Error::InvalidSnapshot};
        Milliseconds time=0;auto next=archivedCount+1;
        for(const auto& e:saved.entries) {
            if(e.id!=next++ || e.time<time || apply(*candidate,e)!=Error::None)return {{},Error::InvalidSnapshot};time=e.time;
        }
        if(candidate->saved.balance!=saved.balance)return {{},Error::InvalidSnapshot};
        state_.swap(candidate);++revision_;return {};
    }
};
}
