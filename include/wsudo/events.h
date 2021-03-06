#ifndef WSUDO_EVENTS_H
#define WSUDO_EVENTS_H

#include <vector>
#include <cstdint>

#include "wsudo.h"

/**
 * Windows Event Server/Client
 * Uses WaitForMultipleObjects to execute callbacks on event completion.
 */

namespace wsudo::events {

// Status codes for the listener to manage individual handlers.
enum class EventStatus {
  // The event completed its work for this step.
  Ok,
  // The event has nothing more to do and should be removed from the list.
  Finished,
  // The event's state is invalid and it should be removed from the list.
  Failed,
};

class EventListener;

// A waitable event based on Windows timers.
// The callback in operator() is triggered when the event is signaled.
class EventHandler {
public:
  EventHandler() = default;
  EventHandler(const EventHandler &) = default;
  EventHandler &operator=(const EventHandler &) = default;

  EventHandler(EventHandler &&) = default;
  EventHandler &operator=(EventHandler &&) = default;

  virtual ~EventHandler() = 0;

  // The Windows event that should trigger this event.
  virtual HANDLE event() const = 0;

  // Optional - return true if the state was reset. The default implementation
  // does nothing and returns false.
  virtual bool reset();

  // Event handler implementation.
  virtual EventStatus operator()(EventListener &) = 0;
};

// Lambda wrapper event handler.
template<
  typename F,
  bool AllowReset = false,
  typename = std::enable_if_t<
               std::is_invocable_r_v<EventStatus, F, EventListener &>
             >
>
class EventCallback final : public EventHandler {
public:
  EventCallback(HANDLE event, F callback) noexcept
    : _callback(std::move(callback)),
      _event(event)
  {}

  HANDLE event() const override { return _event; }

  bool reset() override {
    if constexpr (AllowReset) {
      ResetEvent(_event);
      return true;
    } else {
      return false;
    }
  }

  EventStatus operator()(EventListener &listener) override {
    return _callback(listener);
  }

private:
  F _callback;
  HObject _event;
};

// Handles overlapped IO operations when the event is triggered. Inheriting from
// this class enables subclasses to easily use overlapped IO to incrementally
// read from a file handle and be notified when the entire message is received,
// or to write a large message all at once but send it in smaller chunks.
class EventOverlappedIO : public EventHandler {
public:
  EventOverlappedIO() = delete;
  /// @param isEventSet Should action be taken immediately (true), or should we
  /// wait until the event is triggered another way (false)?
  explicit EventOverlappedIO(bool isEventSet) noexcept;
  ~EventOverlappedIO();

  // This class is not copyable or movable because the embedded
  // OVERLAPPED must remain at the same memory address.

  EventOverlappedIO(const EventOverlappedIO &) = delete;
  EventOverlappedIO &operator=(const EventOverlappedIO &) = delete;

  EventOverlappedIO(EventOverlappedIO &&) = delete;
  EventOverlappedIO &operator=(EventOverlappedIO &&) = delete;

  // Returns false, but succeeds. Subclasses can override this to reset their
  // own state and return true. If this is overridden, a subclass must call
  // EventOverlappedIO::reset().
  bool reset() override;

  // Returns the overlapped trigger event.
  HANDLE event() const override { return _overlapped.hEvent; }

  // Subclasses should call this first to handle chunked reading/writing.
  // Returns EventStatus::Finished when reading/writing is done.
  EventStatus operator()(EventListener &) override;

protected:
  OVERLAPPED _overlapped{};
  std::vector<uint8_t> _buffer{};

  // Subclasses should return an overlapped readable/writable handle here.
  virtual HANDLE fileHandle() const = 0;

  // Begin reading from the file handle. Subclasses must call operator() for
  // this to work.
  EventStatus readToBuffer() { _offset = 0; return beginRead(); }

  // Begin writing to the file handle. Subclasses must call operator() for
  // this to work.
  EventStatus writeFromBuffer() { _offset = 0; return beginWrite(); }

private:
  // Position in buffer to begin reading or writing, depending on IO state.
  size_t _offset = 0;

  // Determines if any IO action needs to take place.
  enum class IOState {
    // No IO is queued.
    Inactive,
    // We are waiting to read from the file.
    Reading,
    // We are waiting to write to the file.
    Writing,
    // Reading or writing failed.
    Failed,
  } _ioState{IOState::Inactive};

  const size_t BufferDoublingLimit = 4;

  // Max amount to read/write at once.
  const DWORD ChunkSize = static_cast<DWORD>(PipeBufferSize);

  // Begins an overlapped read operation.
  EventStatus beginRead();
  // Finishes an overlapped read operation. Not all the data may be read at
  // this point. See EventOverlappedIO::operator().
  EventStatus endRead();
  // Begins an overlapped write operation.
  EventStatus beginWrite();
  // Finishes an overlapped write operation. Not all the data may be written at
  // this point. See EventOverlappedIO::operator().
  EventStatus endWrite();
};

// Manages a set of event handlers.
class EventListener final {
public:
  explicit EventListener() = default;

  EventListener(const EventListener &) = delete;
  EventListener &operator=(const EventListener &) = delete;

  EventListener(EventListener &&) = default;
  EventListener &operator=(EventListener &&) = default;

  // Construct a handler in place.
  template<typename H, typename... Args>
  std::enable_if_t<
    std::conjunction_v<
      std::is_convertible<H &, EventHandler &>,
      std::is_constructible<H, Args...>
    >,
    H &
  >
  emplace(Args &&...args) {
    auto &handler = static_cast<H &>(
      *_handlers.emplace_back(std::make_unique<H>(std::forward<Args>(args)...))
    );
    _events.emplace_back(handler.event());
    return handler;
  }

  // Add a function object or lambda event handler with a custom event object.
  // If AllowReset is true, the event will be reset and the handler reused
  // after it finishes.
  template<bool AllowReset = false, typename F>
  EventCallback<F, AllowReset> &
  emplace(HANDLE event, F callback) {
    return emplace<EventCallback<F, AllowReset>>(event, std::move(callback));
  }

  // Run one iteration of the event loop.
  EventStatus next(DWORD timeout = INFINITE);

  // Run the event loop until a quit is triggered. Returns Finished or Failed.
  EventStatus run(DWORD timeout = INFINITE);

  // Return the number of events in the queue.
  size_t count() const {
    assert(_events.size() == _handlers.size());
    return _events.size();
  }

  bool isRunning() const { return _running; }
  void stop() { _running = false; }

private:
  // List of events to pass to WaitForMultipleObjects.
  std::vector<HANDLE> _events;
  // List of handlers, must be kept in sync with the event list.
  std::vector<std::unique_ptr<EventHandler>> _handlers;
  // Active flag.
  bool _running;

  // Remove an event handler from the list.
  void remove(size_t index);
};

} // namespace wsudo::events

#endif // WSUDO_EVENTS_H
