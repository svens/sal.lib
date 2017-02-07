#pragma once

#include <sal/config.hpp>
#include <system_error>

#if __sal_os_linux || __sal_os_darwin
  #include <sys/socket.h>
#elif __sal_os_windows
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32")
#else
  #error Unsupported platform
#endif


__sal_begin


namespace net { namespace __bits {


#if __sal_os_windows

// socket handle
using native_socket_t = SOCKET;
constexpr native_socket_t invalid_socket = INVALID_SOCKET;

// shutdown() direction
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH

// sockaddr family
using sa_family_t = ::ADDRESS_FAMILY;

// send/recv flags
using message_flags_t = DWORD;

// IOCP
using native_poller_t = HANDLE;
constexpr native_poller_t invalid_poller = INVALID_HANDLE_VALUE;

using io_buf_t = OVERLAPPED;
inline void reset (io_buf_t &aux) noexcept
{
  std::memset(&aux, '\0', sizeof(aux));
}

#else

// socket handle
using native_socket_t = int;
constexpr native_socket_t invalid_socket = -1;

// sockaddr family
using sa_family_t = ::sa_family_t;

// send/recv flags
using message_flags_t = int;

#endif


enum class wait_t { read, write };


#if __sal_os_windows

struct async_t
{
  std::error_code error_{};
  size_t transferred_{};
};


struct async_receive_t
  : public async_t
{};


struct async_receive_from_t
  : public async_t
{
  sockaddr_storage endpoint_{};
  int32_t endpoint_size_ = sizeof(endpoint_);
};


struct async_send_to_t
  : public async_t
{};


struct async_send_t
  : public async_t
{};


struct async_connect_t
  : public async_t
{
  native_socket_t native_handle_ = invalid_socket;
  void finish (std::error_code &error) noexcept;
};

#endif // __sal_os_windows


struct socket_t
{
  native_socket_t native_handle = invalid_socket;

  socket_t () = default;

  socket_t (native_socket_t native_handle) noexcept
    : native_handle(native_handle)
  {}

  void open (int domain,
    int type,
    int protocol,
    std::error_code &error
  ) noexcept;

  void close (std::error_code &error) noexcept;

  void bind (const void *address, size_t address_size, std::error_code &error)
    noexcept;

  void listen (int backlog, std::error_code &error) noexcept;

  native_socket_t accept (void *address, size_t *address_size,
    bool enable_connection_aborted,
    std::error_code &error
  ) noexcept;

  void connect (const void *address, size_t address_size,
    std::error_code &error
  ) noexcept;

  bool wait (wait_t what,
    int timeout_ms,
    std::error_code &error
  ) noexcept;

  void shutdown (int what, std::error_code &error) noexcept;

  void remote_endpoint (void *address, size_t *address_size,
    std::error_code &error
  ) const noexcept;

  void local_endpoint (void *address, size_t *address_size,
    std::error_code &error
  ) const noexcept;

  void get_opt (int level, int name,
    void *data, size_t *size,
    std::error_code &error
  ) const noexcept;

  void set_opt (int level, int name,
    const void *data, size_t size,
    std::error_code &error
  ) noexcept;

  bool non_blocking (std::error_code &error) const noexcept;
  void non_blocking (bool mode, std::error_code &error) noexcept;

  size_t available (std::error_code &error) const noexcept;

  //
  // synchronous send/recv
  //

  size_t receive (void *data, size_t data_size,
    message_flags_t flags,
    std::error_code &error
  ) noexcept;

  size_t receive_from (void *data, size_t data_size,
    void *address, size_t *address_size,
    message_flags_t flags,
    std::error_code &error
  ) noexcept;

  size_t send (const void *data, size_t data_size,
    message_flags_t flags,
    std::error_code &error
  ) noexcept;

  size_t send_to (const void *data, size_t data_size,
    const void *address, size_t address_size,
    message_flags_t flags,
    std::error_code &error
  ) noexcept;

#if __sal_os_windows

  //
  // asynchronous send/recv
  //

  bool start (io_buf_t *io_buf,
    void *data, size_t data_size,
    message_flags_t flags,
    async_receive_t &op
  ) noexcept;

  bool start (io_buf_t *io_buf,
    void *data, size_t data_size,
    message_flags_t flags,
    async_receive_from_t &op
  ) noexcept;

  bool start (io_buf_t *io_buf,
    const void *data, size_t data_size,
    const void *address, size_t address_size,
    message_flags_t flags,
    async_send_to_t &op
  ) noexcept;

  bool start (io_buf_t *io_buf,
    const void *data, size_t data_size,
    message_flags_t flags,
    async_send_t &op
  ) noexcept;

  bool start (io_buf_t *io_buf,
    const void *address, size_t address_size,
    async_connect_t &op
  ) noexcept;

#endif // __sal_os_windows
};


}} // namespace net::__bits


__sal_end
