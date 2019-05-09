#ifndef C10_UTIL_EXCEPTION_H_
#define C10_UTIL_EXCEPTION_H_

#include "c10/macros/Macros.h"
#include "c10/util/StringUtil.h"
#include "c10/util/Deprecated.h"

#include <cstddef>
#include <exception>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_MSC_VER) && _MSC_VER <= 1900
#define __func__ __FUNCTION__
#endif

namespace c10 {

/// The primary ATen error class.
/// Provides a complete error message with source location information via
/// `what()`, and a more concise message via `what_without_backtrace()`. Should
/// primarily be used with the `AT_ERROR` macro.
///
/// NB: c10::Error is handled specially by the default torch to suppress the
/// backtrace, see torch/csrc/Exceptions.h
class C10_API Error : public std::exception {
  std::vector<std::string> msg_stack_;
  std::string backtrace_;

  // These two are derived fields from msg_stack_ and backtrace_, but we need
  // fields for the strings so that we can return a const char* (as the
  // signature of std::exception requires).
  std::string msg_;
  std::string msg_without_backtrace_;

  // This is a little debugging trick: you can stash a relevant pointer
  // in caller, and then when you catch the exception, you can compare
  // against pointers you have on hand to get more information about
  // where the exception came from.  In Caffe2, this is used to figure
  // out which operator raised an exception.
  const void* caller_;

 public:
  Error(
      const std::string& msg,
      const std::string& backtrace,
      const void* caller = nullptr);
  Error(SourceLocation source_location, const std::string& msg);
  Error(
      const char* file,
      const uint32_t line,
      const char* condition,
      const std::string& msg,
      const std::string& backtrace,
      const void* caller = nullptr);

  void AppendMessage(const std::string& msg);

  // Compute the full message from msg_ and msg_without_backtrace_
  // TODO: Maybe this should be private
  std::string msg() const;
  std::string msg_without_backtrace() const;

  const std::vector<std::string>& msg_stack() const {
    return msg_stack_;
  }

  /// Returns the complete error message, including the source location.
  const char* what() const noexcept override {
    return msg_.c_str();
  }

  const void* caller() const noexcept {
    return caller_;
  }

  /// Returns only the error message string, without source location.
  const char* what_without_backtrace() const noexcept {
    return msg_without_backtrace_.c_str();
  }
};

class C10_API Warning {
  using handler_t =
      void (*)(const SourceLocation& source_location, const char* msg);

 public:
  /// Issue a warning with a given message. Dispatched to the current
  /// warning handler.
  static void warn(SourceLocation source_location, std::string msg);
  /// Sets the global warning handler. This is not thread-safe, so it should
  /// generally be called once during initialization.
  static void set_warning_handler(handler_t handler);
  /// The default warning handler. Prints the message to stderr.
  static void print_warning(
      const SourceLocation& source_location,
      const char* msg);

 private:
  static handler_t warning_handler_;
};

// Used in ATen for out-of-bound indices that can reasonably only be detected
// lazily inside a kernel (See: advanced indexing).
class C10_API IndexError : public Error {
  using Error::Error;
};


// A utility function to return an exception std::string by prepending its
// exception type before its what() content
C10_API std::string GetExceptionString(const std::exception& e);

namespace detail {

/*
// Deprecation disabled until we fix sites in our codebase
C10_DEPRECATED_MESSAGE("AT_ASSERT is deprecated, if you mean to indicate an internal invariant failure, use " \
                       "AT_INTERNAL_ASSERT instead; if you mean to do user error checking, use " \
                       "AT_CHECK.  See https://github.com/pytorch/pytorch/issues/20287 for more details.")
*/
inline void deprecated_AT_ASSERT() {}

/*
// Deprecation disabled until we fix sites in our codebase
C10_DEPRECATED_MESSAGE("AT_ASSERTM is deprecated, if you mean to indicate an internal invariant failure, use " \
                       "AT_INTERNAL_ASSERT instead; if you mean to do user error checking, use " \
                       "AT_CHECK.  See https://github.com/pytorch/pytorch/issues/20287 for more details.")
*/
inline void deprecated_AT_ASSERTM() {}

// Return x if it is non-empty; otherwise return y.
inline std::string if_empty_then(std::string x, std::string y) {
  if (x.empty()) {
    return y;
  } else {
    return x;
  }
}

}


} // namespace c10

