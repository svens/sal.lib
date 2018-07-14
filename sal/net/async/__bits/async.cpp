#include <sal/net/async/__bits/async.hpp>
#include <sal/net/error.hpp>


__sal_begin


namespace net::async::__bits {


#if __sal_os_windows


using ::sal::net::__bits::winsock;


namespace {


template <typename T>
inline ULONG rio_buf_offset (const io_t &io, T *p) noexcept
{
  return static_cast<ULONG>(
    reinterpret_cast<char *>(p) - io.block.data.get()
  );
}


inline RIO_BUF make_rio_data (const io_t &io) noexcept
{
  RIO_BUF result;
  result.BufferId = io.block.buffer_id;
  result.Offset = rio_buf_offset(io, io.begin);
  result.Length = static_cast<ULONG>(io.end - io.begin);
  return result;
}


inline RIO_BUF make_rio_address (const io_t &io,
  void *address,
  size_t address_size) noexcept
{
  RIO_BUF result;
  result.BufferId = io.block.buffer_id;
  result.Offset = rio_buf_offset(io, address);
  result.Length = static_cast<ULONG>(address_size);
  return result;
}


} // namespace


io_block_t::io_block_t (size_t size, io_t::free_list_t &free_list)
  : free_list(free_list)
  , data(new char[size])
  , buffer_id(winsock.RIORegisterBuffer(data.get(), static_cast<DWORD>(size)))
{
  if (buffer_id == RIO_INVALID_BUFFERID)
  {
    std::error_code system_error;
    system_error.assign(::WSAGetLastError(), std::system_category());
    throw_system_error(system_error, "RIORegisterBuffer");
  }

  auto it = data.get(), end = it + size;
  while (it != end)
  {
    free_list.push(new(it) io_t(*this));
    it += sizeof(io_t);
  }
}


io_block_t::~io_block_t () noexcept
{
  winsock.RIODeregisterBuffer(buffer_id);
}


service_t::service_t ()
  : iocp(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0))
{
  if (iocp == INVALID_HANDLE_VALUE)
  {
    std::error_code system_error;
    system_error.assign(::GetLastError(), std::system_category());
    throw_system_error(system_error, "CreateIoCompletionPort");
  }
}


service_t::~service_t () noexcept
{
  if (iocp != INVALID_HANDLE_VALUE)
  {
    (void)::CloseHandle(iocp);
  }
}


bool worker_t::wait_for_more (const std::chrono::milliseconds &timeout,
  std::error_code &error) noexcept
{
  ULONG event_count;
  OVERLAPPED_ENTRY event[1];
  auto succeeded = ::GetQueuedCompletionStatusEx(
    service->iocp,
    &event[0],
    1,
    &event_count,
    static_cast<DWORD>(timeout.count()),
    false
  );

  if (succeeded)
  {
    auto socket = reinterpret_cast<async_socket_t *>(event[0].lpCompletionKey);

    auto result_count = winsock.RIODequeueCompletion(
      socket->completion_queue,
      completed.data(),
      static_cast<DWORD>(max_results_per_poll)
    );
    winsock.RIONotify(socket->completion_queue);

    if (result_count != RIO_CORRUPT_CQ)
    {
      first_completed = completed.begin();
      last_completed = first_completed + result_count;
      return true;
    }

    error = std::make_error_code(std::errc::bad_address);
  }
  else
  {
    auto e = ::GetLastError();
    if (e == WAIT_TIMEOUT)
    {
      error.clear();
    }
    else
    {
      error.assign(e, std::system_category());
    }
  }

  first_completed = last_completed = completed.begin();
  return false;
}


io_t *worker_t::result_at (typename completed_list::const_iterator it)
  noexcept
{
  auto &result = *it;
  auto &io = *reinterpret_cast<io_t *>(result.RequestContext);
  io.socket = reinterpret_cast<async_socket_t *>(result.SocketContext);
  io.status.assign(result.Status, std::system_category());
  *io.transferred = result.BytesTransferred;
  (*io.outstanding)--;
  return &io;
}


async_socket_t::handle_t async_socket_t::open (
  int family,
  int type,
  int protocol)
{
  auto handle = ::WSASocketW(
    family,
    type,
    protocol,
    nullptr,
    0,
    WSA_FLAG_REGISTERED_IO
  );
  if (handle == INVALID_SOCKET)
  {
    std::error_code system_error;
    system_error.assign(::WSAGetLastError(), std::system_category());
    throw_system_error(system_error, "WSASocket");
  }

  ::SetFileCompletionNotificationModes(
    reinterpret_cast<HANDLE>(handle),
    FILE_SKIP_SET_EVENT_ON_HANDLE
  );

  if (type == SOCK_DGRAM)
  {
    bool new_behaviour = false;
    DWORD ignored;
    ::WSAIoctl(handle,
      SIO_UDP_CONNRESET,
      &new_behaviour,
      sizeof(new_behaviour),
      nullptr,
      0,
      &ignored,
      nullptr,
      nullptr
    );
  }

  return handle;
}


