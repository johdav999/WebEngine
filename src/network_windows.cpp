#include "engine/network.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <windows.h>
#include <winhttp.h>

namespace engine::net {
namespace {
struct Handle {
  HINTERNET h = nullptr;
  ~Handle() {
    if (h)
      WinHttpCloseHandle(h);
  }
  operator HINTERNET() const { return h; }
};
struct Resources {
  Handle session, connection, request, socket;
};
class WindowsConnection final : public Connection {
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  ConnectionState state_ = ConnectionState::Connecting;
  std::string error_;
  std::vector<Bytes> incoming_;
  std::deque<Bytes> outgoing_;
  std::atomic<bool> stop_ = false;
  std::shared_ptr<Resources> resources_;
  std::thread reader_, writer_;
  void fail(const char *s) {
    std::lock_guard lock(mutex_);
    if (!stop_) {
      state_ = ConnectionState::Failed;
      error_ = s;
    }
    cv_.notify_all();
  }
  void read(std::string url) {
    // Network IO stays off the scene owner. Connection setup has bounded
    // timeouts.
    std::wstring wide(url.begin(), url.end());
    wide.replace(0, url.starts_with("wss:") ? 3 : 2,
                 url.starts_with("wss:") ? L"https" : L"http");
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        DWORD(-1);
    if (!WinHttpCrackUrl(wide.c_str(), DWORD(wide.size()), 0, &parts)) {
      fail("Invalid WebSocket URL");
      return;
    }
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength),
        path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (path.empty())
      path = L"/";
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    auto resources = std::make_shared<Resources>();
    auto &session = resources->session;
    auto &connection = resources->connection;
    auto &request = resources->request;
    auto &socket = resources->socket;
    session.h = WinHttpOpen(L"WebEngine Multiplayer/1",
                            WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    if (!session.h) {
      fail("WinHTTP unavailable");
      return;
    }
    WinHttpSetTimeouts(session, 2000, 2000, 2000, 2000);
    connection.h = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    request.h = WinHttpOpenRequest(
        connection, L"GET", path.c_str(), nullptr, nullptr, nullptr,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!request.h ||
        !WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                          nullptr, 0) ||
        !WinHttpSendRequest(request, nullptr, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
      fail("WebSocket handshake failed");
      return;
    }
    socket.h = WinHttpWebSocketCompleteUpgrade(request, 0);
    if (!socket.h) {
      fail("WebSocket upgrade failed");
      return;
    }
    {
      std::lock_guard lock(mutex_);
      if (stop_)
        return;
      resources_ = resources;
      state_ = ConnectionState::Open;
    }
    cv_.notify_all();
    Bytes message;
    std::uint8_t buffer[4096];
    while (!stop_) {
      DWORD n = 0;
      WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
      auto result =
          WinHttpWebSocketReceive(socket, buffer, sizeof(buffer), &n, &type);
      if (result != NO_ERROR) {
        if (!stop_)
          fail("WebSocket receive failed");
        break;
      }
      if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
        std::lock_guard lock(mutex_);
        state_ = ConnectionState::Closed;
        break;
      }
      if ((type != WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE &&
           type != WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE) ||
          message.size() + n > maxMessage) {
        fail("WebSocket receive limit");
        break;
      }
      message.insert(message.end(), buffer, buffer + n);
      if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
        bool overflow = false;
        {
          std::lock_guard lock(mutex_);
          overflow = incoming_.size() >= maxQueue;
          if (!overflow)
            incoming_.push_back(std::move(message));
        }
        message.clear();
        if (overflow) {
          fail("WebSocket queue limit");
          break;
        }
      }
    }
    // In-flight sends retain the socket and all its parent handles. No IO is
    // performed under the queue mutex, so owner-thread polling never waits on
    // IO.
    {
      std::lock_guard lock(mutex_);
      resources_.reset();
      if (state_ == ConnectionState::Open)
        state_ = ConnectionState::Closed;
    }
    cv_.notify_all();
  }
  void write() {
    std::unique_lock lock(mutex_);
    while (!stop_) {
      cv_.wait(lock, [&] {
        return stop_ || (resources_ && !outgoing_.empty()) ||
               state_ == ConnectionState::Failed ||
               state_ == ConnectionState::Closed;
      });
      if (stop_ || state_ == ConnectionState::Failed ||
          state_ == ConnectionState::Closed)
        break;
      auto b = std::move(outgoing_.front());
      outgoing_.pop_front();
      auto resources = resources_;
      lock.unlock();
      const auto result = WinHttpWebSocketSend(
          resources->socket, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
          b.data(), DWORD(b.size()));
      lock.lock();
      if (result != NO_ERROR) {
        state_ = ConnectionState::Failed;
        error_ = "WebSocket send failed";
        break;
      }
    }
  }

public:
  explicit WindowsConnection(std::string url) {
    reader_ = std::thread([this, url = std::move(url)] { read(url); });
    try {
      writer_ = std::thread([this] { write(); });
    } catch (...) {
      stop_ = true;
      reader_.join();
      throw;
    }
  }
  ~WindowsConnection() override {
    stop_ = true;
    cv_.notify_all();
    if (writer_.joinable())
      writer_.join();
    std::shared_ptr<Resources> resources;
    {
      std::lock_guard lock(mutex_);
      resources = resources_;
    }
    if (resources)
      WinHttpWebSocketShutdown(resources->socket,
                               WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr,
                               0);
    if (reader_.joinable())
      reader_.join();
  }
  ConnectionState state() const override {
    std::lock_guard lock(mutex_);
    return state_;
  }
  std::string error() const override {
    std::lock_guard lock(mutex_);
    return error_;
  }
  bool send(std::span<const std::uint8_t> b) override {
    std::lock_guard lock(mutex_);
    if (state_ != ConnectionState::Open || b.empty() || b.size() > maxMessage ||
        outgoing_.size() >= maxQueue)
      return false;
    outgoing_.emplace_back(b.begin(), b.end());
    cv_.notify_all();
    return true;
  }
  std::vector<Bytes> receive() override {
    std::lock_guard lock(mutex_);
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
  return std::make_unique<WindowsConnection>(url);
}
} // namespace engine::net
