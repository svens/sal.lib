#include <sal/__bits/hex.hpp>


__sal_begin


namespace __bits {


namespace {

static constexpr const char digits[] = "0123456789abcdef";

static constexpr const char lookup[] =
  // _0  _1  _2  _3  _4  _5  _6  _7  _8  _9  _a  _b  _c  _d  _e  _f
  "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 0_
  "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 1_
  "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 2_
  "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\xff\xff\xff\xff\xff\xff" // 3_
  "\xff\x0a\x0b\x0c\x0d\x0e\x0f\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 4_
  "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 5_
  "\xff\x0a\x0b\x0c\x0d\x0e\x0f\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 6_
  "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 7_
  "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 8_
  "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 9_
  "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // a_
  "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // b_
  "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // c_
  "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // d_
  "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // e_
  "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // f_
  ;

} // namespace


uint8_t *hex_string::encode (
  const uint8_t *first,
  const uint8_t *last,
  uint8_t *out) noexcept
{
  while (first != last)
  {
    *out++ = digits[*first >> 4];
    *out++ = digits[*first & 0xf];
    ++first;
  }

  return out;
}


uint8_t *hex_string::decode (
  const uint8_t *first,
  const uint8_t *last,
  uint8_t *out,
  std::error_code &error) noexcept
{
  if ((last - first) % 2 != 0)
  {
    error = std::make_error_code(std::errc::message_size);
    return out;
  }

  uint8_t hi{}, lo{};
  for (/**/;  first != last;  first += 2)
  {
    hi = lookup[size_t(first[0])];
    lo = lookup[size_t(first[1])];
    if (hi != 0xff && lo != 0xff)
    {
      *out++ = (hi << 4) | lo;
    }
    else
    {
      break;
    }
  }

  if (hi != 0xff && lo != 0xff)
  {
    error.clear();
  }
  else
  {
    error = std::make_error_code(std::errc::illegal_byte_sequence);
  }

  return out;
}


} // namespace __bits


__sal_end