// TODO: CAFFE_ENFORCE_WITH_CALLER style macro?

// In the debug build With MSVC, __LINE__ might be of long type (a.k.a int32_t),
// which is different from the definition of `SourceLocation` that requires
// unsigned int (a.k.a uint32_t) and may cause a compile error with the message:
// error C2397: conversion from 'long' to 'uint32_t' requires a narrowing conversion
// Here the static cast is used to pass the build.

#define AT_ERROR(...) \
  throw ::c10::Error({__func__, __FILE__, static_cast<uint32_t>(__LINE__)}, ::c10::str(__VA_ARGS__))

#define AT_INDEX_ERROR(...) \
  throw ::c10::IndexError({__func__, __FILE__, static_cast<uint32_t>(__LINE__)}, ::c10::str(__VA_ARGS__))

#define AT_WARN(...) \
  ::c10::Warning::warn({__func__, __FILE__, static_cast<uint32_t>(__LINE__)}, ::c10::str(__VA_ARGS__))

// A utility macro to provide assert()-like functionality; that is, enforcement
// of internal invariants in code.  It supports an arbitrary number of extra
// arguments (evaluated only on failure), which will be printed in the assert
// failure message using operator<< (this is useful to print some variables
// which may be useful for debugging.)
//
// Usage:
//    AT_INTERNAL_ASSERT(should_be_true);
//    AT_INTERNAL_ASSERT(x == 0, "x = ", x);
//
// Assuming no bugs in PyTorch, the conditions tested by this macro should
// always be true; e.g., it should be possible to disable all of these
// conditions without changing observable user behavior.  If you would like to
// do error reporting for user input, please use AT_CHECK instead.
//
// NOTE: It is SAFE to use this macro in production code; on failure, this
// simply raises an exception, it does NOT unceremoniously quit the process
// (unlike assert()).
//
#define AT_INTERNAL_ASSERT(cond, ...)         \
  if (!(cond)) {                              \
    AT_ERROR(                                 \
        #cond " ASSERT FAILED at ",           \
        __FILE__,                             \
        ":",                                  \
        __LINE__,                             \
        ", please report a bug to PyTorch. ", \
        ::c10::str(__VA_ARGS__));             \
  }

// A utility macro to make it easier to test for error conditions from user
// input.  Like AT_INTERNAL_ASSERT, it supports an arbitrary number of extra
// arguments (evaluated only on failure), which will be printed in the error
// message using operator<< (e.g., you can pass any object which has
// operator<< defined.  Most objects in PyTorch have these definitions!)
//
// Usage:
//    AT_CHECK(should_be_true); // A default error message will be provided
//                              // in this case; but we recommend writing an
//                              // explicit error message, as it is more
//                              // user friendly.
//    AT_CHECK(x == 0, "Expected x to be 0, but got ", x);
//
// On failure, this macro will raise an exception.  If this exception propagates
// to Python, it will convert into a Python RuntimeError.
//
// NOTE: It is SAFE to use this macro in production code; on failure, this
// simply raises an exception, it does NOT unceremoniously quit the process
// (unlike CHECK() from glog.)
//
#define AT_CHECK(cond, ...)            \
  if (!(cond)) {                       \
    AT_ERROR( \
      ::c10::detail::if_empty_then( \
        ::c10::str(__VA_ARGS__), \
        "Expected " #cond " to be true, but got false. " \
        "(Could this error message be improved?  If so, please report an " \
        "enhancement request to PyTorch.)" \
      ) \
    ); \
  }

// Deprecated alias; this alias was deprecated because people kept mistakenly
// using it for user error checking.  Use AT_INTERNAL_ASSERT or AT_CHECK
// instead. See https://github.com/pytorch/pytorch/issues/20287 for more details.
#define AT_ASSERT(cond) \
  do { \
    ::c10::detail::deprecated_AT_ASSERT(); \
    AT_INTERNAL_ASSERT(cond); \
  } while (false); \

// Deprecated alias, like AT_ASSERTM.  The new AT_INTERNAL_ASSERT macro supports
// both 0-ary and variadic calls, 
#define AT_ASSERTM(cond, ...) \
  do { \
    ::c10::detail::deprecated_AT_ASSERTM(); \
    AT_INTERNAL_ASSERT(cond, __VA_ARGS__); \
  } while (false); \

#endif // C10_UTIL_EXCEPTION_H_
