#pragma once
#include "engine/network.hpp"

namespace engine::net {
enum class LobbyOperation : std::uint32_t { Create=1,Join,Resume };
enum class LobbyMessageType : std::uint32_t { Request=1,Accepted,Rejected,Leave,Left,Confirm,Roster };
struct LobbyRequest {
  LobbyOperation operation=LobbyOperation::Create;
  std::string guestName="Guest",roomName="Game",invitation,sessionToken;
};
// Sensitive capabilities: retain in memory; never write tokens to diagnostics,
// URLs, source scene archives, logs or a game's ordinary save data.
struct LobbyCredentials {
  std::string guestId,roomId,sessionToken,invitation;
  std::uint32_t graceSeconds=30;
  bool resumed=false,host=false;
};
struct LobbyMember {std::string id,name;bool online=false,host=false;};
struct LobbyRoster {std::string roomId,name;std::uint32_t capacity=16;bool started=false;std::vector<LobbyMember> members;};
struct LobbyMessage {
  LobbyMessageType type=LobbyMessageType::Request;
  LobbyRequest request;
  LobbyCredentials credentials;
  LobbyRoster roster;
  std::string reason;
};
bool isLobby(std::span<const std::uint8_t>);
Bytes encodeLobby(const LobbyMessage &);
LobbyMessage decodeLobby(std::span<const std::uint8_t>);

// Owns a host adapter, bounded handshake/roster buffers and in-memory guest
// capabilities. Single owner thread; no retained callbacks or scene access.
// state()/receive()/send() pump the handshake. Open means authentication complete.
class LobbyConnection final : public Connection {
public:
  LobbyConnection(std::unique_ptr<Connection>,LobbyRequest);
  ConnectionState state() const override;
  std::string error() const override;
  bool send(std::span<const std::uint8_t>) override;
  std::vector<Bytes> receive() override;
  const LobbyCredentials &credentials() const {service();return credentials_;}
  const LobbyRoster &roster() const {service();return roster_;}
  bool rejected() const {service();return rejected_;}
  void leave(); // Asynchronous; continue pumping until Left/close.
private:
  void service() const;
  mutable std::unique_ptr<Connection> socket_;
  LobbyRequest request_;
  mutable LobbyCredentials credentials_;
  mutable LobbyRoster roster_;
  mutable ConnectionState state_=ConnectionState::Connecting;
  mutable std::string error_;
  mutable bool sent_=false,accepted_=false,rejected_=false,leaving_=false;
  mutable std::vector<Bytes> pending_;
};
inline std::unique_ptr<LobbyConnection> connectLobby(const std::string &url,LobbyRequest request){
  return std::make_unique<LobbyConnection>(connectWebSocket(url),std::move(request));
}
} // namespace engine::net
