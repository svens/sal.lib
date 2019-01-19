# sources
list(APPEND sal_sources
  sal/uri/__bits/char_class.hpp
  sal/uri/__bits/encoding.hpp

  sal/uri/encoding.hpp
  sal/uri/error.hpp
  sal/uri/error.cpp
  sal/uri/scheme.hpp
  sal/uri/scheme.cpp
  sal/uri/uri.hpp
  sal/uri/uri.cpp
  sal/uri/view.hpp
  sal/uri/view.cpp
)


# unittests
list(APPEND sal_unittests_sources
  sal/uri/encoding.test.cpp
  sal/uri/uri.test.cpp
  sal/uri/scheme.test.cpp
  sal/uri/view.test.cpp
)
