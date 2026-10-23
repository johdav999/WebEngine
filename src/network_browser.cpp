#include "engine/network.hpp"
#include <emscripten/websocket.h>
#include <stdexcept>

namespace engine::net {
namespace {
class BrowserConnection final : public Connection {
  EMSCRIPTEN_WEBSOCKET_T socket_ = 0;
  ConnectionState state_ = ConnectionState::Connecting;
  std::string error_;
  std::vector<Bytes> incoming_;
  void fail(const char *text) {
    state_ = ConnectionState::Failed;
    error_ = text;
    incoming_.clear();
    if (socket_)
      emscripten_websocket_close(socket_, 1008, text);
  }

public:
  explicit BrowserConnection(const std::string &url) {
    EmscriptenWebSocketCreateAttributes attr{};
    attr.url = url.c_str();
    attr.createOnMainThread = true;
    socket_ = emscripten_websocket_new(&attr);
    if (socket_ <= 0)
      throw std::runtime_error("WebSocket unavailable");
    emscripten_websocket_set_onopen_callback(
        socket_, this, [](int, const EmscriptenWebSocketOpenEvent *, void *u) {
          static_cast<BrowserConnection *>(u)->state_ = ConnectionState::Open;
          return EM_TRUE;
        });
    emscripten_websocket_set_onerror_callback(
        socket_, this, [](int, const EmscriptenWebSocketErrorEvent *, void *u) {
          static_cast<BrowserConnection *>(u)->fail(
              "WebSocket connection failed");
          return EM_TRUE;
        });
    emscripten_websocket_set_onclose_callback(
        socket_, this, [](int, const EmscriptenWebSocketCloseEvent *, void *u) {
          auto &c = *static_cast<BrowserConnection *>(u);
          if (c.state_ != ConnectionState::Failed)
            c.state_ = ConnectionState::Closed;
          return EM_TRUE;
        });
    emscripten_websocket_set_onmessage_callback(
        socket_, this,
        [](int, const EmscriptenWebSocketMessageEvent *e, void *u) {
          auto &c = *static_cast<BrowserConnection *>(u);
          if (e->isText || e->numBytes > maxMessage ||
              c.incoming_.size() >= maxQueue)
            c.fail("WebSocket receive limit");
          else
            c.incoming_.emplace_back(e->data, e->data + e->numBytes);
          return EM_TRUE;
        });
  }
  ~BrowserConnection() override {
    if (socket_) {
      emscripten_websocket_set_onopen_callback(socket_, nullptr, nullptr);
      emscripten_websocket_set_onerror_callback(socket_, nullptr, nullptr);
      emscripten_websocket_set_onclose_callback(socket_, nullptr, nullptr);
      emscripten_websocket_set_onmessage_callback(socket_, nullptr, nullptr);
      emscripten_websocket_close(socket_, 1000, "Leaving");
      emscripten_websocket_delete(socket_);
    }
  }
  ConnectionState state() const override { return state_; }
  std::string error() const override { return error_; }
  bool send(std::span<const std::uint8_t> b) override {
    unsigned long buffered = 0;
    emscripten_websocket_get_buffered_amount(socket_, &buffered);
    if (state_ != ConnectionState::Open || b.empty() || b.size() > maxMessage ||
        buffered > maxMessage * maxQueue) {
      fail("WebSocket send limit");
      return false;
    }
    return emscripten_websocket_send_binary(
               socket_, const_cast<std::uint8_t *>(b.data()), b.size()) ==
           EMSCRIPTEN_RESULT_SUCCESS;
  }
  std::vector<Bytes> receive() override {
    auto v = std::move(incoming_);
    incoming_.clear();
    return v;
  }
};
} // namespace
std::unique_ptr<Connection> connectWebSocket(const std::string &url) {
  if (!validEndpoint(url))
    throw std::invalid_argument(
        "Use wss://, or ws://localhost for development");
  return std::make_unique<BrowserConnection>(url);
}
} // namespace engine::net
