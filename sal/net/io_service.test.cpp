#include <sal/net/io_service.hpp>
#include <sal/common.test.hpp>


namespace {


using net_io_service = sal_test::fixture;


TEST_F(net_io_service, run)
{
  sal::net::io_context_t ctx;
  ctx.test();
}


} // namespace