#include <sal/net/async/service.hpp>
#include <sal/net/ip/udp.hpp>
#include <sal/net/common.test.hpp>
#include <thread>


namespace {


using namespace std::chrono_literals;
using socket_t = sal::net::ip::udp_t::socket_t;


template <typename Address>
struct net_async_datagram_socket
  : public sal_test::with_type<Address>
{
  const sal::net::ip::udp_t protocol =
    std::is_same_v<Address, sal::net::ip::address_v4_t>
      ? sal::net::ip::udp_t::v4
      : sal::net::ip::udp_t::v6
  ;

  const sal::net::ip::udp_t::endpoint_t endpoint{
    Address::loopback,
    8195
  };

  sal::net::async::service_t service{};
  socket_t socket{protocol}, test_socket{protocol};


  void SetUp ()
  {
    socket.bind(endpoint);
    socket.associate(service);
    test_socket.connect(endpoint);
  }


  sal::net::async::io_ptr wait ()
  {
    auto io = service.try_get();
    if (!io && service.wait())
    {
      io = service.try_get();
    }
    return io;
  }


  sal::net::async::io_ptr poll ()
  {
    auto io = service.try_get();
    if (!io && service.poll())
    {
      io = service.try_get();
    }
    return io;
  }


  void send (const std::string &data)
  {
    EXPECT_EQ(endpoint, test_socket.remote_endpoint());
    test_socket.send(data);

    // just for case, depends on IP stack implementation
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1ms);
  }


  std::string receive ()
  {
    char buf[1024];
    return {buf, test_socket.receive(buf)};
  }


  void fill (sal::net::async::io_ptr &io, const std::string_view &data)
    noexcept
  {
    io->resize(data.size());
    std::memcpy(io->data(), data.data(), data.size());
  }
};

TYPED_TEST_CASE(net_async_datagram_socket,
  sal_test::address_types,
  sal_test::address_names
);

template <typename Result>
inline std::string_view to_view (sal::net::async::io_ptr &io,
  const Result *result) noexcept
{
  return {reinterpret_cast<const char *>(io->data()), result->transferred};
}


TYPED_TEST(net_async_datagram_socket, start_receive_from) //{{{1
{
  TestFixture::socket.start_receive_from(TestFixture::service.make_io());
  TestFixture::send(TestFixture::case_name);

  auto io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  ASSERT_EQ(nullptr, io->template get_if<socket_t::receive_t>());

  auto result = io->template get_if<socket_t::receive_from_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(TestFixture::case_name, to_view(io, result));
  EXPECT_EQ(TestFixture::test_socket.local_endpoint(), result->remote_endpoint);
}


TYPED_TEST(net_async_datagram_socket, start_receive_from_without_associate) //{{{1
{
  if (sal::is_debug_build)
  {
    socket_t s;
    EXPECT_THROW(
      s.start_receive_from(TestFixture::service.make_io()),
      std::logic_error
    );
  }
}


TYPED_TEST(net_async_datagram_socket, start_receive_from_after_send) //{{{1
{
  TestFixture::send(TestFixture::case_name);
  TestFixture::socket.start_receive_from(TestFixture::service.make_io());

  auto io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  auto result = io->template get_if<socket_t::receive_from_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(TestFixture::case_name, to_view(io, result));
  EXPECT_EQ(TestFixture::test_socket.local_endpoint(), result->remote_endpoint);
}


TYPED_TEST(net_async_datagram_socket, start_receive_from_with_context) //{{{1
{
  int socket_ctx = 1, io_ctx = 2;
  TestFixture::socket.context(&socket_ctx);

  TestFixture::socket.start_receive_from(TestFixture::service.make_io(&io_ctx));
  TestFixture::send(TestFixture::case_name);

  auto io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  EXPECT_EQ(&io_ctx, io->template context<int>());
  EXPECT_EQ(nullptr, io->template context<socket_t>());
  EXPECT_EQ(&socket_ctx, io->template socket_context<int>());
  EXPECT_EQ(nullptr, io->template socket_context<socket_t>());

  auto result = io->template get_if<socket_t::receive_from_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(TestFixture::case_name, to_view(io, result));
  EXPECT_EQ(TestFixture::test_socket.local_endpoint(), result->remote_endpoint);
}


