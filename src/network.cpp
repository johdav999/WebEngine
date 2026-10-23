#include "engine/network.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace engine::net {
namespace {
void require(bool ok) {
  if (!ok)
    throw std::invalid_argument("Invalid multiplayer packet or session state");
}
void put(Bytes &b, std::uint32_t n) {
  for (int i = 0; i < 4; ++i)
    b.push_back(std::uint8_t(n >> (i * 8)));
}
struct Reader {
  std::span<const std::uint8_t> b;
  std::size_t at = 0;
  std::uint32_t get() {
    require(at + 4 <= b.size());
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
      v |= std::uint32_t(b[at++]) << (i * 8);
    return v;
  }
  std::uint8_t byte() {
    require(at < b.size());
    return b[at++];
  }
  Bytes blob() {
    auto n = get();
    require(n <= maxPayload && n <= b.size() - at);
    Bytes v(b.begin() + at, b.begin() + at + n);
    at += n;
    return v;
  }
};
void blob(Bytes &b, const Bytes &v) {
  require(v.size() <= maxPayload);
  put(b, std::uint32_t(v.size()));
  b.insert(b.end(), v.begin(), v.end());
}
void recordsValid(std::span<const Record> records) {
  require(records.size() <= maxEntities);
  std::set<EntityId> seen;
  for (const auto &r : records)
    require(r.id && r.kind && r.data.size() <= maxPayload &&
            seen.insert(r.id).second);
}
} // namespace
Bytes encode(const Message &m) {
  require(m.schema != 0);
  Bytes b;
  put(b, 0x314e4557);
  put(b, 1);
  put(b, std::uint32_t(m.type));
  put(b, m.schema);
  put(b, m.session);
  put(b, m.sequence);
  put(b, m.peer);
  switch (m.type) {
  case Type::Hello:
    require(!m.session && !m.sequence && !m.peer);
    break;
  case Type::Welcome:
    require(m.session && m.peer && !m.sequence);
    break;
  case Type::Rpc:
  case Type::Event:
  case Type::Command:
    require(m.session && m.peer && m.sequence && m.command.target &&
            m.command.kind);
    put(b, m.command.target);
    put(b, m.command.kind);
    blob(b, m.command.data);
    break;
  case Type::Snapshot:
    require(m.session && m.sequence && !m.peer);
    recordsValid(m.records);
    put(b, std::uint32_t(m.records.size()));
    for (const auto &r : m.records) {
      put(b, r.id);
      put(b, r.owner);
      put(b, r.kind);
      blob(b, r.data);
    }
    break;
  case Type::Delta: {
    require(m.session && m.sequence && !m.peer && m.baseline &&
            m.baseline < m.sequence);
    recordsValid(m.records);
    require(m.removed.size() <= maxEntities && m.patches.size() <= maxEntities);
    std::set<EntityId> ids;
    for (const auto &r : m.records)
      ids.insert(r.id);
    put(b, m.baseline);
    put(b, std::uint32_t(m.records.size()));
    for (const auto &r : m.records) {
      put(b, r.id);
      put(b, r.owner);
      put(b, r.kind);
      blob(b, r.data);
    }
    put(b, std::uint32_t(m.removed.size()));
    for (auto id : m.removed) {
      require(id && ids.insert(id).second);
      put(b, id);
    }
    put(b, std::uint32_t(m.patches.size()));
    for (const auto &patch : m.patches) {
      require(patch.id && ids.insert(patch.id).second &&
              !patch.changes.empty() && patch.changes.size() <= maxPayload);
      put(b, patch.id);
      put(b, std::uint32_t(patch.changes.size()));
      int previous = -1;
      for (auto change : patch.changes) {
        require(change.offset<maxPayload &&int(change.offset)> previous);
        previous = int(change.offset);
        b.push_back(std::uint8_t(change.offset));
        b.push_back(change.value);
      }
    }
    break;
  }
  case Type::Resync:
    require(m.session && m.peer && !m.sequence);
    break;
  default:
    require(false);
  }
  require(b.size() <= maxMessage);
  return b;
}
Message decode(std::span<const std::uint8_t> bytes) {
  require(bytes.size() <= maxMessage);
  Reader r{bytes};
  require(r.get() == 0x314e4557 && r.get() == 1);
  Message m;
  m.type = Type(r.get());
  m.schema = r.get();
  m.session = r.get();
  m.sequence = r.get();
  m.peer = r.get();
  if (m.type == Type::Command || m.type == Type::Rpc || m.type == Type::Event) {
    m.command = {m.peer, r.get(), m.sequence, r.get(), r.blob()};
  } else if (m.type == Type::Snapshot) {
    auto n = r.get();
    require(n <= maxEntities);
    for (std::uint32_t i = 0; i < n; ++i) {
      Record v;
      v.id = r.get();
      v.owner = r.get();
      v.kind = r.get();
      v.data = r.blob();
      m.records.push_back(std::move(v));
    }
  }
  if (m.type == Type::Delta) {
    m.baseline = r.get();
    auto count = r.get();
    require(count <= maxEntities);
    for (std::uint32_t i = 0; i < count; ++i) {
      Record v;
      v.id = r.get();
      v.owner = r.get();
      v.kind = r.get();
      v.data = r.blob();
      m.records.push_back(std::move(v));
    }
    count = r.get();
    require(count <= maxEntities);
    for (std::uint32_t i = 0; i < count; ++i)
      m.removed.push_back(r.get());
    count = r.get();
    require(count <= maxEntities);
    for (std::uint32_t i = 0; i < count; ++i) {
      DataPatch patch;
      patch.id = r.get();
      auto n = r.get();
      require(n <= maxPayload);
      for (std::uint32_t k = 0; k < n; ++k) {
        auto offset = r.byte(), value = r.byte();
        patch.changes.push_back({offset, value});
      }
      m.patches.push_back(std::move(patch));
    }
  }
  require(r.at == bytes.size());
  (void)encode(m);
  return m;
}
ServerSession::ServerSession(std::uint32_t schema, std::uint32_t session)
    : schema_(schema), session_(session), nextEpoch_(session) {
  require(schema && session);
}
PeerId ServerSession::connect() {
  require(peers_.size() < 16 && nextPeer_ != 0);
  auto p = nextPeer_;
  Peer state;state.epoch=session_;peers_.emplace(p, std::move(state));
  ++nextPeer_;
  return p;
}
void ServerSession::disconnect(PeerId id) {
  peers_.erase(id);
  std::erase_if(commands_, [&](const Command &c) { return c.peer == id; });
  std::erase_if(rpcs_, [&](const Command &c) { return c.peer == id; });
}
void ServerSession::suspend(PeerId id){
  auto at=peers_.find(id);require(at!=peers_.end());auto &p=at->second;
  p.reserved|=p.ready;p.ready=false;
  std::erase_if(commands_,[&](const auto &c){return c.peer==id;});
  std::erase_if(rpcs_,[&](const auto &c){return c.peer==id;});
}
void ServerSession::resume(PeerId id){
  auto at=peers_.find(id);require(at!=peers_.end()&&!at->second.ready&&nextEpoch_!=std::numeric_limits<std::uint32_t>::max());
  auto &p=at->second;p.epoch=++nextEpoch_;p.sequence=p.rpc=p.event=p.snapshot=p.keyframe=0;p.full=true;p.baseline.clear();
}
std::vector<PeerId> ServerSession::simulationPeers() const {
  std::vector<PeerId> result;for(const auto &[id,p]:peers_)if(p.ready||p.reserved)result.push_back(id);return result;
}
Bytes ServerSession::receive(PeerId id, std::span<const std::uint8_t> bytes) {
  auto it = peers_.find(id);
  require(it != peers_.end());
  auto m = decode(bytes);
  require(m.schema == schema_);
  auto &p = it->second;
  if (m.type == Type::Hello) {
    require(!p.ready);
    Message reply;
    reply.type = Type::Welcome;
    reply.schema = schema_;
    reply.session = p.epoch;
    reply.peer = id;
    auto b = encode(reply);
    p.ready = true;p.reserved=false;
    return b;
  }
  if (m.type == Type::Resync) {
    require(p.ready && m.peer == id && m.session == p.epoch);
    p.full = true;
    return {};
  }
  const bool rpc=m.type==Type::Rpc;
  auto &sequence=rpc?p.rpc:p.sequence;
  auto &queue=rpc?rpcs_:commands_;
  require((m.type==Type::Command||rpc) && p.ready && m.peer==id &&
          m.session==p.epoch && m.sequence>sequence && queue.size()<maxQueue);
  require(std::any_of(records_.begin(), records_.end(), [&](const Record &r) {
    return r.id == m.command.target && r.owner == id;
  }));
  queue.push_back(std::move(m.command));
  sequence = m.sequence;
  return {};
}
std::vector<Command> ServerSession::takeRpcs() {
  auto result=std::move(rpcs_);rpcs_.clear();return result;
}
Bytes ServerSession::eventFor(PeerId peer,EntityId target,std::uint32_t kind,Bytes payload) {
  auto at=peers_.find(peer);require(at!=peers_.end()&&at->second.ready&&
      at->second.event!=std::numeric_limits<std::uint32_t>::max());
  require(std::any_of(records_.begin(),records_.end(),[&](const auto &r){return r.id==target;}));
  Message m;m.type=Type::Event;m.schema=schema_;m.session=at->second.epoch;m.peer=peer;
  m.sequence=at->second.event+1;m.command={peer,target,m.sequence,kind,std::move(payload)};
  auto result=encode(m);++at->second.event;return result;
}
std::vector<PeerId> ServerSession::readyPeers() const {
  std::vector<PeerId> v;
  for (const auto &[id, p] : peers_)
    if (p.ready)
      v.push_back(id);
  return v;
}
std::vector<Command> ServerSession::takeCommands() {
  auto v = std::move(commands_);
  commands_.clear();
  return v;
}
Bytes ServerSession::snapshot(std::span<const Record> records) {
  require(sequence_ != std::numeric_limits<std::uint32_t>::max());
  Message m;
  m.type = Type::Snapshot;
  m.schema = schema_;
  m.session = session_;
  m.sequence = sequence_ + 1;
  m.records.assign(records.begin(), records.end());
  auto b = encode(m);
  records_ = std::move(m.records);
  ++sequence_;
  return b;
}
Bytes ServerSession::snapshotFor(PeerId peer,
                                 std::span<const EntityId> relevant,
                                 bool force) {
  require(relevant.size()<=maxEntities);
  std::set<EntityId> selected;
  for(auto id:relevant)require(selected.insert(id).second&&std::any_of(records_.begin(),records_.end(),[&](const auto& r){return r.id==id;}));
  std::vector<Record> current;
  for(const auto& r:records_)if(r.owner==peer||selected.contains(r.id))current.push_back(r);
  return snapshotForRecords(peer,current,force);
}
Bytes ServerSession::snapshotForRecords(PeerId peer,std::span<const Record> projected,bool force) {
  auto found=peers_.find(peer);require(found!=peers_.end()&&found->second.ready&&sequence_);
  auto& p=found->second;require(p.snapshot<sequence_);recordsValid(projected);
  std::vector<Record> current(projected.begin(),projected.end());
  for(const auto& r:current){auto original=std::find_if(records_.begin(),records_.end(),[&](const auto& v){return v.id==r.id;});require(original!=records_.end()&&original->owner==r.owner&&original->kind==r.kind);}
  for(const auto& r:records_)if(r.owner==peer&&std::none_of(current.begin(),current.end(),[&](const auto& v){return v.id==r.id;}))current.push_back(r);
  recordsValid(current);
  std::sort(current.begin(),current.end(),[](const auto& a,const auto& b){return a.id<b.id;});
  Message full;
  full.type = Type::Snapshot;
  full.schema = schema_;
  full.session = p.epoch;
  full.sequence = sequence_;
  full.records = current;
  auto packet = encode(full);
  bool key = force || p.full || !p.snapshot || sequence_ - p.keyframe >= 20;
  if (!key) {
    Message delta = full;
    delta.type = Type::Delta;
    delta.baseline = p.snapshot;
    delta.records.clear();
    for (const auto &old : p.baseline)
      if (std::none_of(current.begin(), current.end(),
                       [&](const auto &r) { return r.id == old.id; }))
        delta.removed.push_back(old.id);
    for (const auto &r : current) {
      auto old = std::find_if(p.baseline.begin(), p.baseline.end(),
                              [&](const auto &v) { return r.id == v.id; });
      if (old == p.baseline.end() || old->owner != r.owner ||
          old->kind != r.kind || old->data.size() != r.data.size())
        delta.records.push_back(r);
      else {
        DataPatch patch;
        patch.id = r.id;
        for (std::size_t i = 0; i < r.data.size(); ++i)
          if (r.data[i] != old->data[i])
            patch.changes.push_back({std::uint32_t(i), r.data[i]});
        if (!patch.changes.empty())
          delta.patches.push_back(std::move(patch));
      }
    }
    std::size_t size = 44 + delta.removed.size() * 4;
    for (const auto &r : delta.records)
      size += 16 + r.data.size();
    for (const auto &patch : delta.patches)
      size += 8 + 2 * patch.changes.size();
    if (size < packet.size())
      packet = encode(delta);
    else
      key = true;
  }
  p.baseline = std::move(current);
  p.snapshot = sequence_;
  p.full = false;
  if (key)
    p.keyframe = sequence_;
  return packet;
}
Bytes ClientSession::requestSnapshot() const {
  require(ready());
  Message m;
  m.type = Type::Resync;
  m.schema = schema_;
  m.session = session_;
  m.peer = peer_;
  return encode(m);
}
Bytes ClientSession::hello() const {
  Message m;
  m.schema = schema_;
  return encode(m);
}
void ClientSession::reset() {
  session_ = peer_ = snapshot_ = command_ = 0;
  records_.clear();
  rpc_=event_=0;events_.clear();
}
void ClientSession::receive(std::span<const std::uint8_t> bytes) {
  auto m = decode(bytes);
  require(m.schema == schema_);
  if (m.type == Type::Welcome) {
    require(!ready());
    session_ = m.session;
    peer_ = m.peer;
    return;
  }
  if(m.type==Type::Event){
    require(ready()&&m.session==session_&&m.peer==peer_&&m.sequence>event_&&events_.size()<maxQueue);
    events_.push_back(std::move(m.command));event_=m.sequence;return;
  }
  require((m.type == Type::Snapshot || m.type == Type::Delta) && ready() &&
          m.session == session_ && m.sequence > snapshot_);
  if (m.type == Type::Delta) {
    if (!snapshot_ || m.baseline != snapshot_)
      throw BaselineMismatch{};
    std::map<EntityId, Record> candidate;
    for (const auto &r : records_)
      candidate.emplace(r.id, r);
    for (auto id : m.removed)
      require(candidate.erase(id) == 1);
    for (const auto &r : m.records)
      candidate[r.id] = r;
    for (const auto &patch : m.patches) {
      auto i = candidate.find(patch.id);
      require(i != candidate.end());
      for (auto change : patch.changes) {
        require(change.offset < i->second.data.size());
        i->second.data[change.offset] = change.value;
      }
    }
    std::vector<Record> rebuilt;
    for (auto &[id, r] : candidate)
      rebuilt.push_back(std::move(r));
    recordsValid(rebuilt);
    records_ = std::move(rebuilt);
  } else
    records_ = std::move(m.records);
  snapshot_ = m.sequence;
}
Bytes ClientSession::command(EntityId target, std::uint32_t kind,
                             Bytes payload) {
  require(ready() && command_ != std::numeric_limits<std::uint32_t>::max());
  Message m;
  m.type = Type::Command;
  m.schema = schema_;
  m.session = session_;
  m.peer = peer_;
  m.sequence = command_ + 1;
  m.command = {peer_, target, m.sequence, kind, std::move(payload)};
  auto b = encode(m);
  ++command_;
  return b;
}
Bytes ClientSession::rpc(EntityId target,std::uint32_t kind,Bytes payload) {
  require(ready()&&rpc_!=std::numeric_limits<std::uint32_t>::max());
  Message m;m.type=Type::Rpc;m.schema=schema_;m.session=session_;m.peer=peer_;
  m.sequence=rpc_+1;m.command={peer_,target,m.sequence,kind,std::move(payload)};
  auto result=encode(m);++rpc_;return result;
}
std::vector<Command> ClientSession::takeEvents(){auto result=std::move(events_);events_.clear();return result;}
bool validEndpoint(const std::string &s) {
  if (s.size() > 2048 || s.find_first_of("\r\n\t #\\") != std::string::npos)
    return false;
  const bool secure = s.starts_with("wss://");
  if (!secure && !s.starts_with("ws://"))
    return false;
  for (unsigned char c : s)
    if (c < 33 || c > 126 || c == '"' || c == '<' || c == '>')
      return false;
  const std::size_t start = secure ? 6u : 5u;
  const auto end = s.find_first_of("/?", start);
  auto host = s.substr(start, end == std::string::npos ? s.size() - start
                                                       : end - start);
  if (host.empty() || host.find('@') != std::string::npos)
    return false;
  if (secure)
    return true;
  auto colon = host.find(':');
  auto name = host.substr(0, colon);
  return name == "127.0.0.1" || name == "localhost";
}
} // namespace engine::net