async_socket_t::async_socket_t (handle_t handle,
    service_ptr service,
    size_t max_outstanding_receives,
    size_t max_outstanding_sends,
    std::error_code &error) noexcept
  : service(service)
{
  auto result = ::CreateIoCompletionPort(
    reinterpret_cast<HANDLE>(handle),
    service->iocp,
    0,
    0
  );
  if (!result)
  {
    error.assign(::GetLastError(), std::system_category());
    return;
  }

  RIO_NOTIFICATION_COMPLETION notification;
  notification.Type = RIO_IOCP_COMPLETION;
  notification.Iocp.IocpHandle = service->iocp;
  notification.Iocp.Overlapped = &service->overlapped;
  notification.Iocp.CompletionKey = this;

  completion_queue = winsock.RIOCreateCompletionQueue(
    static_cast<DWORD>(max_outstanding_receives + max_outstanding_sends),
    &notification
  );
  if (completion_queue == RIO_INVALID_CQ)
  {
    error.assign(::WSAGetLastError(), std::system_category());
    return;
  }

  request_queue = winsock.RIOCreateRequestQueue(handle,
    static_cast<ULONG>(max_outstanding_receives),
    1,
    static_cast<ULONG>(max_outstanding_sends),
    1,
    completion_queue,
    completion_queue,
    this
  );
  if (request_queue == RIO_INVALID_RQ)
  {
    error.assign(::WSAGetLastError(), std::system_category());
    return;
  }

  winsock.RIONotify(completion_queue);
}


async_socket_t::~async_socket_t () noexcept
{
  if (completion_queue != RIO_INVALID_CQ)
  {
    winsock.RIOCloseCompletionQueue(completion_queue);
  }
}


void async_socket_t::start_receive_from (io_t &io,
  void *remote_endpoint,
  size_t remote_endpoint_size,
  size_t *transferred,
  message_flags_t flags) noexcept
{
  auto data = make_rio_data(io);
  auto remote_address = make_rio_address(io,
    remote_endpoint,
    remote_endpoint_size
  );
  io.transferred = transferred;

  bool success;
  {
    std::lock_guard lock(request_queue_mutex);
    success = winsock.RIOReceiveEx(
      request_queue,    // SocketQueue
      &data,            // pData
      1,                // DataBufferCount
      nullptr,          // pLocalAddress
      &remote_address,  // pRemoteAddress
      nullptr,          // pControlContext
      nullptr,          // pFlags
      flags,            // Flags
      &io               // RequestContext
    );
  }

  if (!success)
  {
    io.socket = this;
    io.status.assign(::WSAGetLastError(), std::system_category());
    service->enqueue_error(&io);
  }
  else
  {
    outstanding_recv++;
    io.outstanding = &outstanding_recv;
  }
}


void async_socket_t::start_send_to (io_t &io,
  void *remote_endpoint,
  size_t remote_endpoint_size,
  size_t *transferred,
  message_flags_t flags) noexcept
{
  auto data = make_rio_data(io);
  auto remote_address = make_rio_address(io,
    remote_endpoint,
    remote_endpoint_size
  );
  io.transferred = transferred;

  bool success;
  {
    std::lock_guard lock(request_queue_mutex);
    success = winsock.RIOSendEx(
      request_queue,    // SocketQueue
      &data,            // pData
      1,                // DataBufferCount
      nullptr,          // pLocalAddress
      &remote_address,  // pRemoteAddress
      nullptr,          // pControlContext
      nullptr,          // pFlags
      flags,            // Flags
      &io               // RequestContext
    );
  }

  if (!success)
  {
    io.socket = this;
    io.status.assign(::WSAGetLastError(), std::system_category());
    service->enqueue_error(&io);
  }
  else
  {
    outstanding_send++;
    io.outstanding = &outstanding_send;
  }
}


#elif __sal_os_linux || __sal_os_macos


io_block_t::io_block_t (size_t size, io_t::free_list_t &free_list)
  : free_list(free_list)
  , data(new char[size])
{
  auto it = data.get(), end = it + size;
  while (it != end)
  {
    free_list.push(new(it) io_t(*this));
    it += sizeof(io_t);
  }
}


io_block_t::~io_block_t () noexcept
{}


service_t::service_t ()
{}


service_t::~service_t () noexcept
{}


bool worker_t::wait_for_more (const std::chrono::milliseconds &timeout,
  std::error_code &error) noexcept
{
  (void)timeout;
  (void)error;
  first_completed = last_completed = completed.begin();
  return false;
}


io_t *worker_t::result_at (typename completed_list::const_iterator it)
  noexcept
{
  (void)it;
  return {};
}


async_socket_t::handle_t async_socket_t::open (
  int family,
  int type,
  int protocol)
{
  net::__bits::socket_t socket;
  socket.open(family, type, protocol, throw_on_error("socket::open"));
  return socket.release();
}


async_socket_t::async_socket_t (handle_t handle,
    service_ptr service,
    size_t max_outstanding_receives,
    size_t max_outstanding_sends,
    std::error_code &error) noexcept
  : service(service)
{
  (void)handle;
  (void)max_outstanding_receives;
  (void)max_outstanding_sends;
  (void)error;
}


async_socket_t::~async_socket_t () noexcept
{}


void async_socket_t::start_receive_from (io_t &io,
  void *remote_endpoint,
  size_t remote_endpoint_size,
  size_t *transferred,
  message_flags_t flags) noexcept
{
  (void)io;
  (void)remote_endpoint;
  (void)remote_endpoint_size;
  (void)transferred;
  (void)flags;
}


void async_socket_t::start_send_to (io_t &io,
  void *remote_endpoint,
  size_t remote_endpoint_size,
  size_t *transferred,
  message_flags_t flags) noexcept
{
  (void)io;
  (void)remote_endpoint;
  (void)remote_endpoint_size;
  (void)transferred;
  (void)flags;
}


#endif


} // namespace net::async::__bits


__sal_end