TYPED_TEST(net_async_datagram_socket, start_receive_from_canceled_on_close) //{{{1
{
  TestFixture::socket.start_receive_from(TestFixture::service.make_io());
  TestFixture::socket.close();

  auto io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  std::error_code error;
  auto result = io->template get_if<socket_t::receive_from_t>(error);
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(std::errc::operation_canceled, error);
}


TYPED_TEST(net_async_datagram_socket, start_receive_from_no_sender) //{{{1
{
  TestFixture::socket.start_receive_from(TestFixture::service.make_io());
  EXPECT_FALSE(TestFixture::service.poll());
  EXPECT_FALSE(TestFixture::service.try_get());
  TestFixture::socket.close();

  auto io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  std::error_code error;
  auto result = io->template get_if<socket_t::receive_from_t>(error);
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(std::errc::operation_canceled, error);
}


TYPED_TEST(net_async_datagram_socket, start_receive_from_peek) //{{{1
{
  TestFixture::socket.start_receive_from(TestFixture::service.make_io(), socket_t::peek);
  TestFixture::send(TestFixture::case_name);

  // regardless of peek, completion should be removed from queue
  EXPECT_TRUE(TestFixture::wait());
  EXPECT_FALSE(TestFixture::poll());
}


TYPED_TEST(net_async_datagram_socket, start_receive_from_peek_after_send) //{{{1
{
  TestFixture::send(TestFixture::case_name);
  TestFixture::socket.start_receive_from(TestFixture::service.make_io(), socket_t::peek);

  // regardless of peek, completion should be removed from queue
  EXPECT_TRUE(TestFixture::wait());
  EXPECT_FALSE(TestFixture::poll());
}


TYPED_TEST(net_async_datagram_socket, start_receive_from_less_than_send) //{{{1
{
  std::string_view data{
    TestFixture::case_name.data(),
    TestFixture::case_name.size() / 2,
  };

  auto io = TestFixture::service.make_io();
  io->resize(data.size());
  TestFixture::socket.start_receive_from(std::move(io));
  TestFixture::send(TestFixture::case_name);

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  std::error_code error;
  auto result = io->template get_if<socket_t::receive_from_t>(error);
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(std::errc::message_size, error);
  EXPECT_EQ(data.size(), result->transferred);
  EXPECT_EQ(data, to_view(io, result));

  // even with partial read, 2nd should have nothing
  io->reset();
  TestFixture::socket.start_receive_from(std::move(io));
  EXPECT_FALSE(TestFixture::poll());
}


TYPED_TEST(net_async_datagram_socket, start_receive_from_after_send_less_than_send) //{{{1
{
  TestFixture::send(TestFixture::case_name);

  std::string_view data{
    TestFixture::case_name.data(),
    TestFixture::case_name.size() / 2,
  };

  auto io = TestFixture::service.make_io();
  io->resize(data.size());
  TestFixture::socket.start_receive_from(std::move(io));

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  std::error_code error;
  auto result = io->template get_if<socket_t::receive_from_t>(error);
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(std::errc::message_size, error);
  EXPECT_EQ(data.size(), result->transferred);
  EXPECT_EQ(data, to_view(io, result));

  // even with partial read, 2nd should have nothing
  io->reset();
  TestFixture::socket.start_receive_from(std::move(io));
  EXPECT_FALSE(TestFixture::poll());
}


TYPED_TEST(net_async_datagram_socket, start_receive_from_empty_buf) //{{{1
{
  // can't unify IOCP/epoll/kqueue behavior without additional syscall
  // but this is weird case anyway, prefer performance over unification

  auto io = TestFixture::service.make_io();
  io->resize(0);
  TestFixture::socket.start_receive_from(std::move(io));
  TestFixture::send(TestFixture::case_name);

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  std::error_code error;
  auto result = io->template get_if<socket_t::receive_from_t>(error);
  ASSERT_NE(nullptr, result);

#if __sal_os_macos

  // 1st attempt succeeds immediately, with 0B transferred but leaving data in
  // OS socket buffer
  EXPECT_FALSE(error);
  EXPECT_EQ(0U, result->transferred);

  io->reset();
  TestFixture::socket.start_receive_from(std::move(io));

  // 2nd attempt with appropriately sized buffer succeeds with originally sent
  // data
  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);
  result = io->template get_if<socket_t::receive_from_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(TestFixture::case_name, to_view(io, result));

