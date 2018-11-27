#ifndef STDO_SERVER_H
#define STDO_SERVER_H

#include "stdo/stdo.h"
#include "stdo/winsupport.h"

#include <memory>
#include <type_traits>
#include <exception>
#include <vector>
#include <string_view>

namespace stdo::server {

// Maximum concurrent server connections. Being sudo, it's unlikely to have to
// process many things concurrently, but we have to give Windows a number.
constexpr int MaxPipeConnections = 10;

// Pipe timeout, again for Windows.
constexpr int PipeDefaultTimeout = 0;

// Server status codes
enum Status : int {
  StatusUnset = -1,
  StatusOk = 0,
  StatusCreatePipeFailed,
  StatusTimedOut,
  StatusEventFailed,
};

inline const char *statusToString(Status status) {
  switch (status) {
  default: return "unknown status";
  case StatusUnset: return "status not set";
  case StatusOk: return "ok";
  case StatusCreatePipeFailed: return "pipe creation failed";
  case StatusTimedOut: return "timed out";
  case StatusEventFailed: return "event failed";
  }
}

class EventListener;
class EventHandler;

class event_mutex_abandoned_error : public std::exception {
  std::unique_ptr<EventHandler> _handler;
public:
  explicit event_mutex_abandoned_error(std::unique_ptr<EventHandler> handler)
    noexcept
    : _handler(std::move(handler))
  {}

  const char *what() const noexcept override {
    return "Mutex abandoned";
  }

  const EventHandler *handler() const noexcept {
    return _handler.get();
  }

  // Allow you to take the handler and do something with it.
  std::unique_ptr<EventHandler> &handler() noexcept {
    return _handler;
  }
};

class event_wait_failed_error : public std::exception {
  DWORD _lastError;
public:
  explicit event_wait_failed_error(DWORD lastError) noexcept
    : _lastError(lastError)
  {}

  const char *what() const noexcept override {
    return "Mutex abandoned";
  }

  DWORD getLastError() const noexcept {
    return _lastError;
  }
};

enum class EventStatus {
  Finished,
  InProgress,
  Failed,
};

class EventListener;

class EventHandler {
public:
  virtual ~EventHandler() { }

  virtual HANDLE event() const = 0;
  virtual EventStatus operator()(EventListener &) = 0;
};

class SimpleEventHandler : public EventHandler {
  HObject _event;

public:
  explicit SimpleEventHandler() noexcept
    : _event{CreateEventW(nullptr, true, false, nullptr)}
  {}

  virtual HANDLE event() const override { return _event; }
  virtual EventStatus operator()(EventListener &) {
    return EventStatus::Finished;
  }
};

class EventHandlerOverlapped : public EventHandler {
protected:
  std::unique_ptr<OVERLAPPED> _overlapped;
  std::vector<char> _buffer;

  void resetBuffer() { _buffer.clear(); }
  EventStatus beginRead(HANDLE hFile);
  EventStatus beginWrite(HANDLE hFile);
  EventStatus endReadWrite(HANDLE hFile);

  // Interprets GetOverlappedResult and resets the event on success.
  DWORD getOverlappedResult(HANDLE hFile, DWORD *bytes = nullptr);

public:
  explicit EventHandlerOverlapped() noexcept
    : _overlapped(std::make_unique<OVERLAPPED>())
  {
    _overlapped->hEvent = CreateEventW(nullptr, true, false, nullptr);
    _buffer.reserve(PipeBufferSize);
  }

  EventHandlerOverlapped(EventHandlerOverlapped &&other) = default;

  ~EventHandlerOverlapped() {
    if (_overlapped) {
      CloseHandle(_overlapped->hEvent);
    }
  }

  EventHandlerOverlapped &operator=(EventHandlerOverlapped &&other) = default;

  HANDLE event() const override { return _overlapped->hEvent; }
};

class WaitForClientHandler : public EventHandlerOverlapped {

};

using HPipeConnection = Handle<HANDLE, DisconnectNamedPipe>;
class ClientConnectionHandler : public EventHandlerOverlapped {
  struct Callback {
    using Function = Callback (ClientConnectionHandler::*)();

    Callback() noexcept : function(nullptr) {}
    Callback(Function function) noexcept : function(function) {}
    Callback(const Callback &) = default;
    Callback &operator=(const Callback &) = default;

    Callback operator()(ClientConnectionHandler *self) {
      return (self->*function)();
    }

    explicit operator bool() const { return !!function; }

  private:
    Function function;
  };

  int _clientId;
  HPipeConnection _connection{};
  Callback _callback{};
  HObject _userToken{};

  void createResponse(const char *header,
                      std::string_view message = std::string_view{});

  Callback connect(HANDLE pipe);
  Callback finishConnect();
  Callback read();
  Callback finishRead();
  Callback respond();
  template<bool Loop>
  Callback finishRespond();
  Callback reset();

  // Returns true to read another message, false to reset the connection.
  bool dispatchMessage();
  bool tryToLogonUser(const char *username, const char *password);
  bool bless(HANDLE remoteHandle);

public:
  explicit ClientConnectionHandler(HANDLE pipe, int clientId) noexcept;

  EventStatus operator()(EventListener &) override {
    if (!_callback || !(_callback = _callback(this))) {
      return EventStatus::Failed;
    }
    return EventStatus::InProgress;
  }
};

class EventListener {
  std::vector<HANDLE> _events;
  std::vector<std::unique_ptr<EventHandler>> _handlers;
  constexpr static size_t ExitLoopIndex = 0;

  std::unique_ptr<EventHandler> remove(size_t index);

public:
  explicit EventListener()
    : _events{},
      _handlers{}
  {
    _events.reserve(MaxPipeConnections + 1);
    _handlers.reserve(MaxPipeConnections + 1);

    // Add the quit event
    push(SimpleEventHandler{});
  }

  template<typename H>
  std::enable_if_t<std::is_convertible_v<H &, EventHandler &>>
  push(H handler) {
    _events.emplace_back(handler.event());
    _handlers.emplace_back(std::make_unique<H>(std::move(handler)));
  }

  HANDLE quitEvent() const { return _events[ExitLoopIndex]; }

  Status eventLoop(DWORD timeout = INFINITE);
};

struct Config {
  // Named pipe filename.
  std::wstring pipeName;

  // Pointer to global quit event handle.
  HANDLE *quitEvent;

  // Server status return value.
  Status status = StatusUnset;

  explicit Config(std::wstring pipeName, HANDLE *quitEvent)
    : pipeName(std::move(pipeName)), quitEvent(quitEvent)
  {}
};

void serverMain(Config &config);

} // namespace stdo::server

#endif // STDO_SERVER_H

