#pragma once

/**
 * \file sal/crypto/hash.hpp
 * Cryptographic hash functions
 */


#include <sal/config.hpp>
#include <sal/assert.hpp>
#include <sal/crypto/__bits/hash_algorithm.hpp>


__sal_begin


namespace crypto {


using md2 = __bits::md2_t;
using md4 = __bits::md4_t;
using md5 = __bits::md5_t;
using sha_1 = __bits::sha_1_t;
using sha_256 = __bits::sha_256_t;
using sha_384 = __bits::sha_384_t;
using sha_512 = __bits::sha_512_t;


template <typename T>
class hash_t
{
public:

  static constexpr size_t digest_size () noexcept
  {
    return T::digest_size;
  }


  template <typename Ptr>
  void add (const Ptr &data) noexcept
  {
    impl_.add(data.data(), data.size());
  }


  template <typename Ptr>
  void finish (const Ptr &result)
  {
    sal_assert(result.size() >= digest_size());
    impl_.finish(result.data());
  }


private:

  typename T::hash_t impl_{};
};


} // namespace crypto


__sal_end