#else

  // 1st attempt fails, OS drops data from socket buffer
  EXPECT_EQ(std::errc::message_size, error);
  EXPECT_EQ(0U, result->transferred);

  // 2st attempt fails with no data
  io->reset();
  TestFixture::socket.start_receive_from(std::move(io));
  io = TestFixture::poll();
  EXPECT_EQ(nullptr, io);

#endif
}


TYPED_TEST(net_async_datagram_socket, start_receive_from_after_send_empty_buf) //{{{1
{
  TestFixture::send(TestFixture::case_name);

  auto io = TestFixture::service.make_io();
  io->resize(0);
  TestFixture::socket.start_receive_from(std::move(io));

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  std::error_code error;
  auto result = io->template get_if<socket_t::receive_from_t>(error);
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(std::errc::message_size, error);
  EXPECT_EQ(0U, result->transferred);
}


TYPED_TEST(net_async_datagram_socket, start_receive_from_interleaved) //{{{1
{
  TestFixture::socket.start_receive_from(TestFixture::service.make_io());
  TestFixture::socket.start_receive_from(TestFixture::service.make_io());
  TestFixture::socket.start_receive_from(TestFixture::service.make_io());

  TestFixture::send("one");
  TestFixture::send("two");
  TestFixture::send("three");

  auto io = TestFixture::wait();
  ASSERT_NE(nullptr, io);
  auto result = io->template get_if<socket_t::receive_from_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ("one", to_view(io, result));

  std::thread([&]
  {
    auto i = TestFixture::wait();
    ASSERT_TRUE(i);
    auto r = i->template get_if<socket_t::receive_from_t>();
    ASSERT_NE(nullptr, r);
    EXPECT_EQ("two", to_view(i, r));
  }).join();

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);
  result = io->template get_if<socket_t::receive_from_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ("three", to_view(io, result));
}


TYPED_TEST(net_async_datagram_socket, start_receive_from_interleaved_after_send) //{{{1
{
  TestFixture::send("one");
  TestFixture::send("two");
  TestFixture::send("three");

  TestFixture::socket.start_receive_from(TestFixture::service.make_io());
  TestFixture::socket.start_receive_from(TestFixture::service.make_io());
  TestFixture::socket.start_receive_from(TestFixture::service.make_io());

  auto io = TestFixture::wait();
  ASSERT_NE(nullptr, io);
  auto result = io->template get_if<socket_t::receive_from_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ("one", to_view(io, result));

  std::thread([&]
  {
    auto i = TestFixture::wait();
    ASSERT_TRUE(i);
    auto r = i->template get_if<socket_t::receive_from_t>();
    ASSERT_NE(nullptr, r);
    EXPECT_EQ("two", to_view(i, r));
  }).join();

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);
  result = io->template get_if<socket_t::receive_from_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ("three", to_view(io, result));
}


//}}}1


TYPED_TEST(net_async_datagram_socket, start_receive) //{{{1
{
  TestFixture::socket.start_receive(TestFixture::service.make_io());
  TestFixture::send(TestFixture::case_name);

  auto io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  ASSERT_EQ(nullptr, io->template get_if<socket_t::receive_from_t>());

  auto result = io->template get_if<socket_t::receive_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(TestFixture::case_name, to_view(io, result));
}


TYPED_TEST(net_async_datagram_socket, start_receive_without_associate) //{{{1
{
  if (sal::is_debug_build)
  {
    socket_t s;
    EXPECT_THROW(
      s.start_receive(TestFixture::service.make_io()),
      std::logic_error
    );
  }
}


TYPED_TEST(net_async_datagram_socket, start_receive_after_send) //{{{1
{
  TestFixture::send(TestFixture::case_name);
  TestFixture::socket.start_receive(TestFixture::service.make_io());

  auto io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  auto result = io->template get_if<socket_t::receive_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(TestFixture::case_name, to_view(io, result));
}


