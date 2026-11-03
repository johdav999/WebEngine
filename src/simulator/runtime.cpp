#include "engine/simulator/runtime.hpp"
#include <atomic>
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace engine::simulator {
namespace {
constexpr auto maximum = std::numeric_limits<Quantity>::max();
bool validKey(std::string_view s) {
    if (s.empty() || s.size() > 128) return false;
    auto separator = s.find(':');
    if (separator == s.npos || separator == 0 || separator + 1 == s.size()) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.' || c == ':')) return false;
    return s.find(':', separator + 1) == s.npos;
}
std::uint64_t nextEpoch() {
    static std::atomic<std::uint64_t> next{1};
    auto current = next.load();
    for (;;) {
        if (current == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Simulator runtime identity exhausted");
        if (next.compare_exchange_weak(current, current + 1)) return current;
    }
}
template<class T> Result<T> fail(Error e) { return {{}, e}; }
}

std::string_view describe(Error e) noexcept {
    switch (e) {
    case Error::None: return "Success";
    case Error::InvalidQuantity: return "Quantity or elapsed time is invalid";
    case Error::InvalidId: return "Unknown or foreign runtime handle";
    case Error::UnknownResource: return "Resource is not defined in this inventory";
    case Error::DuplicateResource: return "Bundle contains a duplicate resource";
    case Error::InsufficientStock: return "Insufficient unreserved stock";
    case Error::CapacityExceeded: return "Insufficient unreserved output capacity";
    case Error::LimitReached: return "Configured record, bundle or event limit reached";
    case Error::AlreadyTerminal: return "Record or source sequence has already completed";
    case Error::ActiveReservation: return "Cannot forget an active reservation";
    case Error::Overflow: return "Time, identity or money limit would be exceeded";
    case Error::InvalidDefinition: return "Invalid or duplicate process/capability definition";
    case Error::InvalidState: return "Operation is not valid in the current state";
    case Error::StaleAssignment: return "Movement result does not match the active assignment";
    case Error::NoStation: return "No compatible free station slot";
    case Error::NoWorker: return "No compatible available worker";
    case Error::NotQueueHead: return "Customer is not first in the waiting queue";
    case Error::InvalidSnapshot: return "Invalid or inconsistent simulator snapshot";
    case Error::UnsupportedVersion: return "Unsupported simulator snapshot version";
    case Error::DefinitionMismatch: return "Snapshot definitions do not match this runtime";
    case Error::InsufficientFunds: return "Insufficient available cash";
    case Error::StaleTransaction: return "Ledger transaction is stale or belongs to another ledger";
    }
    return "Unknown error";
}

struct Runtime::Impl {
    struct State {
        std::map<InventoryId, std::map<ResourceId, Stock>> inventories;
        std::map<ReservationId, Reservation> reservations;
        std::vector<Event> events;
        std::uint64_t nextInventory = 1, nextReservation = 1;
        std::map<ProcessId, Process> processes;
        std::map<StationId, Station> stations;
        std::map<WorkerId, Worker> workers;
        std::map<JobId, Job> jobs;
        std::uint64_t nextStation = 1, nextWorker = 1, nextJob = 1;
        std::map<CustomerId, Customer> customers;
        std::map<OrderId, Order> orders;
        std::map<ReceiptId, Receipt> receipts;
        std::uint64_t nextCustomer = 1, nextOrder = 1, nextReceipt = 1;
        std::size_t eventLimit = 0;
    };
    std::uint64_t epoch = nextEpoch();
    const Limits limits;
    std::set<ResourceId> resources;
    Milliseconds now = 0;
    std::unique_ptr<State> state = std::make_unique<State>();
    explicit Impl(Limits l) : limits(l) { state->eventLimit = l.events; }
    struct OutboxFull {};
    static void push(State& s, Event event) {
        if (s.events.size() == s.eventLimit) throw OutboxFull{};
        s.events.push_back(event);
    }
    Error bundle(std::span<const Amount> amounts, bool capacity = false) const {
        if (amounts.empty()) return Error::InvalidQuantity;
        if (amounts.size() > limits.bundleEntries) return Error::LimitReached;
        std::set<ResourceId> seen;
        for (const auto& a : amounts) {
            if (a.quantity < 0 || (!capacity && a.quantity == 0)) return Error::InvalidQuantity;
            if (!resources.contains(a.resource)) return Error::UnknownResource;
            if (!seen.insert(a.resource).second) return Error::DuplicateResource;
        }
        return Error::None;
    }
    Error eventRoom(std::size_t count = 1) const {
        return count > limits.events - state->events.size() ? Error::LimitReached : Error::None;
    }
    Error capabilities(std::span<const std::string> values) const {
        if (values.empty() || values.size() > limits.capabilities) return Error::InvalidDefinition;
        std::set<std::string> seen;
        for (const auto& value : values)
            if (!validKey(value) || !seen.insert(value).second) return Error::InvalidDefinition;
        return Error::None;
    }
    static bool terminal(JobState s) {
        return s == JobState::Completed || s == JobState::Cancelled || s == JobState::Failed;
    }
    static bool supports(const std::vector<std::string>& caps, const std::string& required) {
        return std::find(caps.begin(), caps.end(), required) != caps.end();
    }
    static void emit(State& s, EventKind kind, Milliseconds time, const Job& j) {
        push(s, {kind, time, j.input, {}, j.id, j.station, j.worker, {}, j.order});
    }
    static void start(State& s, Job& j, Milliseconds time) {
        const auto& process = s.processes.at(j.process);
        for (const auto& a : process.inputs) {
            auto& stock = s.inventories.at(j.input).at(a.resource);
            stock.total -= a.quantity;
            stock.reserved -= a.quantity;
        }
        j.state = JobState::Running;
        j.deadline = time + process.duration; // caller validated overflow
        emit(s, EventKind::JobStarted, time, j);
        if (j.order && !orderTerminal(s.orders.at(j.order).state))
            s.orders.at(j.order).state = OrderState::Preparing;
    }
    static void stop(State& s, Job& j, JobState terminalState, Milliseconds time) {
        const auto& process = s.processes.at(j.process);
        if (j.state == JobState::Assigned || j.state == JobState::Running) {
            if (j.state == JobState::Assigned)
                for (const auto& a : process.inputs)
                    s.inventories.at(j.input).at(a.resource).reserved -= a.quantity;
            for (const auto& a : process.outputs) {
                auto& stock = s.inventories.at(j.output).at(a.resource);
                stock.reservedCapacity -= a.quantity;
                if (terminalState == JobState::Completed) {
                    stock.total += a.quantity;
                    if (j.order && !orderTerminal(s.orders.at(j.order).state)) stock.reserved += a.quantity;
                }
            }
            s.stations.at(j.station).slots.at(j.slot) = {};
            s.workers.at(j.worker).job = {};
        }
        j.state = terminalState;
        j.blocked = Error::None;
        emit(s, terminalState == JobState::Completed ? EventKind::JobCompleted :
             terminalState == JobState::Failed ? EventKind::JobFailed : EventKind::JobCancelled, time, j);
        if (j.order) syncOrder(s, s.orders.at(j.order), time);
    }
    static bool orderTerminal(OrderState state) {
        return state == OrderState::Delivered || state == OrderState::Abandoned || state == OrderState::Failed;
    }
    static void endOrder(State& s, Order& order, OrderState terminalState, Milliseconds time) {
        order.state = terminalState; // suppress recursive failure propagation
        s.customers.at(order.customer).state = CustomerState::Departed;
        for (auto id : order.jobs) {
            auto& job = s.jobs.at(id);
            if (job.state == JobState::Completed) {
                for (const auto& a : s.processes.at(job.process).outputs)
                    s.inventories.at(job.output).at(a.resource).reserved -= a.quantity;
            } else if (!terminal(job.state)) {
                if (terminalState == OrderState::Abandoned &&
                    order.abandonment == AbandonmentPolicy::FinishForStock && job.state == JobState::Running)
                    continue;
                stop(s,job,JobState::Cancelled,time);
            }
        }
        push(s,{terminalState == OrderState::Abandoned ? EventKind::OrderAbandoned : EventKind::OrderFailed,
            time, {}, {}, {}, {}, {}, order.customer, order.id});
    }
    static void syncOrder(State& s, Order& order, Milliseconds time) {
        if (orderTerminal(order.state)) return;
        bool ready = true;
        for (auto id : order.jobs) {
            const auto state = s.jobs.at(id).state;
            if (state == JobState::Cancelled || state == JobState::Failed) {
                endOrder(s,order,OrderState::Failed,time); return;
            }
            if (state != JobState::Completed) ready = false;
        }
        if (ready && order.state != OrderState::Ready) {
            order.state = OrderState::Ready;
            push(s,{EventKind::OrderReady,time,{}, {}, {}, {}, {},order.customer,order.id});
        }
    }
    static void leave(State& s, Customer& customer, Milliseconds time) {
        if (customer.order) endOrder(s,s.orders.at(customer.order),OrderState::Abandoned,time);
        else {
            customer.state = CustomerState::Departed;
            push(s,{EventKind::CustomerLeft,time,{}, {}, {}, {}, {},customer.id});
        }
    }
    // Stage a bounded state copy. Allocation failure cannot partially publish stock,
    // reservation records or events. swap is the only publication boundary.
    template<class F> Error publish(F mutate) {
        auto candidate = std::make_unique<State>(*state);
        try { mutate(*candidate); } catch (const OutboxFull&) { return Error::LimitReached; }
        state.swap(candidate);
        return Error::None;
    }
};

Runtime::Runtime(std::span<const ResourceId> resources, Limits limits)
    : impl_(std::make_unique<Impl>(limits)) {
    if (!limits.resources || !limits.inventories || !limits.reservations ||
        !limits.bundleEntries || !limits.events || !limits.processes || !limits.stations ||
        !limits.workers || !limits.jobs || !limits.slotsPerStation || !limits.capabilities ||
        !limits.transitionsPerAdvance || !limits.customers || !limits.orders || !limits.receipts ||
        !limits.jobsPerOrder || !limits.snapshotBytes || resources.empty() || resources.size() > limits.resources)
        throw std::invalid_argument("Invalid simulator limits or resource count");
    for (const auto& r : resources)
        if (!validKey(r.value) || !impl_->resources.insert(r).second)
            throw std::invalid_argument("Invalid or duplicate namespaced simulator resource key");
}
Runtime::~Runtime() = default;

Result<InventoryId> Runtime::createInventory(std::span<const Amount> capacities) {
    auto& p = *impl_;
    if (auto e = p.bundle(capacities, true); e != Error::None) return fail<InventoryId>(e);
    if (p.state->inventories.size() >= p.limits.inventories) return fail<InventoryId>(Error::LimitReached);
    if (auto e = p.eventRoom(); e != Error::None) return fail<InventoryId>(e);
    if (p.state->nextInventory == std::numeric_limits<std::uint64_t>::max()) return fail<InventoryId>(Error::Overflow);
    InventoryId id{p.state->nextInventory, p.epoch};
    if (auto error = p.publish([&](auto& s) {
        auto& slots = s.inventories[id];
        for (const auto& a : capacities) slots.emplace(a.resource, Stock{a.resource, 0, 0, a.quantity, 0});
        ++s.nextInventory;
        Impl::push(s, {EventKind::InventoryCreated, p.now, id, {}});
    }); error != Error::None) return fail<InventoryId>(error);
    return {id};
}
Result<Stock> Runtime::stock(InventoryId id, const ResourceId& resource) const {
    const auto& inventories = impl_->state->inventories;
    auto i = inventories.find(id);
    if (i == inventories.end()) return fail<Stock>(Error::InvalidId);
    auto r = i->second.find(resource);
    if (r == i->second.end()) return fail<Stock>(Error::UnknownResource);
    return {r->second};
}
Status Runtime::add(InventoryId id, std::span<const Amount> amounts) {
    auto& p = *impl_;
    if (auto e = p.bundle(amounts); e != Error::None) return fail<Unit>(e);
    if (!p.state->inventories.contains(id)) return fail<Unit>(Error::InvalidId);
    if (auto e = p.eventRoom(); e != Error::None) return fail<Unit>(e);
    for (const auto& a : amounts) {
        auto r = stock(id, a.resource);
        if (!r) return fail<Unit>(r.error);
        if (a.quantity > r.value.capacity - r.value.total - r.value.reservedCapacity)
            return fail<Unit>(Error::CapacityExceeded);
    }
    if (auto error = p.publish([&](auto& s) {
        for (const auto& a : amounts) s.inventories.at(id).at(a.resource).total += a.quantity;
        Impl::push(s, {EventKind::StockAdded, p.now, id, {}});
    }); error != Error::None) return fail<Unit>(error);
    return {};
}
Status Runtime::remove(InventoryId id, std::span<const Amount> amounts) {
    auto& p = *impl_;
    if (auto e = p.bundle(amounts); e != Error::None) return fail<Unit>(e);
    if (!p.state->inventories.contains(id)) return fail<Unit>(Error::InvalidId);
    if (auto e = p.eventRoom(); e != Error::None) return fail<Unit>(e);
    for (const auto& a : amounts) {
        auto r = stock(id, a.resource);
        if (!r) return fail<Unit>(r.error);
        if (a.quantity > r.value.available()) return fail<Unit>(Error::InsufficientStock);
    }
    if (auto e = p.publish([&](auto& s) {
        for (const auto& a : amounts) s.inventories.at(id).at(a.resource).total -= a.quantity;
        Impl::push(s, {EventKind::StockRemoved, p.now, id});
    }); e != Error::None) return fail<Unit>(e);
    return {};
}
Status Runtime::transfer(InventoryId source, InventoryId destination, std::span<const Amount> amounts) {
    auto& p = *impl_;
    if (auto e = p.bundle(amounts); e != Error::None) return fail<Unit>(e);
    if (!p.state->inventories.contains(source) || !p.state->inventories.contains(destination)) return fail<Unit>(Error::InvalidId);
    if (source == destination) return fail<Unit>(Error::InvalidState);
    if (auto e = p.eventRoom(2); e != Error::None) return fail<Unit>(e);
    for (const auto& a : amounts) {
        auto from = stock(source, a.resource), to = stock(destination, a.resource);
        if (!from) return fail<Unit>(from.error);
        if (!to) return fail<Unit>(to.error);
        if (a.quantity > from.value.available()) return fail<Unit>(Error::InsufficientStock);
        if (a.quantity > to.value.capacity - to.value.total - to.value.reservedCapacity) return fail<Unit>(Error::CapacityExceeded);
    }
    if (auto e = p.publish([&](auto& s) {
        for (const auto& a : amounts) {
            s.inventories.at(source).at(a.resource).total -= a.quantity;
            s.inventories.at(destination).at(a.resource).total += a.quantity;
        }
        Impl::push(s, {EventKind::StockTransferred, p.now, source});
        Impl::push(s, {EventKind::StockTransferred, p.now, destination});
    }); e != Error::None) return fail<Unit>(e);
    return {};
}
Result<ReservationId> Runtime::reserve(InventoryId id, std::span<const Amount> amounts) {
    return reserveImpl(id, amounts, ReservationKind::Input);
}
Result<ReservationId> Runtime::reserveOutput(InventoryId id, std::span<const Amount> amounts) {
    return reserveImpl(id, amounts, ReservationKind::OutputCapacity);
}
Result<ReservationId> Runtime::reserveImpl(InventoryId id, std::span<const Amount> amounts, ReservationKind kind) {
    auto& p = *impl_;
    if (auto e = p.bundle(amounts); e != Error::None) return fail<ReservationId>(e);
    if (!p.state->inventories.contains(id)) return fail<ReservationId>(Error::InvalidId);
    if (p.state->reservations.size() >= p.limits.reservations) return fail<ReservationId>(Error::LimitReached);
    if (auto e = p.eventRoom(); e != Error::None) return fail<ReservationId>(e);
    if (p.state->nextReservation == std::numeric_limits<std::uint64_t>::max()) return fail<ReservationId>(Error::Overflow);
    for (const auto& a : amounts) {
        auto r = stock(id, a.resource);
        if (!r) return fail<ReservationId>(r.error);
        if (kind == ReservationKind::Input && a.quantity > r.value.available())
            return fail<ReservationId>(Error::InsufficientStock);
        if (kind == ReservationKind::OutputCapacity && a.quantity > r.value.capacity - r.value.total - r.value.reservedCapacity)
            return fail<ReservationId>(Error::CapacityExceeded);
    }
    ReservationId token{p.state->nextReservation, p.epoch};
    if (auto error = p.publish([&](auto& s) {
        Reservation record{token, id, kind, ReservationState::Active, {amounts.begin(), amounts.end()}};
        // Canonical resource order does not depend on caller bundle ordering.
        std::map<ResourceId, Quantity> ordered;
        for (const auto& a : amounts) ordered.emplace(a.resource, a.quantity);
        record.amounts.clear();
        for (const auto& [resource, quantity] : ordered) {
            record.amounts.push_back({resource, quantity});
            auto& slot = s.inventories.at(id).at(resource);
            if (kind == ReservationKind::Input) slot.reserved += quantity;
            else slot.reservedCapacity += quantity;
        }
        s.reservations.emplace(token, std::move(record));
        ++s.nextReservation;
        Impl::push(s, {EventKind::Reserved, p.now, id, token});
    }); error != Error::None) return fail<ReservationId>(error);
    return {token};
}
Result<Reservation> Runtime::reservation(ReservationId id) const {
    auto r = impl_->state->reservations.find(id);
    if (r == impl_->state->reservations.end()) return fail<Reservation>(Error::InvalidId);
    return {r->second};
}
Status Runtime::commit(ReservationId id) { return finish(id, true); }
Status Runtime::release(ReservationId id) { return finish(id, false); }
Status Runtime::finish(ReservationId id, bool commitValue) {
    auto& p = *impl_;
    auto r = reservation(id);
    if (!r) return fail<Unit>(r.error);
    if (r.value.state != ReservationState::Active) return fail<Unit>(Error::AlreadyTerminal);
    if (auto e = p.eventRoom(); e != Error::None) return fail<Unit>(e);
    if (auto error = p.publish([&](auto& s) {
        auto& record = s.reservations.at(id);
        for (const auto& a : record.amounts) {
            auto& slot = s.inventories.at(record.inventory).at(a.resource);
            if (record.kind == ReservationKind::Input) {
                slot.reserved -= a.quantity;
                if (commitValue) slot.total -= a.quantity;
            } else {
                slot.reservedCapacity -= a.quantity;
                if (commitValue) slot.total += a.quantity;
            }
        }
        record.state = commitValue ? ReservationState::Committed : ReservationState::Released;
        Impl::push(s, {commitValue ? EventKind::Committed : EventKind::Released, p.now, record.inventory, id});
    }); error != Error::None) return fail<Unit>(error);
    return {};
}
Status Runtime::forget(ReservationId id) {
    auto r = reservation(id);
    if (!r) return fail<Unit>(r.error);
    if (r.value.state == ReservationState::Active) return fail<Unit>(Error::ActiveReservation);
    impl_->state->reservations.erase(id);
    return {};
}
Status Runtime::defineProcess(const Process& process) {
    auto& p = *impl_;
    if (!validKey(process.id.value) || !validKey(process.capability) || process.duration <= 0 ||
        p.state->processes.contains(process.id)) return fail<Unit>(Error::InvalidDefinition);
    if (auto e = p.bundle(process.inputs); e != Error::None) return fail<Unit>(e);
    if (auto e = p.bundle(process.outputs); e != Error::None) return fail<Unit>(e);
    if (p.state->processes.size() == p.limits.processes) return fail<Unit>(Error::LimitReached);
    if (auto error = p.publish([&](auto& s) {
        auto copy = process;
        const auto order = [](const Amount& a, const Amount& b) { return a.resource < b.resource; };
        std::sort(copy.inputs.begin(), copy.inputs.end(), order);
        std::sort(copy.outputs.begin(), copy.outputs.end(), order);
        s.processes.emplace(copy.id, std::move(copy));
    }); error != Error::None) return fail<Unit>(error);
    return {};
}
Result<StationId> Runtime::createStation(std::span<const std::string> caps, std::size_t slots) {
    auto& p = *impl_;
    if (auto e = p.capabilities(caps); e != Error::None) return fail<StationId>(e);
    if (!slots || slots > p.limits.slotsPerStation) return fail<StationId>(Error::InvalidQuantity);
    if (p.state->stations.size() == p.limits.stations) return fail<StationId>(Error::LimitReached);
    if (auto e = p.eventRoom(); e != Error::None) return fail<StationId>(e);
    if (p.state->nextStation == std::numeric_limits<std::uint64_t>::max()) return fail<StationId>(Error::Overflow);
    StationId id{p.state->nextStation, p.epoch};
    if (auto error = p.publish([&](auto& s) {
        s.stations.emplace(id, Station{id, {caps.begin(),caps.end()}, std::vector<JobId>(slots)});
        ++s.nextStation;
        Impl::push(s, {EventKind::StationCreated, p.now, {}, {}, {}, id, {}});
    }); error != Error::None) return fail<StationId>(error);
    return {id};
}
Result<WorkerId> Runtime::createWorker(std::span<const std::string> caps) {
    auto& p = *impl_;
    if (auto e = p.capabilities(caps); e != Error::None) return fail<WorkerId>(e);
    if (p.state->workers.size() == p.limits.workers) return fail<WorkerId>(Error::LimitReached);
    if (auto e = p.eventRoom(); e != Error::None) return fail<WorkerId>(e);
    if (p.state->nextWorker == std::numeric_limits<std::uint64_t>::max()) return fail<WorkerId>(Error::Overflow);
    WorkerId id{p.state->nextWorker, p.epoch};
    if (auto error = p.publish([&](auto& s) {
        s.workers.emplace(id, Worker{id, {caps.begin(),caps.end()}, true, {}});
        ++s.nextWorker;
        Impl::push(s, {EventKind::WorkerCreated, p.now, {}, {}, {}, {}, id});
    }); error != Error::None) return fail<WorkerId>(error);
    return {id};
}
Result<Station> Runtime::station(StationId id) const {
    auto it = impl_->state->stations.find(id);
    return it == impl_->state->stations.end() ? fail<Station>(Error::InvalidId) : Result<Station>{it->second};
}
Result<Worker> Runtime::worker(WorkerId id) const {
    auto it = impl_->state->workers.find(id);
    return it == impl_->state->workers.end() ? fail<Worker>(Error::InvalidId) : Result<Worker>{it->second};
}
Status Runtime::setAvailable(WorkerId id, bool available) {
    auto& p = *impl_;
    if (!p.state->workers.contains(id)) return fail<Unit>(Error::InvalidId);
    if (p.state->workers.at(id).available == available) return {};
    if (auto e = p.eventRoom(); e != Error::None) return fail<Unit>(e);
    if (auto error = p.publish([&](auto& s) {
        s.workers.at(id).available = available;
        Impl::push(s, {EventKind::WorkerAvailability, p.now, {}, {}, {}, {}, id});
    }); error != Error::None) return fail<Unit>(error);
    return {};
}
Result<JobId> Runtime::enqueue(ProcessId process, InventoryId input, InventoryId output) {
    auto& p = *impl_;
    auto definition = p.state->processes.find(process);
    if (definition == p.state->processes.end()) return fail<JobId>(Error::InvalidDefinition);
    if (!p.state->inventories.contains(input) || !p.state->inventories.contains(output)) return fail<JobId>(Error::InvalidId);
    for (const auto& a : definition->second.inputs) {
        auto q = stock(input, a.resource);
        if (!q) return fail<JobId>(q.error);
        if (q.value.capacity < a.quantity) return fail<JobId>(Error::CapacityExceeded);
    }
    for (const auto& a : definition->second.outputs) {
        auto q = stock(output, a.resource);
        if (!q) return fail<JobId>(q.error);
        if (q.value.capacity < a.quantity) return fail<JobId>(Error::CapacityExceeded);
    }
    if (p.state->jobs.size() == p.limits.jobs) return fail<JobId>(Error::LimitReached);
    if (auto e = p.eventRoom(); e != Error::None) return fail<JobId>(e);
    if (p.state->nextJob == std::numeric_limits<std::uint64_t>::max()) return fail<JobId>(Error::Overflow);
    JobId id{p.state->nextJob,p.epoch};
    if (auto error = p.publish([&](auto& s) {
        Job j; j.id = id; j.process = process; j.input = input; j.output = output;
        s.jobs.emplace(id,j);
        ++s.nextJob;
        Impl::emit(s, EventKind::JobQueued,p.now,j);
    }); error != Error::None) return fail<JobId>(error);
    return {id};
}
Result<Job> Runtime::job(JobId id) const {
    auto it = impl_->state->jobs.find(id);
    return it == impl_->state->jobs.end() ? fail<Job>(Error::InvalidId) : Result<Job>{it->second};
}
Result<std::size_t> Runtime::assign(bool immediateArrival) {
    auto& p = *impl_;
    if(std::none_of(p.state->jobs.begin(),p.state->jobs.end(),[](const auto& row){return row.second.state==JobState::Queued;}))return {0};
    std::size_t assigned = 0;
    if (auto error = p.publish([&](auto& s) {
        for (auto& [id,j] : s.jobs) {
            if (j.state != JobState::Queued) continue;
            if (assigned == p.limits.transitionsPerAdvance) break;
            const auto& process = s.processes.at(j.process);
            j.blocked = Error::None;
            if (immediateArrival && process.duration > maximum - p.now) { j.blocked = Error::Overflow; continue; }
            for (const auto& a : process.inputs) {
                const auto& q = s.inventories.at(j.input).at(a.resource);
                if (a.quantity > q.available()) { j.blocked = Error::InsufficientStock; break; }
            }
            if (j.blocked != Error::None) continue;
            for (const auto& a : process.outputs) {
                const auto& q = s.inventories.at(j.output).at(a.resource);
                if (a.quantity > q.capacity - q.total - q.reservedCapacity) { j.blocked = Error::CapacityExceeded; break; }
            }
            if (j.blocked != Error::None) continue;
            Station* station = nullptr;
            std::size_t slot = 0;
            for (auto& [stationId, value] : s.stations) {
                if (!Impl::supports(value.capabilities,process.capability)) continue;
                auto free = std::find(value.slots.begin(),value.slots.end(),JobId{});
                if (free != value.slots.end()) { station = &value; slot = static_cast<std::size_t>(free-value.slots.begin()); break; }
            }
            if (!station) { j.blocked = Error::NoStation; continue; }
            Worker* worker = nullptr;
            for (auto& [workerId,value] : s.workers)
                if (value.available && !value.job && Impl::supports(value.capabilities,process.capability)) { worker = &value; break; }
            if (!worker) { j.blocked = Error::NoWorker; continue; }
            const std::size_t eventCount = immediateArrival ? 2 : 1;
            if (eventCount > p.limits.events - s.events.size()) { j.blocked = Error::LimitReached; break; }
            for (const auto& a : process.inputs) s.inventories.at(j.input).at(a.resource).reserved += a.quantity;
            for (const auto& a : process.outputs) s.inventories.at(j.output).at(a.resource).reservedCapacity += a.quantity;
            station->slots[slot] = id; worker->job = id;
            j.station = station->id; j.worker = worker->id; j.slot = slot;
            j.assignment = id.value; j.state = JobState::Assigned;
            Impl::emit(s,EventKind::JobAssigned,p.now,j);
            if (immediateArrival) Impl::start(s,j,p.now);
            ++assigned;
        }
    }); error != Error::None) return fail<std::size_t>(error);
    return {assigned};
}
Status Runtime::arrive(JobId id, std::uint64_t assignment) {
    auto& p = *impl_;
    auto j = job(id);
    if (!j) return fail<Unit>(j.error);
    if (Impl::terminal(j.value.state)) return fail<Unit>(Error::AlreadyTerminal);
    if (j.value.state != JobState::Assigned) return fail<Unit>(Error::InvalidState);
    if (j.value.assignment != assignment) return fail<Unit>(Error::StaleAssignment);
    if (p.state->processes.at(j.value.process).duration > maximum - p.now) return fail<Unit>(Error::Overflow);
    if (auto e = p.eventRoom(); e != Error::None) return fail<Unit>(e);
    if (auto error = p.publish([&](auto& s) { Impl::start(s,s.jobs.at(id),p.now); }); error != Error::None) return fail<Unit>(error);
    return {};
}
Status Runtime::stopJob(JobId id, bool failed, std::uint64_t assignment) {
    auto& p = *impl_;
    auto j = job(id);
    if (!j) return fail<Unit>(j.error);
    if (Impl::terminal(j.value.state)) return fail<Unit>(Error::AlreadyTerminal);
    if (failed && j.value.state != JobState::Assigned) return fail<Unit>(Error::InvalidState);
    if (failed && j.value.assignment != assignment) return fail<Unit>(Error::StaleAssignment);
    if (auto e = p.eventRoom(); e != Error::None) return fail<Unit>(e);
    if (auto error = p.publish([&](auto& s) { Impl::stop(s,s.jobs.at(id),failed ? JobState::Failed : JobState::Cancelled,p.now); }); error != Error::None) return fail<Unit>(error);
    return {};
}
Status Runtime::cancel(JobId id) { return stopJob(id,false); }
Status Runtime::movementFailed(JobId id, std::uint64_t assignment) { return stopJob(id,true,assignment); }
Status Runtime::forgetJob(JobId id) {
    auto j = job(id);
    if (!j) return fail<Unit>(j.error);
    if (j.value.order) return fail<Unit>(Error::InvalidState);
    if (!Impl::terminal(j.value.state)) return fail<Unit>(Error::InvalidState);
    impl_->state->jobs.erase(id);
    return {};
}
Status Runtime::removeStation(StationId id) {
    auto& p = *impl_;
    auto value = station(id);
    if (!value) return fail<Unit>(value.error);
    std::size_t count = 1;
    for (auto job : value.value.slots) if (job) ++count;
    if (auto e = p.eventRoom(count); e != Error::None) return fail<Unit>(e);
    if (auto error = p.publish([&](auto& s) {
        // Job ID order, independent of station slot occupancy order.
        for (auto& [jobId,j] : s.jobs)
            if (j.station == id && !Impl::terminal(j.state)) Impl::stop(s,j,JobState::Cancelled,p.now);
        s.stations.erase(id);
        Impl::push(s, {EventKind::StationRemoved,p.now,{}, {}, {}, id, {}});
    }); error != Error::None) return fail<Unit>(error);
    return {};
}
Status Runtime::removeWorker(WorkerId id) {
    auto& p = *impl_;
    auto value = worker(id);
    if (!value) return fail<Unit>(value.error);
    if (auto e = p.eventRoom(value.value.job ? 2 : 1); e != Error::None) return fail<Unit>(e);
    if (auto error = p.publish([&](auto& s) {
        if (value.value.job) Impl::stop(s,s.jobs.at(value.value.job),JobState::Cancelled,p.now);
        s.workers.erase(id);
        Impl::push(s, {EventKind::WorkerRemoved,p.now,{}, {}, {}, {}, id});
    }); error != Error::None) return fail<Unit>(error);
    return {};
}
Result<CustomerId> Runtime::arriveCustomer(Milliseconds patience) {
    auto& p = *impl_;
    if (patience <= 0) return fail<CustomerId>(Error::InvalidQuantity);
    if (patience > maximum-p.now) return fail<CustomerId>(Error::Overflow);
    if (p.state->customers.size() == p.limits.customers) return fail<CustomerId>(Error::LimitReached);
    if (p.state->nextCustomer == std::numeric_limits<std::uint64_t>::max()) return fail<CustomerId>(Error::Overflow);
    CustomerId id{p.state->nextCustomer,p.epoch};
    if (auto error=p.publish([&](auto& s) {
        s.customers.emplace(id,Customer{id,CustomerState::Waiting,p.now+patience,{}});
        ++s.nextCustomer;
        Impl::push(s,{EventKind::CustomerArrived,p.now,{}, {}, {}, {}, {},id});
    }); error != Error::None) return fail<CustomerId>(error);
    return {id};
}
std::vector<CustomerId> Runtime::serviceQueue() const {
    std::vector<CustomerId> result;
    for (const auto& [id,c] : impl_->state->customers)
        if (c.state == CustomerState::Waiting) result.push_back(id);
    return result;
}
Result<Customer> Runtime::customer(CustomerId id) const {
    auto i=impl_->state->customers.find(id);
    return i == impl_->state->customers.end() ? fail<Customer>(Error::InvalidId) : Result<Customer>{i->second};
}
Result<Order> Runtime::order(OrderId id) const {
    auto i=impl_->state->orders.find(id);
    return i == impl_->state->orders.end() ? fail<Order>(Error::InvalidId) : Result<Order>{i->second};
}
Result<OrderId> Runtime::acceptOrder(CustomerId id, std::span<const OrderLine> lines,
    InventoryId input, InventoryId output, Quantity price, AbandonmentPolicy policy) {
    auto& p=*impl_;
    auto c=customer(id);
    if (!c) return fail<OrderId>(c.error);
    if (c.value.state != CustomerState::Waiting || c.value.deadline <= p.now) return fail<OrderId>(Error::InvalidState);
    auto queue=serviceQueue();
    if (queue.front() != id) return fail<OrderId>(Error::NotQueueHead);
    if (price < 0 || lines.empty()) return fail<OrderId>(Error::InvalidQuantity);
    if (policy != AbandonmentPolicy::CancelProduction && policy != AbandonmentPolicy::FinishForStock)
        return fail<OrderId>(Error::InvalidDefinition);
    if (lines.size() > p.limits.bundleEntries || p.state->orders.size() == p.limits.orders)
        return fail<OrderId>(Error::LimitReached);
    if (!p.state->inventories.contains(input) || !p.state->inventories.contains(output)) return fail<OrderId>(Error::InvalidId);
    std::set<ResourceId> seen;
    std::vector<ProcessId> batches;
    for (const auto& line : lines) {
        if (line.quantity <= 0) return fail<OrderId>(Error::InvalidQuantity);
        if (!seen.insert(line.item).second) return fail<OrderId>(Error::DuplicateResource);
        auto process=p.state->processes.find(line.process);
        if (process == p.state->processes.end()) return fail<OrderId>(Error::InvalidDefinition);
        const auto& recipe=process->second;
        // Direct recipes only: exact whole batches, no surplus or byproducts.
        if (recipe.outputs.size()!=1 || recipe.outputs[0].resource != line.item ||
            line.quantity % recipe.outputs[0].quantity != 0) return fail<OrderId>(Error::InvalidDefinition);
        const auto count=static_cast<std::uint64_t>(line.quantity/recipe.outputs[0].quantity);
        if (count > p.limits.jobsPerOrder-batches.size()) return fail<OrderId>(Error::LimitReached);
        auto space=stock(output,line.item);
        if (!space) return fail<OrderId>(space.error);
        if (space.value.capacity < line.quantity) return fail<OrderId>(Error::CapacityExceeded);
        for (const auto& amount : recipe.inputs) {
            auto available=stock(input,amount.resource);
            if (!available) return fail<OrderId>(available.error);
            if (available.value.capacity < amount.quantity) return fail<OrderId>(Error::CapacityExceeded);
        }
        for (std::uint64_t n=0;n<count;++n) batches.push_back(line.process);
    }
    if (batches.size() > p.limits.jobs-p.state->jobs.size()) return fail<OrderId>(Error::LimitReached);
    const auto maxId=std::numeric_limits<std::uint64_t>::max();
    if (p.state->nextOrder==maxId || batches.size() > maxId-p.state->nextJob) return fail<OrderId>(Error::Overflow);
    OrderId oid{p.state->nextOrder,p.epoch};
    if (auto error=p.publish([&](auto& s) {
        Order o; o.id=oid; o.customer=id; o.output=output; o.price=price; o.abandonment=policy;
        o.lines.assign(lines.begin(),lines.end());
        for (const auto& process : batches) {
            Job j; j.id={s.nextJob++,p.epoch}; j.process=process; j.input=input; j.output=output; j.order=oid;
            s.jobs.emplace(j.id,j); o.jobs.push_back(j.id);
            Impl::emit(s,EventKind::JobQueued,p.now,j);
        }
        s.orders.emplace(oid,o); ++s.nextOrder;
        s.customers.at(id).state=CustomerState::InService; s.customers.at(id).order=oid;
        Impl::push(s,{EventKind::OrderAccepted,p.now,{}, {}, {}, {}, {},id,oid});
    }); error != Error::None) return fail<OrderId>(error);
    return {oid};
}
Status Runtime::abandon(CustomerId id) {
    auto& p=*impl_; auto c=customer(id);
    if (!c) return fail<Unit>(c.error);
    if (c.value.state == CustomerState::Served || c.value.state == CustomerState::Departed)
        return fail<Unit>(Error::AlreadyTerminal);
    if (auto error=p.publish([&](auto& s) { Impl::leave(s,s.customers.at(id),p.now); });
        error != Error::None) return fail<Unit>(error);
    return {};
}
Result<ReceiptId> Runtime::deliver(OrderId id) {
    auto& p=*impl_; auto o=order(id);
    if (!o) return fail<ReceiptId>(o.error);
    if (Impl::orderTerminal(o.value.state)) return fail<ReceiptId>(Error::AlreadyTerminal);
    if (o.value.state != OrderState::Ready) return fail<ReceiptId>(Error::InvalidState);
    if (p.state->customers.at(o.value.customer).deadline <= p.now) return fail<ReceiptId>(Error::InvalidState);
    if (p.state->receipts.size() == p.limits.receipts) return fail<ReceiptId>(Error::LimitReached);
    if (p.state->nextReceipt == std::numeric_limits<std::uint64_t>::max()) return fail<ReceiptId>(Error::Overflow);
    ReceiptId receipt{p.state->nextReceipt,p.epoch};
    if (auto error=p.publish([&](auto& s) {
        auto& order=s.orders.at(id);
        for (const auto& line : order.lines) {
            auto& stock=s.inventories.at(order.output).at(line.item);
            stock.reserved-=line.quantity; stock.total-=line.quantity;
        }
        order.state=OrderState::Delivered; order.receipt=receipt;
        s.customers.at(order.customer).state=CustomerState::Served;
        s.receipts.emplace(receipt,Receipt{receipt,id,order.price,false}); ++s.nextReceipt;
        Impl::push(s,{EventKind::Payment,p.now,{}, {}, {}, {}, {},order.customer,id,receipt});
    }); error != Error::None) return fail<ReceiptId>(error);
    return {receipt};
}
Result<Receipt> Runtime::receipt(ReceiptId id) const {
    auto i=impl_->state->receipts.find(id);
    return i == impl_->state->receipts.end() ? fail<Receipt>(Error::InvalidId) : Result<Receipt>{i->second};
}
std::vector<Receipt> Runtime::pendingReceipts() const {
    std::vector<Receipt> result;
    for (const auto& [id,r] : impl_->state->receipts) if (!r.acknowledged) result.push_back(r);
    return result;
}
Status Runtime::acknowledgeReceipt(ReceiptId id) {
    auto it=impl_->state->receipts.find(id);
    if (it==impl_->state->receipts.end()) return fail<Unit>(Error::InvalidId);
    it->second.acknowledged=true;
    return {};
}
Status Runtime::forgetCustomer(CustomerId id) {
    auto& p=*impl_; auto c=customer(id);
    if (!c) return fail<Unit>(c.error);
    if (c.value.state != CustomerState::Departed && c.value.state != CustomerState::Served)
        return fail<Unit>(Error::InvalidState);
    if (c.value.order) {
        const auto& o=p.state->orders.at(c.value.order);
        for (auto jid:o.jobs) if (!Impl::terminal(p.state->jobs.at(jid).state)) return fail<Unit>(Error::InvalidState);
        if (o.receipt && !p.state->receipts.at(o.receipt).acknowledged) return fail<Unit>(Error::InvalidState);
    }
    if (auto error=p.publish([&](auto& s) {
        if (c.value.order) {
            const auto& o=s.orders.at(c.value.order);
            for (auto jid:o.jobs) s.jobs.erase(jid);
            if (o.receipt) s.receipts.erase(o.receipt);
            s.orders.erase(c.value.order);
        }
        s.customers.erase(id);
    }); error != Error::None) return fail<Unit>(error);
    return {};
}
Result<Advance> Runtime::advance(Milliseconds elapsed) {
    auto& p = *impl_;
    if (elapsed < 0) return fail<Advance>(Error::InvalidQuantity);
    if (elapsed > maximum - p.now) return fail<Advance>(Error::Overflow);
    const auto target = p.now + elapsed;
    const bool dueJob=std::any_of(p.state->jobs.begin(),p.state->jobs.end(),[&](const auto& row){return row.second.state==JobState::Running && row.second.deadline<=target;});
    const bool dueCustomer=std::any_of(p.state->customers.begin(),p.state->customers.end(),[&](const auto& row){const auto& c=row.second;return (c.state==CustomerState::Waiting || c.state==CustomerState::InService) && c.deadline<=target;});
    // Clock-only publication cannot allocate or emit events. Due transitions keep
    // the existing staged cascade, tie ordering, work budget and outbox checks.
    if(!dueJob && !dueCustomer){p.now=target;return {Advance{elapsed,0,0,false,0}};}
    auto cursor = p.now;
    Advance result;
    if (auto error = p.publish([&](auto& s) {
        for (;;) {
            Job* next = nullptr;
            for (auto& [id,j] : s.jobs)
                if (j.state == JobState::Running && j.deadline <= target &&
                    (!next || j.deadline < next->deadline || (j.deadline == next->deadline && id < next->id)))
                    next = &j;
            Customer* expiry=nullptr;
            for (auto& [id,c] : s.customers)
                if ((c.state==CustomerState::Waiting || c.state==CustomerState::InService) && c.deadline<=target &&
                    (!expiry || c.deadline<expiry->deadline || (c.deadline==expiry->deadline && id<expiry->id))) expiry=&c;
            if (!next && !expiry) { cursor = target; break; }
            if (result.completed+result.expired == p.limits.transitionsPerAdvance || s.events.size() == p.limits.events) {
                result.blocked = true; break;
            }
            if (next && (!expiry || next->deadline<=expiry->deadline)) {
                cursor = next->deadline;
                Impl::stop(s, *next, JobState::Completed, cursor);
                ++result.completed;
            } else {
                cursor=expiry->deadline;
                Impl::leave(s,*expiry,cursor);
                ++result.expired;
            }
        }
    }); error != Error::None) return fail<Advance>(error);
    result.consumed = cursor - p.now;
    result.remaining = target - cursor;
    p.now = cursor;
    return {result};
}
Milliseconds Runtime::time() const noexcept { return impl_->now; }
RuntimeMetrics Runtime::metrics() const noexcept {
    const auto& s=*impl_->state;
    return {impl_->limits,impl_->resources.size(),s.inventories.size(),s.reservations.size(),s.processes.size(),s.stations.size(),s.workers.size(),s.jobs.size(),s.customers.size(),s.orders.size(),s.receipts.size(),s.events.size()};
}
std::vector<Event> Runtime::drainEvents() {
    std::vector<Event> result;
    result.swap(impl_->state->events);
    return result;
}
#include "snapshot.inc"
} // namespace engine::simulator
