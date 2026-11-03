#pragma once
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::simulator {
using Quantity = std::int64_t;
using Milliseconds = std::int64_t;

// Definition keys are owned values; validated by Runtime construction.
struct ResourceId {
    std::string value;
    auto operator<=>(const ResourceId&) const = default;
};
template<class Tag> struct Id {
    std::uint64_t value = 0, epoch = 0;
    explicit operator bool() const { return value != 0 && epoch != 0; }
    auto operator<=>(const Id&) const = default;
};
using InventoryId = Id<struct InventoryTag>;
using ReservationId = Id<struct ReservationTag>;
using StationId = Id<struct StationTag>;
using WorkerId = Id<struct WorkerTag>;
using JobId = Id<struct JobTag>;
using CustomerId = Id<struct CustomerTag>;
using OrderId = Id<struct OrderTag>;
using ReceiptId = Id<struct ReceiptTag>;
struct ProcessId {
    std::string value;
    auto operator<=>(const ProcessId&) const = default;
};
enum class Error {
    None, InvalidQuantity, InvalidId, UnknownResource, DuplicateResource,
    InsufficientStock, CapacityExceeded, LimitReached, AlreadyTerminal,
    ActiveReservation, Overflow, InvalidDefinition, InvalidState, StaleAssignment,
    NoStation, NoWorker, NotQueueHead, InvalidSnapshot, UnsupportedVersion, DefinitionMismatch,
    InsufficientFunds, StaleTransaction
};
std::string_view describe(Error error) noexcept;
template<class T> struct Result {
    T value{};
    Error error = Error::None;
    explicit operator bool() const { return error == Error::None; }
};
struct Unit {};
using Status = Result<Unit>;
struct Amount { ResourceId resource; Quantity quantity = 0; };
struct Stock {
    ResourceId resource;
    Quantity total = 0, reserved = 0, capacity = 0, reservedCapacity = 0;
    Quantity available() const { return total - reserved; }
};
enum class ReservationKind { Input, OutputCapacity };
enum class ReservationState { Active, Committed, Released };
struct Reservation {
    ReservationId id;
    InventoryId inventory;
    ReservationKind kind = ReservationKind::Input;
    ReservationState state = ReservationState::Active;
    std::vector<Amount> amounts;
};
struct Process {
    ProcessId id;
    std::vector<Amount> inputs, outputs;
    Milliseconds duration = 0;
    std::string capability;
};
enum class JobState { Queued, Assigned, Running, Completed, Cancelled, Failed };
struct Job {
    JobId id;
    ProcessId process;
    InventoryId input, output;
    JobState state = JobState::Queued;
    StationId station;
    WorkerId worker;
    std::size_t slot = 0;
    std::uint64_t assignment = 0;
    Milliseconds deadline = 0;
    Error blocked = Error::None;
    OrderId order;
};
enum class CustomerState { Waiting, InService, Served, Departed };
enum class OrderState { Accepted, Preparing, Ready, Delivered, Abandoned, Failed };
enum class AbandonmentPolicy { CancelProduction, FinishForStock };
struct Customer {
    CustomerId id;
    CustomerState state = CustomerState::Waiting;
    Milliseconds deadline = 0;
    OrderId order;
};
struct OrderLine { ResourceId item; Quantity quantity = 0; ProcessId process; };
struct Order {
    OrderId id;
    CustomerId customer;
    InventoryId output;
    OrderState state = OrderState::Accepted;
    AbandonmentPolicy abandonment = AbandonmentPolicy::CancelProduction;
    std::vector<OrderLine> lines;
    std::vector<JobId> jobs;
    Quantity price = 0; // integer minor units; interpretation and wallet are game-owned
    ReceiptId receipt;
};
struct Receipt { ReceiptId id; OrderId order; Quantity amount = 0; bool acknowledged = false; };
struct Station { StationId id; std::vector<std::string> capabilities; std::vector<JobId> slots; };
struct Worker { WorkerId id; std::vector<std::string> capabilities; bool available = true; JobId job; };
struct Advance {
    Milliseconds consumed = 0, remaining = 0;
    std::size_t completed = 0;
    bool blocked = false; // work budget or event outbox exhausted; drain/retry remaining
    std::size_t expired = 0;
};
enum class EventKind {
    InventoryCreated, StockAdded, Reserved, Committed, Released,
    StationCreated, WorkerCreated, WorkerAvailability, StationRemoved, WorkerRemoved,
    JobQueued, JobAssigned, JobStarted, JobCompleted, JobCancelled, JobFailed,
    CustomerArrived, CustomerLeft, OrderAccepted, OrderReady, OrderAbandoned, OrderFailed, Payment,
    StockTransferred, StockRemoved
};
struct Event {
    EventKind kind;
    Milliseconds time = 0;
    InventoryId inventory;
    ReservationId reservation;
    JobId job;
    StationId station;
    WorkerId worker;
    CustomerId customer;
    OrderId order;
    ReceiptId receipt;
};
struct Limits {
    std::size_t resources = 256, inventories = 128, reservations = 4096;
    std::size_t bundleEntries = 64, events = 4096;
    std::size_t processes = 256, stations = 128, workers = 128, jobs = 4096;
    std::size_t slotsPerStation = 16, capabilities = 16, transitionsPerAdvance = 64;
    std::size_t customers = 1024, orders = 1024, receipts = 1024, jobsPerOrder = 32;
    std::size_t snapshotBytes = 8 * 1024 * 1024;
};

struct RuntimeMetrics {
    Limits limits;
    std::size_t resources=0,inventories=0,reservations=0,processes=0,stations=0,workers=0,jobs=0,customers=0,orders=0,receipts=0,events=0;
};

// Single-owner-thread runtime. No callbacks, clock, scene or IO ownership.
// All returned records are copies. Handles belong to exactly one runtime lifetime.
class Runtime {
public:
    // Throws invalid_argument for invalid/duplicate definitions or invalid limits.
    explicit Runtime(std::span<const ResourceId> resources, Limits limits = {});
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;

    Result<InventoryId> createInventory(std::span<const Amount> capacities);
    Status add(InventoryId inventory, std::span<const Amount> amounts);
    // Atomic removal of unreserved stock, for disposal or other host-owned sinks.
    Status remove(InventoryId inventory, std::span<const Amount> amounts);
    // Atomic bundle transfer; input holds and incoming capacity are unavailable.
    Status transfer(InventoryId source, InventoryId destination, std::span<const Amount> amounts);
    Result<Stock> stock(InventoryId inventory, const ResourceId& resource) const;
    Result<ReservationId> reserve(InventoryId inventory, std::span<const Amount> amounts);
    Result<ReservationId> reserveOutput(InventoryId inventory, std::span<const Amount> amounts);
    Result<Reservation> reservation(ReservationId id) const;
    Status commit(ReservationId id);
    Status release(ReservationId id);
    // Only terminal records may be forgotten. IDs are never reused.
    Status forget(ReservationId id);
    // Process definitions are copied, immutable once registered, and never replaced.
    Status defineProcess(const Process& process);
    Result<StationId> createStation(std::span<const std::string> capabilities, std::size_t slots = 1);
    Result<WorkerId> createWorker(std::span<const std::string> capabilities);
    Result<Station> station(StationId id) const;
    Result<Worker> worker(WorkerId id) const;
    Status setAvailable(WorkerId id, bool available);
    Result<JobId> enqueue(ProcessId process, InventoryId input, InventoryId output);
    Result<Job> job(JobId id) const;
    // Explicit scheduling boundary, FIFO with ID ties; scans at most limits.jobs.
    // immediateArrival is a headless/simple-game adapter, not navigation.
    Result<std::size_t> assign(bool immediateArrival = false);
    Status arrive(JobId id, std::uint64_t assignment);
    Status movementFailed(JobId id, std::uint64_t assignment);
    Status cancel(JobId id);
    Status forgetJob(JobId id);
    Status removeStation(StationId id);
    Status removeWorker(WorkerId id);
    Result<CustomerId> arriveCustomer(Milliseconds patience);
    std::vector<CustomerId> serviceQueue() const;
    Result<Customer> customer(CustomerId id) const;
    Result<Order> order(OrderId id) const;
    Result<OrderId> acceptOrder(CustomerId customer, std::span<const OrderLine> lines,
        InventoryId input, InventoryId output, Quantity price,
        AbandonmentPolicy abandonment = AbandonmentPolicy::CancelProduction);
    Status abandon(CustomerId customer);
    Result<ReceiptId> deliver(OrderId order);
    Result<Receipt> receipt(ReceiptId id) const;
    std::vector<Receipt> pendingReceipts() const;
    Status acknowledgeReceipt(ReceiptId id);
    // Retires a terminal customer's entire service record only after all linked
    // jobs terminate and any payment is acknowledged. IDs are never reused.
    Status forgetCustomer(CustomerId id);
    Result<Advance> advance(Milliseconds elapsed);
    Milliseconds time() const noexcept;
    RuntimeMetrics metrics() const noexcept;
    std::vector<Event> drainEvents();
    // Version 1 canonical bytes. Events and runtime epochs are transient.
    std::vector<std::uint8_t> save() const;
    Status restore(std::span<const std::uint8_t> bytes);
    std::uint64_t epoch() const noexcept;
    std::uint64_t receiptSequence() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Result<ReservationId> reserveImpl(InventoryId, std::span<const Amount>, ReservationKind);
    Status finish(ReservationId, bool commit);
    Status stopJob(JobId, bool failed, std::uint64_t assignment = 0);
};
} // namespace engine::simulator