TYPED_TEST(net_async_datagram_socket, start_receive_with_context) //{{{1
{
  int socket_ctx = 1, io_ctx = 2;
  TestFixture::socket.context(&socket_ctx);

  TestFixture::socket.start_receive(TestFixture::service.make_io(&io_ctx));
  TestFixture::send(TestFixture::case_name);

  auto io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  EXPECT_EQ(&io_ctx, io->template context<int>());
  EXPECT_EQ(nullptr, io->template context<socket_t>());
  EXPECT_EQ(&socket_ctx, io->template socket_context<int>());
  EXPECT_EQ(nullptr, io->template socket_context<socket_t>());

  auto result = io->template get_if<socket_t::receive_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(TestFixture::case_name, to_view(io, result));
}


TYPED_TEST(net_async_datagram_socket, start_receive_canceled_on_close) //{{{1
{
  TestFixture::socket.start_receive(TestFixture::service.make_io());
  TestFixture::socket.close();

  auto io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  std::error_code error;
  auto result = io->template get_if<socket_t::receive_t>(error);
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(std::errc::operation_canceled, error);
}


TYPED_TEST(net_async_datagram_socket, start_receive_no_sender) //{{{1
{
  TestFixture::socket.start_receive(TestFixture::service.make_io());
  EXPECT_FALSE(TestFixture::service.poll());
  EXPECT_FALSE(TestFixture::service.try_get());
  TestFixture::socket.close();

  auto io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  std::error_code error;
  auto result = io->template get_if<socket_t::receive_t>(error);
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(std::errc::operation_canceled, error);
}


TYPED_TEST(net_async_datagram_socket, start_receive_peek) //{{{1
{
  TestFixture::socket.start_receive(TestFixture::service.make_io(), socket_t::peek);
  TestFixture::send(TestFixture::case_name);

  // regardless of peek, completion should be removed from queue
  EXPECT_TRUE(TestFixture::wait());
  EXPECT_FALSE(TestFixture::poll());
}


TYPED_TEST(net_async_datagram_socket, start_receive_peek_after_send) //{{{1
{
  TestFixture::send(TestFixture::case_name);
  TestFixture::socket.start_receive(TestFixture::service.make_io(), socket_t::peek);

  // regardless of peek, completion should be removed from queue
  EXPECT_TRUE(TestFixture::wait());
  EXPECT_FALSE(TestFixture::poll());
}


TYPED_TEST(net_async_datagram_socket, start_receive_less_than_send) //{{{1
{
  std::string_view data{
    TestFixture::case_name.data(),
    TestFixture::case_name.size() / 2,
  };

  auto io = TestFixture::service.make_io();
  io->resize(data.size());
  TestFixture::socket.start_receive(std::move(io));
  TestFixture::send(TestFixture::case_name);

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  std::error_code error;
  auto result = io->template get_if<socket_t::receive_t>(error);
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(std::errc::message_size, error);
  EXPECT_EQ(data.size(), result->transferred);
  EXPECT_EQ(data, to_view(io, result));

  // even with partial read, 2nd should have nothing
  io->reset();
  TestFixture::socket.start_receive(std::move(io));
  EXPECT_FALSE(TestFixture::poll());
}


TYPED_TEST(net_async_datagram_socket, start_receive_after_send_less_than_send) //{{{1
{
  TestFixture::send(TestFixture::case_name);

  std::string_view data{
    TestFixture::case_name.data(),
    TestFixture::case_name.size() / 2,
  };

  auto io = TestFixture::service.make_io();
  io->resize(data.size());
  TestFixture::socket.start_receive(std::move(io));

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  std::error_code error;
  auto result = io->template get_if<socket_t::receive_t>(error);
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(std::errc::message_size, error);
  EXPECT_EQ(data.size(), result->transferred);
  EXPECT_EQ(data, to_view(io, result));

  // even with partial read, 2nd should have nothing
  io->reset();
  TestFixture::socket.start_receive(std::move(io));
  EXPECT_FALSE(TestFixture::poll());
}


