#pragma once
#include "engine/simulator/economy.hpp"
#include <source_location>
#include <stdexcept>
#include <type_traits>
inline void economyChecks() {
    using namespace engine::simulator;
    auto check=[](bool ok,std::source_location at=std::source_location::current()){if(!ok)throw std::runtime_error("Ledger line "+std::to_string(at.line()));};
    static_assert(!std::is_copy_constructible_v<Ledger> && !std::is_copy_constructible_v<Ledger::Prepared>);
    Ledger l(1000,2,2,10000);check(l.balance()==1000 && l.entries().empty());
    auto p=l.prepare("test:cost",1,-1200,0);check(!p && p.error==Error::InsufficientFunds);check(l.balance()==1000);
    p=l.prepare("test:cost",1,-300,0);check(bool(p) && l.balance()==1000);check(bool(l.commit(std::move(p.value))));
    check(l.balance()==700 && !l.commit(std::move(p.value)));
    check(l.post("test:cost",1,-300,0).error==Error::AlreadyTerminal && l.balance()==700);
    check(!l.post("test:cost",3,-10,0));check(!l.post("invalid",1,1,0));check(!l.post("test:cost",2,0,0));
    check(!l.post("test:cost",2,std::numeric_limits<Quantity>::min(),0));
    auto stale=l.prepare("test:cost",2,-100,1);check(bool(stale));
    check(bool(l.post("test:sales",1,500,1)));check(!l.commit(std::move(stale.value)) && l.balance()==1200);
    Ledger foreign;auto quote=l.prepare("test:cost",2,-100,2);check(bool(quote));
    check(!foreign.commit(std::move(quote.value)));check(bool(l.commit(std::move(quote.value))));
    check(l.entries().size()==2 && l.snapshot().archived.size()==1 && l.sequence("test:cost")==2);
    check(!l.post("test:third",1,1,2));check(!l.post("test:cost",3,-1,-1));check(!l.post("test:cost",3,-1,0));
    auto saved=l.snapshot();Ledger restored(0,2,2,10000);check(bool(restored.restore(saved)));
    check(restored.balance()==1100 && restored.credits()==500 && restored.debits()==400);
    check(restored.post("test:cost",1,-300,3).error==Error::AlreadyTerminal);
    auto before=restored.snapshot();auto bad=saved;bad.balance++;check(!restored.restore(bad) && restored.balance()==before.balance);
    bad=saved;bad.entries[0].sequence++;check(!restored.restore(bad));
    bad=saved;bad.archived[0].sequence++;check(!restored.restore(bad));
    bad=saved;bad.entries[0].amount=100000;check(!restored.restore(bad));
    bad=saved;bad.entries.back().time=-1;check(!restored.restore(bad));
    auto old=restored.prepare("test:cost",3,-10,3);check(bool(old));check(bool(restored.restore(saved)));check(!restored.commit(std::move(old.value)));
    Ledger bounded(100,1,1,100);check(bounded.post("test:sales",1,1,0).error==Error::Overflow);
    check(bool(bounded.post("test:sales",1,-100,0)));check(bounded.balance()==0);
    check(bool(bounded.post("test:sales",2,100,0)));check(!bounded.post("test:sales",3,-1,0));
    Ledger history(500,8);for(std::uint64_t i=1;i<=300;++i)check(bool(history.post("test:flow",i,i%2?1:-1,std::int64_t(i))));
    check(history.entries().size()==8 && history.sequence("test:flow")==300 && history.balance()==500);
    auto compact=history.snapshot();Ledger again(0,8);check(bool(again.restore(compact)) && again.balance()==500);
    check(again.post("test:flow",1,1,301).error==Error::AlreadyTerminal);
    // Ledger lifetime identity survives address reuse while a quote is retained.
    alignas(Ledger) unsigned char storage[sizeof(Ledger)];
    auto* first=new(storage)Ledger(100);auto lifetime=first->prepare("test:x",1,-1,0);first->~Ledger();
    auto* second=new(storage)Ledger(100);check(!second->commit(std::move(lifetime.value)));second->~Ledger();
}
