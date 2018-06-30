#ifndef STDO_STDO_H
#define STDO_STDO_H

#pragma warning(push)
#pragma warning(disable:4996) // codecvt deprecated
#define SPDLOG_WCHAR_FILENAMES
#define SPDLOG_WCHAR_TO_UTF8_SUPPORT
#define SPDLOG_NO_NAME
#include "spdlog/spdlog.h"
#pragma warning(pop)

#include <cstdint>

namespace stdo {

extern const wchar_t *PipeName;
extern const wchar_t *PipeFullPath;
constexpr size_t PipeBufferSize = 1024;

extern const char *MsgHeaderCredential;
extern const char *MsgHeaderBless;

extern const char *SMsgHeaderSuccess;
extern const char *SMsgHeaderInvalidMessage;
extern const char *SMsgHeaderInternalError;
extern const char *SMsgHeaderAccessDenied;

namespace log {

extern std::shared_ptr<spdlog::logger> g_outLogger;
extern std::shared_ptr<spdlog::logger> g_errLogger;

template<typename... Args>
static inline void trace(Args &&...args)
{ g_outLogger->trace(std::forward<Args>(args)...); }

template<typename... Args>
static inline void debug(Args &&...args)
{ g_outLogger->debug(std::forward<Args>(args)...); }

template<typename... Args>
static inline void info(Args &&...args)
{ g_outLogger->info(std::forward<Args>(args)...); }

template<typename... Args>
static inline void warn(Args &&...args)
{ g_errLogger->warn(std::forward<Args>(args)...); }

template<typename... Args>
static inline void error(Args &&...args)
{ g_errLogger->error(std::forward<Args>(args)...); }

template<typename... Args>
static inline void critical(Args &&...args)
{ g_errLogger->critical(std::forward<Args>(args)...); }

} // namespace log

template<typename OnExit>
class ScopeExit {
  std::enable_if_t<std::is_invocable_v<OnExit>, OnExit> _onExit;
public:
  explicit ScopeExit(OnExit onExit) : _onExit(std::move(onExit)) {}
  ScopeExit<OnExit> &operator=(const ScopeExit<OnExit> &) = delete;
  ~ScopeExit() {
    _onExit();
  }
};

template<typename OnExit>
ScopeExit<OnExit> makeScopeExit(OnExit onExit) {
  return ScopeExit<OnExit>{std::move(onExit)};
}

namespace detail {
  struct ScopeExitHelper{};

  template<typename OnExit>
  ScopeExit<OnExit> operator&&(const detail::ScopeExitHelper &, OnExit onExit) {
    return ScopeExit<OnExit>{std::move(onExit)};
  }
}

} // namespace stdo

#define STDO_CONCAT_IMPL(a,b) a##b
#define STDO_CONCAT2(a,b) STDO_CONCAT_IMPL(a,b)
#define STDO_CONCAT3(a,b,c) STDO_CONCAT2(STDO_CONCAT2(a,b),c)
#define STDO_CONCAT4(a,b,c,d) STDO_CONCAT2(STDO_CONCAT2(a,b),STDO_CONCAT2(c,d))

/// Usage: STDO_SCOPEEXIT { capture-by-ref lambda body };
#define STDO_SCOPEEXIT \
  auto STDO_CONCAT4(_scopeExit_, __func__, _, __LINE__) = \
    ::stdo::detail::ScopeExitHelper{} && [&]()

#define STDO_SCOPEEXIT_THIS \
  auto STDO_CONCAT4(_scopeExit_, __func__, _, __LINE__) = \
    ::stdo::detail::ScopeExitHelper{} && [&, this]()

#endif // STDO_STDO_H