TYPED_TEST(net_async_datagram_socket, start_receive_empty_buf) //{{{1
{
  // can't unify IOCP/epoll/kqueue behavior without additional syscall
  // but this is weird case anyway, prefer performance over unification

  auto io = TestFixture::service.make_io();
  io->resize(0);
  TestFixture::socket.start_receive(std::move(io));
  TestFixture::send(TestFixture::case_name);

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  std::error_code error;
  auto result = io->template get_if<socket_t::receive_t>(error);
  ASSERT_NE(nullptr, result);

#if __sal_os_macos

  // 1st attempt succeeds immediately, with 0B transferred but leaving data in
  // OS socket buffer
  EXPECT_FALSE(error);
  EXPECT_EQ(0U, result->transferred);

  io->reset();
  TestFixture::socket.start_receive(std::move(io));

  // 2nd attempt with appropriately sized buffer succeeds with originally sent
  // data
  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);
  result = io->template get_if<socket_t::receive_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(TestFixture::case_name, to_view(io, result));

#else

  // 1st attempt fails, OS drops data from socket buffer
  EXPECT_EQ(std::errc::message_size, error);
  EXPECT_EQ(0U, result->transferred);

  // 2st attempt fails with no data
  io->reset();
  TestFixture::socket.start_receive(std::move(io));
  EXPECT_FALSE(TestFixture::poll());

#endif
}


TYPED_TEST(net_async_datagram_socket, start_receive_after_send_empty_buf) //{{{1
{
  TestFixture::send(TestFixture::case_name);

  auto io = TestFixture::service.make_io();
  io->resize(0);
  TestFixture::socket.start_receive(std::move(io));

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  std::error_code error;
  auto result = io->template get_if<socket_t::receive_t>(error);
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(std::errc::message_size, error);
  EXPECT_EQ(0U, result->transferred);
}


TYPED_TEST(net_async_datagram_socket, start_receive_interleaved) //{{{1
{
  TestFixture::socket.start_receive(TestFixture::service.make_io());
  TestFixture::socket.start_receive(TestFixture::service.make_io());
  TestFixture::socket.start_receive(TestFixture::service.make_io());

  TestFixture::send("one");
  TestFixture::send("two");
  TestFixture::send("three");

  auto io = TestFixture::wait();
  ASSERT_NE(nullptr, io);
  auto result = io->template get_if<socket_t::receive_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ("one", to_view(io, result));

  std::thread([&]
  {
    auto i = TestFixture::wait();
    ASSERT_TRUE(i);
    auto r = i->template get_if<socket_t::receive_t>();
    ASSERT_NE(nullptr, r);
    EXPECT_EQ("two", to_view(i, r));
  }).join();

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);
  result = io->template get_if<socket_t::receive_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ("three", to_view(io, result));
}


TYPED_TEST(net_async_datagram_socket, start_receive_interleaved_after_send) //{{{1
{
  TestFixture::send("one");
  TestFixture::send("two");
  TestFixture::send("three");

  TestFixture::socket.start_receive(TestFixture::service.make_io());
  TestFixture::socket.start_receive(TestFixture::service.make_io());
  TestFixture::socket.start_receive(TestFixture::service.make_io());

  auto io = TestFixture::wait();
  ASSERT_NE(nullptr, io);
  auto result = io->template get_if<socket_t::receive_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ("one", to_view(io, result));

  std::thread([&]
  {
    auto i = TestFixture::wait();
    ASSERT_TRUE(i);
    auto r = i->template get_if<socket_t::receive_t>();
    ASSERT_NE(nullptr, r);
    EXPECT_EQ("two", to_view(i, r));
  }).join();

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);
  result = io->template get_if<socket_t::receive_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ("three", to_view(io, result));
}


//}}}1


TYPED_TEST(net_async_datagram_socket, start_send_to) //{{{1
{
  auto io = TestFixture::service.make_io();
  TestFixture::fill(io, TestFixture::case_name);
  TestFixture::socket.start_send_to(std::move(io),
    TestFixture::test_socket.local_endpoint()
  );
  EXPECT_EQ(TestFixture::case_name, TestFixture::receive());

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  auto result = io->template get_if<socket_t::send_to_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(TestFixture::case_name.size(), result->transferred);
}


