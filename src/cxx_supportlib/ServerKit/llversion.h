#ifndef INCLUDE_LLVERSION_H_
#define INCLUDE_LLVERSION_H_

#include <ServerKit/llhttp.h>
#include <boost/preprocessor/stringize.hpp>

inline const char *llhttp_version() {
  return BOOST_PP_STRINGIZE(LLHTTP_VERSION_MAJOR) "."
      BOOST_PP_STRINGIZE(LLHTTP_VERSION_MINOR) "."
      BOOST_PP_STRINGIZE(LLHTTP_VERSION_PATCH);
}
#endif
