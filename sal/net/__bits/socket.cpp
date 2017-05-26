#include <sal/net/__bits/socket.hpp>
#include <sal/net/error.hpp>

#if __sal_os_windows
  #include <mswsock.h>
  #include <mutex>
#elif __sal_os_darwin || __sal_os_linux
  #include <fcntl.h>
  #include <poll.h>
  #include <signal.h>
  #include <sys/ioctl.h>
  #include <unistd.h>
#endif


__sal_begin


namespace net { namespace __bits {


//
// Synchronous API
//


#if __sal_os_windows // {{{1


namespace {


struct winsock_t
{
  winsock_t () noexcept
  {
    init_lib();
  }
} sys{};

LPFN_CONNECTEX ConnectEx{};
LPFN_ACCEPTEX AcceptEx{};
LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrs{};


template <typename T>
inline T check_call (T result, std::error_code &error) noexcept
{
  if (result != SOCKET_ERROR)
  {
    error.clear();
  }
  else
  {
    auto e = ::WSAGetLastError();
    if (e == WSAENOTSOCK)
    {
      e = WSAEBADF;
    }
    error.assign(e, std::system_category());
  }
  return result;
}

#define call(error,func,...) check_call(func(__VA_ARGS__), error)


template <typename F>
void load (F *fn, GUID id, SOCKET socket, std::error_code &error) noexcept
{
  if (!error)
  {
    DWORD bytes;
    call(error, ::WSAIoctl, socket,
      SIO_GET_EXTENSION_FUNCTION_POINTER,
      &id, sizeof(id),
      &fn, sizeof(fn),
      &bytes,
      nullptr,
      nullptr
    );
  }
}


void load (RIO_EXTENSION_FUNCTION_TABLE *rio, SOCKET socket,
  std::error_code &error) noexcept
{
  if (!error)
  {
    DWORD bytes;
    GUID id = WSAID_MULTIPLE_RIO;
    call(error, ::WSAIoctl, socket,
      SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
      &id, sizeof(id),
      rio, sizeof(*rio),
      &bytes,
      nullptr,
      nullptr
    );
  }
}


void init_winsock (std::error_code &init_result) noexcept
{
  WSADATA wsa;
  init_result.assign(
    ::WSAStartup(MAKEWORD(2, 2), &wsa),
    std::system_category()
  );

  if (!init_result)
  {
    auto s = ::socket(AF_INET, SOCK_STREAM, 0);

    load(&ConnectEx, WSAID_CONNECTEX, s, init_result);
    load(&AcceptEx, WSAID_ACCEPTEX, s, init_result);
    load(&GetAcceptExSockaddrs, WSAID_GETACCEPTEXSOCKADDRS, s, init_result);

    RIO_EXTENSION_FUNCTION_TABLE rio;
    load(&rio, s, init_result);
    if (!init_result)
    {
      // TODO assign rio functions
    }

    (void)::closesocket(s);
  }
}


} // namespace


const std::error_code &init_lib () noexcept
{
  static std::once_flag once;
  static std::error_code init_result;
  std::call_once(once, &init_winsock, std::ref(init_result));
  return init_result;
}


void socket_t::open (int domain, int type, int protocol,
  std::error_code &error) noexcept
{
  handle = call(error, ::WSASocketW,
    domain,
    type,
    protocol,
    nullptr,
    0,
    WSA_FLAG_OVERLAPPED
  );

  if (handle != SOCKET_ERROR)
  {
    // we handle immediate completion by deferring handling
    ::SetFileCompletionNotificationModes(
      reinterpret_cast<HANDLE>(handle),
      FILE_SKIP_COMPLETION_PORT_ON_SUCCESS |
      FILE_SKIP_SET_EVENT_ON_HANDLE
    );

    if (type == SOCK_DGRAM)
    {
      bool new_behaviour = false;
      DWORD ignored;
      ::WSAIoctl(handle,
        SIO_UDP_CONNRESET,
        &new_behaviour, sizeof(new_behaviour),
        nullptr, 0,
        &ignored,
        nullptr,
        nullptr
      );
    }
  }
}


void socket_t::close (std::error_code &error) noexcept
{
  call(error, ::closesocket, handle);
  handle = invalid;
}


void socket_t::bind (const void *address, size_t address_size,
  std::error_code &error) noexcept
{
  call(error, ::bind,
    handle,
    static_cast<const sockaddr *>(address),
    static_cast<socklen_t>(address_size)
  );
}


void socket_t::listen (int backlog, std::error_code &error) noexcept
{
  call(error, ::listen, handle, backlog);
}


socket_t::handle_t socket_t::accept (void *address, size_t *address_size,
  bool enable_connection_aborted, std::error_code &error) noexcept
{
  socklen_t size{}, *size_p = nullptr;
  if (address_size)
  {
    size = static_cast<socklen_t>(*address_size);
    size_p = &size;
  }

retry:
  auto new_socket = call(error, ::accept,
    handle,
    static_cast<sockaddr *>(address),
    size_p
  );

  if (new_socket != invalid)
  {
    if (address_size)
    {
      *address_size = size;
    }
    return new_socket;
  }

  if (error == std::errc::connection_aborted && !enable_connection_aborted)
  {
    goto retry;
  }

  return 0;
}


void socket_t::connect (const void *address, size_t address_size,
  std::error_code &error) noexcept
{
  call(error, ::connect,
    handle,
    static_cast<const sockaddr *>(address),
    static_cast<socklen_t>(address_size)
  );
}


bool socket_t::wait (wait_t what, int timeout_ms, std::error_code &error)
  noexcept
{
  pollfd fd{};
  fd.fd = handle;
  fd.events = what == wait_t::read ? POLLIN : POLLOUT;

  auto event_count = ::WSAPoll(&fd, 1, timeout_ms);
  if (event_count == 1)
  {
    return (fd.revents & fd.events) != 0;
  }
  else if (event_count == -1)
  {
    check_call(invalid, error);
  }

  return false;
}


size_t socket_t::receive (void *data, size_t data_size, message_flags_t flags,
  std::error_code &error) noexcept
{
  WSABUF buf;
  buf.buf = static_cast<char *>(data);
  buf.len = static_cast<ULONG>(data_size);

  DWORD transferred{};
  DWORD recv_flags = flags;

  auto result = call(error, ::WSARecv,
    handle,
    &buf, 1,
    &transferred,
    &recv_flags,
    nullptr,
    nullptr
  );

  if (result != SOCKET_ERROR || error.value() == WSAESHUTDOWN)
  {
    if (transferred)
    {
      return transferred;
    }
    error = make_error_code(std::errc::broken_pipe);
  }

  return 0;
}


size_t socket_t::receive_from (void *data, size_t data_size,
  void *address, size_t *address_size,
  message_flags_t flags,
  std::error_code &error) noexcept
{
  WSABUF buf;
  buf.buf = static_cast<char *>(data);
  buf.len = static_cast<ULONG>(data_size);

  auto tmp_address_size = static_cast<INT>(*address_size);

  DWORD transferred{};
  DWORD recv_flags = flags;

  auto result = call(error, ::WSARecvFrom,
    handle,
    &buf, 1,
    &transferred,
    &recv_flags,
    static_cast<sockaddr *>(address),
    (address ? &tmp_address_size : nullptr),
    nullptr,
    nullptr
  );

  if (result != SOCKET_ERROR)
  {
    *address_size = tmp_address_size;
    return transferred;
  }

  return 0;
}


size_t socket_t::send (const void *data, size_t data_size, message_flags_t flags,
  std::error_code &error) noexcept
{
  WSABUF buf;
  buf.buf = static_cast<char *>(const_cast<void *>(data));
  buf.len = static_cast<ULONG>(data_size);

  DWORD transferred{};

  auto result = call(error, ::WSASend,
    handle,
    &buf, 1,
    &transferred,
    flags,
    nullptr,
    nullptr
  );

  if (result != SOCKET_ERROR)
  {
    return transferred;
  }
  else if (error.value() == WSAESHUTDOWN)
  {
    error = make_error_code(std::errc::broken_pipe);
  }

  return 0;
}


size_t socket_t::send_to (const void *data, size_t data_size,
  const void *address, size_t address_size,
  message_flags_t flags,
  std::error_code &error) noexcept
{
  WSABUF buf;
  buf.buf = static_cast<char *>(const_cast<void *>(data));
  buf.len = static_cast<ULONG>(data_size);

  DWORD transferred{};

  auto result = call(error, ::WSASendTo,
    handle,
    &buf, 1,
    &transferred,
    flags,
    static_cast<const sockaddr *>(address),
    static_cast<int>(address_size),
    nullptr,
    nullptr
  );

  if (result != SOCKET_ERROR)
  {
    return transferred;
  }

  return 0;
}


void socket_t::shutdown (shutdown_t what, std::error_code &error) noexcept
{
  call(error, ::shutdown, handle, static_cast<int>(what));
}


void socket_t::remote_endpoint (void *address, size_t *address_size,
  std::error_code &error) const noexcept
{
  auto size = static_cast<socklen_t>(*address_size);
  auto result = call(error, ::getpeername,
    handle,
    static_cast<sockaddr *>(address), &size
  );
  if (result != -1)
  {
    *address_size = size;
  }
}


void socket_t::local_endpoint (void *address, size_t *address_size,
  std::error_code &error) const noexcept
{
  auto size = static_cast<socklen_t>(*address_size);
  auto result = call(error, ::getsockname,
    handle,
    static_cast<sockaddr *>(address), &size
  );
  if (result != -1)
  {
    *address_size = size;
  }
}


void socket_t::get_opt (int level, int name, void *data, size_t *size,
  std::error_code &error) const noexcept
{
  auto data_size = static_cast<socklen_t>(*size);
  auto result = call(error, ::getsockopt,
    handle,
    level, name,
    static_cast<char *>(data), &data_size
  );
  if (result != -1)
  {
    *size = data_size;
  }
}


void socket_t::set_opt (int level, int name, const void *data, size_t size,
  std::error_code &error) noexcept
{
  call(error, ::setsockopt,
    handle,
    level, name,
    static_cast<const char *>(data), static_cast<socklen_t>(size)
  );
}


bool socket_t::non_blocking (std::error_code &error) const noexcept
{
  error.assign(WSAEOPNOTSUPP, std::system_category());
  return true;
}


void socket_t::non_blocking (bool mode, std::error_code &error) noexcept
{
  unsigned long arg = mode ? 1 : 0;
  call(error, ::ioctlsocket, handle, FIONBIO, &arg);
}


size_t socket_t::available (std::error_code &error) const noexcept
{
  unsigned long value{};
  if (call(error, ::ioctlsocket, handle, FIONBIO, &value) == SOCKET_ERROR)
  {
    value = 0;
  }
  return value;
}


#elif __sal_os_darwin || __sal_os_linux // {{{1


namespace {


template <typename T>
inline T check_call (T result, std::error_code &error) noexcept
{
  if (result != -1)
  {
    error.clear();
  }
  else
  {
    if (errno == EDESTADDRREQ)
    {
      errno = ENOTCONN;
    }
    error.assign(errno, std::generic_category());
  }
  return result;
}

#define call(error,func,...) check_call(func(__VA_ARGS__), error)


} // namspace


const std::error_code &init_lib () noexcept
{
  static const std::error_code no_error{};
  return no_error;
}


void socket_t::open (int domain, int type, int protocol,
  std::error_code &error) noexcept
{
  handle = call(error, ::socket, domain, type, protocol);

#if __sal_os_darwin
  if (handle != invalid)
  {
    int optval = 1;
    (void)::setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE,
      &optval, sizeof(optval)
    );
  }
#endif
}


void socket_t::close (std::error_code &error) noexcept
{
  for (;;)
  {
    if (call(error, ::close, handle) == 0 || errno != EINTR)
    {
      handle = invalid;
      return;
    }
  }
}


void socket_t::bind (const void *address, size_t address_size,
  std::error_code &error) noexcept
{
  call(error, ::bind,
    handle,
    static_cast<const sockaddr *>(address),
    static_cast<socklen_t>(address_size)
  );
}


void socket_t::listen (int backlog, std::error_code &error) noexcept
{
  call(error, ::listen, handle, backlog);
}


socket_t::handle_t socket_t::accept (void *address, size_t *address_size,
  bool enable_connection_aborted, std::error_code &error) noexcept
{
  socklen_t size{}, *size_p = nullptr;
  if (address_size)
  {
    size = static_cast<socklen_t>(*address_size);
    size_p = &size;
  }

retry:
  auto new_socket = call(error, ::accept,
    handle,
    static_cast<sockaddr *>(address),
    size_p
  );

  if (new_socket != invalid)
  {
    if (address_size)
    {
#if __sal_os_darwin
      // LCOV_EXCL_START
      // kernel bug: instead of ECONNABORTED, we might get size=0
      if (!size)
      {
        if (enable_connection_aborted)
        {
          error.assign(ECONNABORTED, std::generic_category());
          return invalid;
        }
        size = static_cast<socklen_t>(*address_size);
        goto retry;
      }
      // LCOV_EXCL_STOP
#endif
      *address_size = size;
    }

#if __sal_os_darwin
    int optval = 1;
    ::setsockopt(new_socket, SOL_SOCKET, SO_NOSIGPIPE,
      &optval, sizeof(optval)
    );
#endif

    return new_socket;
  }

  // LCOV_EXCL_START
  // OS-dependent section

#if __sal_os_linux
  // see accept(2), these are already pending errors
  if (errno == ENETDOWN
    || errno == EPROTO
    || errno == ENOPROTOOPT
    || errno == EHOSTDOWN
    || errno == ENONET
    || errno == EHOSTUNREACH
    || errno == EOPNOTSUPP
    || errno == ENETUNREACH)
  {
    goto retry;
  }
#endif

  if (error == std::errc::connection_aborted && !enable_connection_aborted)
  {
    goto retry;
  }

  // LCOV_EXCL_STOP

  return 0;
}


void socket_t::connect (const void *address, size_t address_size,
  std::error_code &error) noexcept
{
  call(error, ::connect,
    handle,
    static_cast<const sockaddr *>(address),
    static_cast<socklen_t>(address_size)
  );
}


bool socket_t::wait (wait_t what, int timeout_ms, std::error_code &error)
  noexcept
{
  if (handle == invalid)
  {
    error.assign(EBADF, std::generic_category());
    return false;
  }

  pollfd fd{};
  fd.fd = handle;
  fd.events = what == wait_t::read ? POLLIN : POLLOUT;

  auto event_count = ::poll(&fd, 1, timeout_ms);
  if (event_count == 1)
  {
#if __sal_os_linux
    // Linux does the "right thing", setting POLLHUP on non-connected sockets
    // unfortunately Darwin & Windows disagree and no way to detect such
    // situation, so simply align after their behaviour
    if (fd.revents & POLLHUP)
    {
      return false;
    }
#endif
    return (fd.revents & fd.events) != 0;
  }

  // LCOV_EXCL_START
  // can't reach this case by feeding incorrect parameters
  else if (event_count == -1)
  {
    check_call(invalid, error);
  }
  // LCOV_EXCL_STOP

  return false;
}


size_t socket_t::receive (void *data, size_t data_size, message_flags_t flags,
  std::error_code &error) noexcept
{
  iovec iov;
  iov.iov_base = data;
  iov.iov_len = data_size;

  msghdr msg{};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

#if __sal_os_linux
  flags |= MSG_NOSIGNAL;
#endif

  auto size = call(error, ::recvmsg, handle, &msg, flags);
  if (!size && data_size)
  {
    error = make_error_code(std::errc::broken_pipe);
  }
  else if (size == -1)
  {
    size = 0;
  }
  else if (msg.msg_flags & MSG_TRUNC)
  {
    error.assign(EMSGSIZE, std::generic_category());
    size = 0;
  }
  return size;
}


size_t socket_t::receive_from (void *data, size_t data_size,
  void *address, size_t *address_size,
  message_flags_t flags,
  std::error_code &error) noexcept
{
  iovec iov;
  iov.iov_base = data;
  iov.iov_len = data_size;

  msghdr msg{};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_name = address;
  msg.msg_namelen = *address_size;

#if __sal_os_linux
  flags |= MSG_NOSIGNAL;
#endif

  auto size = call(error, ::recvmsg, handle, &msg, flags);
  if (size >= 0)
  {
    if (msg.msg_flags & MSG_TRUNC)
    {
      error.assign(EMSGSIZE, std::generic_category());
      size = 0;
    }
    else
    {
      *address_size = msg.msg_namelen;
    }
  }
  else
  {
    size = 0;
  }
  return size;
}


size_t socket_t::send (const void *data, size_t data_size, message_flags_t flags,
  std::error_code &error) noexcept
{
  iovec iov;
  iov.iov_base = const_cast<void *>(data);
  iov.iov_len = data_size;

  msghdr msg{};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

#if __sal_os_linux
  flags |= MSG_NOSIGNAL;
#endif

  auto size = call(error, ::sendmsg, handle, &msg, flags);
  if (size == -1)
  {
    size = 0;
  }
  return size;
}


size_t socket_t::send_to (const void *data, size_t data_size,
  const void *address, size_t address_size,
  message_flags_t flags,
  std::error_code &error) noexcept
{
  iovec iov;
  iov.iov_base = const_cast<void *>(data);
  iov.iov_len = data_size;

  msghdr msg{};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_name = const_cast<void *>(address);
  msg.msg_namelen = address_size;

#if __sal_os_linux
  flags |= MSG_NOSIGNAL;
#endif

  auto size = call(error, ::sendmsg, handle, &msg, flags);
  if (size == -1)
  {
    size = 0;
  }
  return size;
}


void socket_t::shutdown (shutdown_t what, std::error_code &error) noexcept
{
  call(error, ::shutdown, handle, static_cast<int>(what));
}


void socket_t::remote_endpoint (void *address, size_t *address_size,
  std::error_code &error) const noexcept
{
  auto size = static_cast<socklen_t>(*address_size);
  auto result = call(error, ::getpeername,
    handle,
    static_cast<sockaddr *>(address), &size
  );
  if (result != -1)
  {
    *address_size = size;
  }
}


void socket_t::local_endpoint (void *address, size_t *address_size,
  std::error_code &error) const noexcept
{
  auto size = static_cast<socklen_t>(*address_size);
  auto result = call(error, ::getsockname,
    handle,
    static_cast<sockaddr *>(address), &size
  );
  if (result != -1)
  {
    *address_size = size;
  }
}


void socket_t::get_opt (int level, int name, void *data, size_t *size,
  std::error_code &error) const noexcept
{
  auto data_size = static_cast<socklen_t>(*size);
  auto result = call(error, ::getsockopt,
    handle,
    level, name,
    static_cast<char *>(data), &data_size
  );
  if (result != -1)
  {
    *size = data_size;
  }
}


void socket_t::set_opt (int level, int name, const void *data, size_t size,
  std::error_code &error) noexcept
{
  call(error, ::setsockopt,
    handle,
    level, name,
    static_cast<const char *>(data), static_cast<socklen_t>(size)
  );
}


bool socket_t::non_blocking (std::error_code &error) const noexcept
{
  // on error, returned value is undefined
  return O_NONBLOCK & call(error, ::fcntl, handle, F_GETFL, 0);
}


void socket_t::non_blocking (bool mode, std::error_code &error) noexcept
{
  int flags = call(error, ::fcntl, handle, F_GETFL, 0);
  if (flags >= 0)
  {
    if (mode)
    {
      flags |= O_NONBLOCK;
    }
    else
    {
      flags &= ~O_NONBLOCK;
    }
    call(error, ::fcntl, handle, F_SETFL, flags);
  }
}


size_t socket_t::available (std::error_code &error) const noexcept
{
  unsigned long value{};
  if (call(error, ::ioctl, handle, FIONREAD, &value) == -1)
  {
    value = 0;
  }
  return value;
}


#endif // }}}1


//
// Asynchronous API
//

#if __sal_os_windows // {{{1


socket_t::async_t::async_t (socket_t &socket,
    async_worker_t::shared_ptr worker,
    std::error_code &error) noexcept
  : socket(socket)
  , worker(worker)
{
  auto result = ::CreateIoCompletionPort(
    reinterpret_cast<HANDLE>(socket.handle),
    worker->iocp,
    0,
    0
  );
  if (!result)
  {
    auto e = ::GetLastError();
    if (e == ERROR_INVALID_PARAMETER)
    {
      error = std::make_error_code(std::errc::invalid_argument);
    }
    else
    {
      error.assign(e, std::system_category());
    }
  }
}


async_worker_t::async_worker_t (std::error_code &error) noexcept
  : iocp(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0))
{
  if (iocp)
  {
    error = init_lib();
  }
  else
  {
    error.assign(::GetLastError(), std::system_category());
  }
}


async_worker_t::~async_worker_t () noexcept
{
  if (iocp)
  {
    ::CloseHandle(iocp);
  }
}


#elif __sal_os_darwin || sal_os_linux // {{{1


socket_t::async_t::async_t (socket_t &socket,
    async_worker_t::shared_ptr worker,
    std::error_code &error) noexcept
  : socket(socket)
  , worker(worker)
{
  if (socket.handle == invalid)
  {
    error = std::make_error_code(std::errc::invalid_argument);
  }
}


async_worker_t::async_worker_t (std::error_code &error) noexcept
{
  (void)error;
}


async_worker_t::~async_worker_t () noexcept
{
}


#endif // }}}1


}} // namespace net::__bits


__sal_end