TYPED_TEST(net_async_datagram_socket, start_send_to_without_associate) //{{{1
{
  if (sal::is_debug_build)
  {
    socket_t s;
    EXPECT_THROW(
      s.start_send_to(
        TestFixture::service.make_io(),
        TestFixture::endpoint
      ),
      std::logic_error
    );
  }
}


TYPED_TEST(net_async_datagram_socket, start_send_to_with_context) //{{{1
{
  int socket_ctx = 1, io_ctx = 2;
  TestFixture::socket.context(&socket_ctx);

  auto io = TestFixture::service.make_io(&io_ctx);
  TestFixture::fill(io, TestFixture::case_name);
  TestFixture::socket.start_send_to(std::move(io),
    TestFixture::test_socket.local_endpoint()
  );
  EXPECT_EQ(TestFixture::case_name, TestFixture::receive());

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  EXPECT_EQ(&io_ctx, io->template context<int>());
  EXPECT_EQ(nullptr, io->template context<socket_t>());
  EXPECT_EQ(&socket_ctx, io->template socket_context<int>());
  EXPECT_EQ(nullptr, io->template socket_context<socket_t>());

  auto result = io->template get_if<socket_t::send_to_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(TestFixture::case_name.size(), result->transferred);
}


TYPED_TEST(net_async_datagram_socket, start_send_to_empty_buf) //{{{1
{
  auto io = TestFixture::service.make_io();
  io->resize(0);
  TestFixture::socket.start_send_to(std::move(io),
    TestFixture::test_socket.local_endpoint()
  );

  char buf[1024];
  EXPECT_EQ(0U, TestFixture::test_socket.receive(buf));

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  auto result = io->template get_if<socket_t::send_to_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(0U, result->transferred);
}


//}}}1


TYPED_TEST(net_async_datagram_socket, start_send) //{{{1
{
  TestFixture::socket.connect(TestFixture::test_socket.local_endpoint());

  auto io = TestFixture::service.make_io();
  TestFixture::fill(io, TestFixture::case_name);
  TestFixture::socket.start_send(std::move(io));
  EXPECT_EQ(TestFixture::case_name, TestFixture::receive());

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  auto result = io->template get_if<socket_t::send_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(TestFixture::case_name.size(), result->transferred);
}


TYPED_TEST(net_async_datagram_socket, start_send_without_associate) //{{{1
{
  if (sal::is_debug_build)
  {
    socket_t s;
    EXPECT_THROW(
      s.start_send(TestFixture::service.make_io()),
      std::logic_error
    );
  }
}


TYPED_TEST(net_async_datagram_socket, start_send_with_context) //{{{1
{
  TestFixture::socket.connect(TestFixture::test_socket.local_endpoint());

  int socket_ctx = 1, io_ctx = 2;
  TestFixture::socket.context(&socket_ctx);

  auto io = TestFixture::service.make_io(&io_ctx);
  TestFixture::fill(io, TestFixture::case_name);
  TestFixture::socket.start_send(std::move(io));
  EXPECT_EQ(TestFixture::case_name, TestFixture::receive());

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  EXPECT_EQ(&io_ctx, io->template context<int>());
  EXPECT_EQ(nullptr, io->template context<socket_t>());
  EXPECT_EQ(&socket_ctx, io->template socket_context<int>());
  EXPECT_EQ(nullptr, io->template socket_context<socket_t>());

  auto result = io->template get_if<socket_t::send_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(TestFixture::case_name.size(), result->transferred);
}


TYPED_TEST(net_async_datagram_socket, start_send_empty_buf) //{{{1
{
  TestFixture::socket.connect(TestFixture::test_socket.local_endpoint());

  auto io = TestFixture::service.make_io();
  io->resize(0);
  TestFixture::socket.start_send(std::move(io));

  char buf[1024];
  EXPECT_EQ(0U, TestFixture::test_socket.receive(buf));

  io = TestFixture::wait();
  ASSERT_NE(nullptr, io);

  auto result = io->template get_if<socket_t::send_t>();
  ASSERT_NE(nullptr, result);
  EXPECT_EQ(0U, result->transferred);
}


//}}}1


} // namespace
