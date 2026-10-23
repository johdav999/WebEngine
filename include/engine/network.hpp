#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine::net {
using Bytes = std::vector<std::uint8_t>;
using PeerId = std::uint32_t;
using EntityId = std::uint32_t;
inline constexpr std::size_t maxMessage = 32768, maxEntities = 64,
                             maxPayload = 256, maxQueue = 64;
// Application-defined, explicitly versioned payloads. IDs belong to this
// session, never to a local Scene registry. owner=0 denotes a server-owned
// entity.
struct Record {
  EntityId id = 0;
  PeerId owner = 0;
  std::uint32_t kind = 0;
  Bytes data;
  bool operator==(const Record &) const = default;
};
struct Command {
  PeerId peer = 0;
  EntityId target = 0;
  std::uint32_t sequence = 0, kind = 0;
  Bytes data;
};
enum class Type : std::uint32_t {
  Hello = 1,
  Welcome,
  Command,
  Snapshot,
  Delta,
  Resync,
  Rpc,
  Event
};
struct ByteChange {
  std::uint32_t offset = 0;
  std::uint8_t value = 0;
};
struct DataPatch {
  EntityId id = 0;
  std::vector<ByteChange> changes;
};
class BaselineMismatch : public std::invalid_argument {
public:
  BaselineMismatch() : std::invalid_argument("Snapshot baseline unavailable") {}
};
struct Message {
  Type type = Type::Hello;
  std::uint32_t schema = 0, session = 0, sequence = 0, peer = 0;
  Command command;
  std::vector<Record> records;
  std::uint32_t baseline = 0;
  std::vector<EntityId> removed;
  std::vector<DataPatch> patches;
};
Bytes encode(const Message &);
Message decode(
    std::span<const std::uint8_t>); // Strict, bounded, complete validation.

// Single owner thread. No clock, scene, IO, callbacks or global registry.
class ServerSession {
public:
  ServerSession(std::uint32_t schema, std::uint32_t session);
  PeerId connect(); // At most 16 live peers; IDs never reused in a session.
  void disconnect(PeerId);
  // Trusted host authentication/grace policy controls these transitions.
  // Suspend retains simulation identity but rejects network traffic. Resume
  // requires a fresh Hello and rotates a per-peer epoch, clearing old queues.
  void suspend(PeerId);
  void resume(PeerId);
  std::vector<PeerId> simulationPeers() const;
  // Invalid input throws before changing the peer or command queue.
  // Hello returns a Welcome; command/resync return empty. Host closes invalid
  // peers.
  Bytes receive(PeerId, std::span<const std::uint8_t>);
  std::vector<PeerId> readyPeers() const;
  std::vector<Command> takeCommands();
  // Separate reliable sequence/queue: client RPCs can target owned entities only.
  std::vector<Command> takeRpcs();
  // Trusted host chooses recipients. No retention/replay for late join or resync.
  Bytes eventFor(PeerId, EntityId target, std::uint32_t kind, Bytes payload);
  Bytes snapshot(
      std::span<const Record>); // Atomic registry replacement + sequence.
  // Publish snapshot(all) once first. Select from that authoritative registry;
  // owned entities are always included. Baselines are per ready peer, over the
  // reliable ordered transport. Full keyframes at least every 20 publications.
  Bytes snapshotFor(PeerId, std::span<const EntityId> relevant,
                    bool forceFull = false);
  // Trusted host projection: IDs/owners/kinds must match the global registry.
  // Payloads may be filtered per peer. Missing owned records are auto-included.
  Bytes snapshotForRecords(PeerId, std::span<const Record>,bool forceFull=false);

private:
  struct Peer {
    bool ready = false, reserved = false;
    std::uint32_t epoch = 0;
    std::uint32_t sequence = 0, rpc = 0, event = 0;
    std::uint32_t snapshot = 0, keyframe = 0;
    bool full = true;
    std::vector<Record> baseline;
  };
  std::uint32_t schema_, session_, nextPeer_ = 1, sequence_ = 0, nextEpoch_ = 0;
  std::map<PeerId, Peer> peers_;
  std::vector<Record> records_;
  std::vector<Command> commands_, rpcs_;
};
class ClientSession {
public:
  explicit ClientSession(std::uint32_t schema) : schema_(schema) {}
  Bytes hello() const;
  void reset();
  void receive(std::span<const std::uint8_t>); // Stages complete reconstructed
                                               // records before publication.
  Bytes command(EntityId, std::uint32_t kind, Bytes payload);
  Bytes rpc(EntityId, std::uint32_t kind, Bytes payload);
  std::vector<Command> takeEvents();
  Bytes
  requestSnapshot() const; // Does not consume a gameplay command sequence.
  bool ready() const { return peer_ != 0; }
  PeerId peer() const { return peer_; }
  std::uint32_t sequence() const { return snapshot_; }
  const std::vector<Record> &records() const { return records_; }

private:
  std::uint32_t schema_, session_ = 0, peer_ = 0, snapshot_ = 0, command_ = 0;
  std::uint32_t rpc_ = 0, event_ = 0;
  std::vector<Command> events_;
  std::vector<Record> records_;
};
enum class ConnectionState { Connecting, Open, Closed, Failed };
// All public methods run on the creating thread. Adapters own bounded queues;
// disposal cancels callbacks/IO. A new connection is a new lifetime/generation.
class Connection {
public:
  virtual ~Connection() = default;
  virtual ConnectionState state() const = 0;
  virtual std::string error() const = 0;
  virtual bool send(std::span<const std::uint8_t>) = 0;
  virtual std::vector<Bytes> receive() = 0;
};
// Link engine_network_browser or engine_network_windows. ws:// is allowed only
// for loopback development; remote endpoints require wss://. No credentials in
// URL.
std::unique_ptr<Connection> connectWebSocket(const std::string &url);
bool validEndpoint(const std::string &url);
} // namespace engine::net
