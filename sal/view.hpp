#pragma once

/**
 * \file sal/view.hpp
 * To/from text conversions
 */


#include <sal/builtins.hpp>
#include <sal/config.hpp>
#include <cstring>
#include <sstream>

__sal_warn_disable
#include <memory>
__sal_warn_enable


namespace sal {
__sal_hpp_begin


namespace __bits {


// helper: [first,last) -> d_first unless doesn't fit
template <typename InputIt, typename ForwardIt>
inline ForwardIt copy_str (InputIt first, InputIt last,
  ForwardIt d_first, ForwardIt d_last) noexcept
{
  auto end = d_first + (last - first);
  if (end <= d_last)
  {
    return std::uninitialized_copy(first, last, d_first);
  }

  return end;
}


// bool
inline char *copy_v (bool value, char *first, char *last) noexcept
{
  if (value)
  {
    static constexpr char s[] = "true";
    return copy_str(s, s + sizeof(s) - 1, first, last);
  }

  static constexpr char s[] = "false";
  return copy_str(s, s + sizeof(s) - 1, first, last);
}


// char
inline char *copy_v (char value, char *first, char *last) noexcept
{
  return copy_str(&value, &value + 1, first, last);
}


// signed char
inline char *copy_v (signed char value, char *first, char *last) noexcept
{
  return copy_v(static_cast<char>(value), first, last);
}


// unsigned char
inline char *copy_v (unsigned char value, char *first, char *last) noexcept
{
  return copy_v(static_cast<char>(value), first, last);
}


// helper:
inline unsigned digit_count (uint64_t value) noexcept
{
  // http://graphics.stanford.edu/~seander/bithacks.html#IntegerLog10

  static constexpr uint64_t pow10[] =
  {
    0ULL,
    10ULL,
    100ULL,
    1000ULL,
    10000ULL,
    100000ULL,
    1000000ULL,
    10000000ULL,
    100000000ULL,
    1000000000ULL,
    10000000000ULL,
    100000000000ULL,
    1000000000000ULL,
    10000000000000ULL,
    100000000000000ULL,
    1000000000000000ULL,
    10000000000000000ULL,
    100000000000000000ULL,
    1000000000000000000ULL,
    10000000000000000000ULL,
  };

  unsigned t = (64 - sal_clz(value | 1)) * 1233 >> 12;
  return t - (value < pow10[t]) + 1;
}


// uint64_t
inline char *copy_v (uint64_t value, char *first, char *last) noexcept
{
  // https://www.facebook.com/notes/facebook-engineering/three-optimization-tips-for-c/10151361643253920

  auto *end = first + digit_count(value);
  if (end <= last)
  {
    static constexpr char digits[] =
      "0001020304050607080910111213141516171819"
      "2021222324252627282930313233343536373839"
      "4041424344454647484950515253545556575859"
      "6061626364656667686970717273747576777879"
      "8081828384858687888990919293949596979899"
    ;

    auto p = end - 1;
    while (value > 99)
    {
      const auto i = (value % 100) * 2;
      value /= 100;
      *p-- = digits[i + 1];
      *p-- = digits[i];
    }

    if (value > 9)
    {
      const auto i = value * 2;
      *p-- = digits[i + 1];
      *p = digits[i];
    }
    else
    {
      *p = static_cast<char>('0' + value);
    }
  }

  return end;
}


// int64_t
inline char *copy_v (int64_t value, char *first, char *last) noexcept
{
  if (value > -1)
  {
    return copy_v(static_cast<uint64_t>(value), first, last);
  }

  auto end = copy_v(0 - static_cast<uint64_t>(value), first + 1, last);
  if (end <= last)
  {
    *first = '-';
  }

  return end;
}


// uint32_t
inline char *copy_v (uint32_t value, char *first, char *last) noexcept
{
  return copy_v(static_cast<uint64_t>(value), first, last);
}


// int32_t
inline char *copy_v (int32_t value, char *first, char *last) noexcept
{
  return copy_v(static_cast<int64_t>(value), first, last);
}


// uint16_t
inline char *copy_v (uint16_t value, char *first, char *last) noexcept
{
  return copy_v(static_cast<uint64_t>(value), first, last);
}


// int16_t
inline char *copy_v (int16_t value, char *first, char *last) noexcept
{
  return copy_v(static_cast<int64_t>(value), first, last);
}


// nullptr
inline char *copy_v (std::nullptr_t /**/, char *first, char *last) noexcept
{
  static constexpr char s[] = "(null)";
  return copy_str(s, s + sizeof(s) - 1, first, last);
}


// const T *
template <typename T>
inline char *copy_v (const T *value, char *first, char *last) noexcept
{
  auto v = reinterpret_cast<uintptr_t>(value);
  char *end = first + 2;
  do
  {
    ++end;
  } while (v >>= 4);

  if (end <= last)
  {
    v = reinterpret_cast<uintptr_t>(value);
    auto p = end - 1;
    do
    {
      static constexpr char digits[] = "0123456789abcdef";
      *p-- = digits[v & 0xf];
    } while (v >>= 4);
    *p-- = 'x';
    *p = '0';
  }

  return end;
}


// const char *
inline char *copy_v (const char *value, char *first, char *last) noexcept
{
  return value
    ? copy_str(value, value + std::strlen(value), first, last)
    : copy_v(nullptr, first, last)
  ;
}


// T *
template <typename T>
inline char *copy_v (T *value, char *first, char *last) noexcept
{
  return copy_v(static_cast<const T *>(value), first, last);
}


// std::string
inline char *copy_v (const std::string &value, char *first, char *last)
  noexcept
{
  return copy_str(value.begin(), value.end(), first, last);
}


// float/double/long double
char *copy_v (float value, char *first, char *last) noexcept;
char *copy_v (double value, char *first, char *last) noexcept;
char *copy_v (const long double &value, char *first, char *last) noexcept;


// catch-all
template <typename T>
inline char *copy_v (const T &value, char *first, char *last)
{
  std::ostringstream oss;
  oss << value;
  return copy_v(oss.str(), first, last);
}


} // namespace __bits


/**
 * Copy \a value human-readable representation to [\a first, \a last).
 * Result is not NUL-terminated.
 *
 * - If result fits to specified range, whole text is copied and pointer to
 *   one position past last character written is returned. If caller needs
 *   NUL-terminated string, this is position where NUL should be written.
 * - If \a value representation would not fit into given range, no partial
 *   text will be copied. Function returns same pointer as on success.
 *
 * To check if copy_v() copied text successfully, check:
 * \code
 * auto end = sal::copy_v(value, first, last);
 * if (end <= last)
 * {
 *   // success
 *   std::cout << std::string(first, end) << std::endl;
 * }
 * else
 * {
 *   // overflow, need (end - last) bytes
 * }
 * \endcode
 *
 * Library provides specialization for builtin types (integral types, floats,
 * NUL-terminated string, pointers) plus std::string. For other types, it uses
 * operator<<(std::ostream) to get \a value textual representation. If
 * application-specified type has more optimized means to generate textual
 * representation, it can specialize copy_v() for given type. This way
 * application types plug into SAL logging etc.
 */
template <typename T>
inline char *copy_v (const T &value, char *first, char *last)
  noexcept(noexcept(__bits::copy_v(value, first, last)))
{
  return __bits::copy_v(value, first, last);
}


__sal_hpp_end
} // namespace sal