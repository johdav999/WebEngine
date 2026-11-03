// Private domain codec. No platform IO, C++ object-memory dumps or callbacks.
namespace snapshot {
struct Invalid {};
inline void require(bool ok) { if (!ok) throw Invalid{}; }
template<class A> void fields(A& a, ResourceId& x) { a(x.value); }
template<class A> void fields(A& a, ProcessId& x) { a(x.value); }
template<class A> void fields(A& a, Amount& x) { a(x.resource,x.quantity); }
template<class A> void fields(A& a, Stock& x) { a(x.resource,x.total,x.reserved,x.capacity,x.reservedCapacity); }
template<class A> void fields(A& a, Reservation& x) { a(x.id,x.inventory,x.kind,x.state,x.amounts); }
template<class A> void fields(A& a, Process& x) { a(x.id,x.inputs,x.outputs,x.duration,x.capability); }
template<class A> void fields(A& a, Job& x) { a(x.id,x.process,x.input,x.output,x.state,x.station,x.worker,x.slot,x.assignment,x.deadline,x.blocked,x.order); }
template<class A> void fields(A& a, Station& x) { a(x.id,x.capabilities,x.slots); }
template<class A> void fields(A& a, Worker& x) { a(x.id,x.capabilities,x.available,x.job); }
template<class A> void fields(A& a, Customer& x) { a(x.id,x.state,x.deadline,x.order); }
template<class A> void fields(A& a, OrderLine& x) { a(x.item,x.quantity,x.process); }
template<class A> void fields(A& a, Order& x) { a(x.id,x.customer,x.output,x.state,x.abandonment,x.lines,x.jobs,x.price,x.receipt); }
template<class A> void fields(A& a, Receipt& x) { a(x.id,x.order,x.amount,x.acknowledged); }
struct Writer {
    std::vector<std::uint8_t> bytes;
    std::size_t limit;
    void number(std::uint64_t v) {
        require(bytes.size()<=limit && limit-bytes.size()>=8);
        for(int n=0;n<8;++n) bytes.push_back(static_cast<std::uint8_t>(v>>(n*8)));
    }
    template<class T> void value(const T& v) {
        if constexpr(std::is_integral_v<T> || std::is_enum_v<T>) number(static_cast<std::uint64_t>(v));
        else fields(*this,const_cast<T&>(v));
    }
    template<class Tag> void value(const Id<Tag>& v) { number(v.value); }
    void value(const std::string& v) {
        number(v.size()); require(v.size()<=limit-bytes.size()); bytes.insert(bytes.end(),v.begin(),v.end());
    }
    template<class T> void value(const std::vector<T>& v) { number(v.size()); for(const auto& x:v) value(x); }
    template<class K,class V> void value(const std::map<K,V>& v) { number(v.size()); for(const auto& [k,x]:v) { value(k);value(x); } }
    template<class... T> void operator()(const T&... v) { (value(v),...); }
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t cursor=0, countLimit;
    std::uint64_t epoch;
    std::uint64_t number() {
        require(bytes.size()-cursor>=8); std::uint64_t n=0;
        for(int i=0;i<8;++i) n|=std::uint64_t(bytes[cursor++])<<(i*8);
        return n;
    }
    template<class T> void value(T& v) {
        if constexpr(std::is_enum_v<T>) { auto n=number();require(n<=255);v=static_cast<T>(n); }
        else if constexpr(std::is_integral_v<T>) { auto n=number();require(n<=std::uint64_t(std::numeric_limits<T>::max()));v=static_cast<T>(n); }
        else fields(*this,v);
    }
    template<class Tag> void value(Id<Tag>& v) { v.value=number();v.epoch=v.value?epoch:0; }
    void value(std::string& v) { auto n=number();require(n<=128 && n<=bytes.size()-cursor);v.assign(reinterpret_cast<const char*>(bytes.data()+cursor),std::size_t(n));cursor+=std::size_t(n); }
    template<class T> void value(std::vector<T>& v) { auto n=number();require(n<=countLimit && n<=(bytes.size()-cursor)/8);v.resize(std::size_t(n));for(auto& x:v)value(x); }
    template<class K,class V> void value(std::map<K,V>& v) {
        auto n=number();require(n<=countLimit && n<=(bytes.size()-cursor)/16);
        K previous{};
        for(std::uint64_t i=0;i<n;++i) { K k;V x;value(k);value(x);require(i==0 || previous<k);require(v.emplace(k,std::move(x)).second);previous=k; }
    }
    template<class... T> void operator()(T&... v) { (value(v),...); }
};
template<class A,class S> void state(A& a,S& s) {
    a(s.nextInventory,s.nextReservation,s.nextStation,s.nextWorker,s.nextJob,s.nextCustomer,s.nextOrder,s.nextReceipt,
      s.inventories,s.reservations,s.stations,s.workers,s.jobs,s.customers,s.orders,s.receipts);
}
template<class P> std::vector<std::uint8_t> catalog(const P& p) {
    Writer w{{},p.limits.snapshotBytes};
    w(std::vector<ResourceId>(p.resources.begin(),p.resources.end()),p.state->processes);
    return w.bytes;
}
template<class P,class S> std::vector<std::uint8_t> encode(const P& p,const S& s,Milliseconds now) {
    Writer w{{},p.limits.snapshotBytes}; w.number(0x53494d53);w.number(1);
    auto definitions=catalog(p);w.number(definitions.size());
    require(definitions.size()<=w.limit-w.bytes.size());w.bytes.insert(w.bytes.end(),definitions.begin(),definitions.end());
    w(now); state(w,s);return w.bytes;
}
template<class M> void ids(const M& map,std::uint64_t next,std::size_t limit) {
    require(next>0 && map.size()<=limit);
    for(const auto& [id,x]:map) { (void)x;require(bool(id) && id.value<next); }
}
template<class S,class P> void validate(S& s,const P& p,Milliseconds now) {
    const auto& l=p.limits;
    ids(s.inventories,s.nextInventory,l.inventories);ids(s.reservations,s.nextReservation,l.reservations);
    ids(s.stations,s.nextStation,l.stations);ids(s.workers,s.nextWorker,l.workers);ids(s.jobs,s.nextJob,l.jobs);
    ids(s.customers,s.nextCustomer,l.customers);ids(s.orders,s.nextOrder,l.orders);ids(s.receipts,s.nextReceipt,l.receipts);
    using Key=std::pair<InventoryId,ResourceId>;
    std::map<Key,Quantity> held,space;
    auto add=[&](auto& counts,InventoryId inventory,const Amount& a) {
        require(s.inventories.contains(inventory) && s.inventories.at(inventory).contains(a.resource));
        auto& count=counts[{inventory,a.resource}]; require(a.quantity>0 && a.quantity<=maximum-count);count+=a.quantity;
    };
    auto bundle=[&](const auto& amounts) {
        require(!amounts.empty() && amounts.size()<=l.bundleEntries);
        std::set<ResourceId> seen;for(const auto& a:amounts) require(p.resources.contains(a.resource)&&a.quantity>0&&seen.insert(a.resource).second);
    };
    for(const auto& [id,slots]:s.inventories) {
        require(!slots.empty()&&slots.size()<=l.bundleEntries);
        for(const auto& [resource,q]:slots) require(resource==q.resource&&p.resources.contains(resource)&&q.total<=q.capacity&&q.reserved<=q.total&&q.reservedCapacity<=q.capacity-q.total);
    }
    for(const auto& [id,r]:s.reservations) {
        require(id==r.id && s.inventories.contains(r.inventory) && r.kind<=ReservationKind::OutputCapacity && r.state<=ReservationState::Released);bundle(r.amounts);
        for(const auto& a:r.amounts) {
            require(s.inventories.at(r.inventory).contains(a.resource));
            if(r.state==ReservationState::Active) add(r.kind==ReservationKind::Input?held:space,r.inventory,a);
        }
    }
    auto caps=[&](const auto& c) { require(p.capabilities(c)==Error::None); };
    std::set<JobId> occupied,employed;
    for(const auto& [id,st]:s.stations) {
        require(id==st.id&&!st.slots.empty()&&st.slots.size()<=l.slotsPerStation);caps(st.capabilities);
        for(std::size_t slot=0;slot<st.slots.size();++slot) if(auto jid=st.slots[slot]) {
            require(s.jobs.contains(jid)&&occupied.insert(jid).second);const auto& j=s.jobs.at(jid);
            require(j.station==id&&j.slot==slot&&(j.state==JobState::Assigned||j.state==JobState::Running));
        }
    }
    for(const auto& [id,w]:s.workers) {
        require(id==w.id);caps(w.capabilities);
        if(w.job) { require(s.jobs.contains(w.job)&&employed.insert(w.job).second);const auto& j=s.jobs.at(w.job);require(j.worker==id&&(j.state==JobState::Assigned||j.state==JobState::Running)); }
    }
    for(const auto& [id,j]:s.jobs) {
        require(id==j.id&&s.processes.contains(j.process)&&s.inventories.contains(j.input)&&s.inventories.contains(j.output)&&j.state<=JobState::Failed&&j.blocked<=Error::DefinitionMismatch);
        const auto& process=s.processes.at(j.process);
        for(const auto& a:process.inputs) require(s.inventories.at(j.input).contains(a.resource));
        for(const auto& a:process.outputs) require(s.inventories.at(j.output).contains(a.resource));
        if(j.order) require(s.orders.contains(j.order));
        if(j.state==JobState::Queued) require(!j.worker&&!j.station&&j.assignment==0&&j.deadline==0);
        if(j.state==JobState::Assigned||j.state==JobState::Running) {
            require(occupied.contains(id)&&employed.contains(id)&&j.assignment==id.value);
            require(P::supports(s.stations.at(j.station).capabilities,process.capability)&&P::supports(s.workers.at(j.worker).capabilities,process.capability));
            if(j.state==JobState::Assigned) { require(j.deadline==0);for(const auto& a:process.inputs)add(held,j.input,a); }
            else require(j.deadline>=now && j.deadline>=process.duration);
            for(const auto& a:process.outputs)add(space,j.output,a);
        }
        if(j.state==JobState::Completed) {
            require(j.assignment==id.value&&j.deadline<=now&&j.deadline>=process.duration);
            if(j.order&&!P::orderTerminal(s.orders.at(j.order).state)) for(const auto& a:process.outputs)add(held,j.output,a);
        }
    }
    std::set<JobId> ownedJobs;std::set<OrderId> ownedOrders;std::set<ReceiptId> ownedReceipts;
    for(const auto& [id,c]:s.customers) {
        require(id==c.id&&c.state<=CustomerState::Departed&&c.deadline>0);
        if(c.state==CustomerState::Waiting||c.state==CustomerState::InService) require(c.deadline>=now);
        if(c.order) { require(s.orders.contains(c.order)&&ownedOrders.insert(c.order).second&&s.orders.at(c.order).customer==id);require(c.state!=CustomerState::Waiting); }
        else require(c.state==CustomerState::Waiting||c.state==CustomerState::Departed);
    }
    for(const auto& [id,o]:s.orders) {
        require(id==o.id&&ownedOrders.contains(id)&&o.state<=OrderState::Failed&&o.abandonment<=AbandonmentPolicy::FinishForStock&&s.inventories.contains(o.output));
        require(!o.jobs.empty()&&o.jobs.size()<=l.jobsPerOrder&&!o.lines.empty()&&o.lines.size()<=l.bundleEntries);
        std::map<ProcessId,std::uint64_t> requested,actual;std::set<ResourceId> seen;
        for(const auto& line:o.lines) {
            require(line.quantity>0&&seen.insert(line.item).second&&s.processes.contains(line.process));const auto& recipe=s.processes.at(line.process);
            require(recipe.outputs.size()==1&&recipe.outputs[0].resource==line.item&&line.quantity%recipe.outputs[0].quantity==0);
            require(s.inventories.at(o.output).contains(line.item)&&line.quantity<=s.inventories.at(o.output).at(line.item).capacity);
            requested[line.process]=std::uint64_t(line.quantity/recipe.outputs[0].quantity);
        }
        bool allComplete=true,started=false;
        for(auto jid:o.jobs) {
            require(s.jobs.contains(jid)&&ownedJobs.insert(jid).second);const auto& j=s.jobs.at(jid);
            require(j.order==id&&j.output==o.output);++actual[j.process];
            allComplete &= j.state==JobState::Completed;started |= j.state==JobState::Running||j.state==JobState::Completed;
            if(!P::orderTerminal(o.state)) require(j.state!=JobState::Cancelled&&j.state!=JobState::Failed);
            else if(o.state==OrderState::Delivered) require(j.state==JobState::Completed);
            else require(P::terminal(j.state)||(o.state==OrderState::Abandoned&&o.abandonment==AbandonmentPolicy::FinishForStock&&j.state==JobState::Running));
        }
        require(requested==actual);
        const auto cs=s.customers.at(o.customer).state;
        if(!P::orderTerminal(o.state)) {
            require(cs==CustomerState::InService&&!o.receipt);
            require((o.state==OrderState::Ready)==allComplete);
            if(o.state==OrderState::Accepted)require(!started);
            if(o.state==OrderState::Preparing)require(started&&!allComplete);
        } else if(o.state==OrderState::Delivered) {
            require(cs==CustomerState::Served&&bool(o.receipt)&&s.receipts.contains(o.receipt)&&ownedReceipts.insert(o.receipt).second);
            const auto& r=s.receipts.at(o.receipt);require(r.order==id&&r.amount==o.price);
        } else require(cs==CustomerState::Departed&&!o.receipt);
    }
    for(const auto& [id,j]:s.jobs) if(j.order)require(ownedJobs.contains(id));
    for(const auto& [id,r]:s.receipts)require(id==r.id&&ownedReceipts.contains(id));
    for(const auto& [id,slots]:s.inventories) for(const auto& [resource,q]:slots)
        require(q.reserved==held[{id,resource}]&&q.reservedCapacity==space[{id,resource}]);
}
} // namespace snapshot
std::vector<std::uint8_t> Runtime::save() const {
    try { return snapshot::encode(*impl_,*impl_->state,impl_->now); }
    catch(const snapshot::Invalid&) { throw std::length_error("Simulator snapshot exceeds byte limit"); }
}
Status Runtime::restore(std::span<const std::uint8_t> bytes) {
    auto& p=*impl_;
    try {
        snapshot::require(bytes.size()<=p.limits.snapshotBytes);
        const auto epoch=nextEpoch();
        const auto countLimit=std::max({p.limits.resources,p.limits.inventories,p.limits.reservations,p.limits.jobs,p.limits.orders,p.limits.customers,p.limits.receipts,p.limits.workers,p.limits.stations,p.limits.bundleEntries,p.limits.jobsPerOrder,p.limits.capabilities,p.limits.slotsPerStation});
        snapshot::Reader r{bytes,0,countLimit,epoch};
        snapshot::require(r.number()==0x53494d53);
        if(r.number()!=1)return fail<Unit>(Error::UnsupportedVersion);
        auto n=r.number();snapshot::require(n<=bytes.size()-r.cursor);
        auto definitions=snapshot::catalog(p);
        if(n!=definitions.size()||!std::equal(definitions.begin(),definitions.end(),bytes.begin()+r.cursor))return fail<Unit>(Error::DefinitionMismatch);
        r.cursor+=std::size_t(n);
        auto candidate=std::make_unique<Impl::State>();candidate->eventLimit=p.limits.events;candidate->processes=p.state->processes;
        Milliseconds now=0;r(now);snapshot::state(r,*candidate);snapshot::require(r.cursor==bytes.size());
        snapshot::validate(*candidate,p,now);
        p.state.swap(candidate);p.now=now;p.epoch=epoch;
        return {};
    } catch(const snapshot::Invalid&) { return fail<Unit>(Error::InvalidSnapshot); }
      catch(const std::out_of_range&) { return fail<Unit>(Error::InvalidSnapshot); }
}
std::uint64_t Runtime::epoch() const noexcept { return impl_->epoch; }

std::uint64_t Runtime::receiptSequence() const noexcept { return impl_->state->nextReceipt - 1; }
