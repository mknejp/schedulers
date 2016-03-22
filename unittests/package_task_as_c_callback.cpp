// Copyright (c) 2016, Miroslav Knejp
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include <schedulers/package_task_as_c_callback.hpp>
#include <schedulers/schedulers.hpp>
#include "catch.hpp"

namespace
{
  // Keeps track of the number of instances without throwing when called
  class tracked_callable
  {
  public:
    tracked_callable(int* counter) : _counter(counter) { ++*_counter; }
    tracked_callable(const tracked_callable& other) : _counter(other._counter) { ++*_counter; }
    ~tracked_callable() { --*_counter; }

    void operator()() const { }

  private:
    int* _counter;
  };

  struct exception_t { };

  // Keeps track of the number of instances and throws exception_t when called
  class tracked_throwing_callable
  {
  public:
    tracked_throwing_callable(int* counter) : _base(counter) { }

    void operator()() const { throw exception_t{}; }

  private:
    tracked_callable _base;
  };

  // Fails test if used
  template<class T = void>
  struct forbidden_allocator
  {
    forbidden_allocator() = default;

    template<class U>
    forbidden_allocator(forbidden_allocator<U> other) { }

    using value_type = T;

    T* allocate(size_t n)
    {
      FAIL("attempted to allocate using forbidden allocator");
      return nullptr;
    }
    void deallocate(T* ptr, size_t n)
    {
      FAIL("attempted to deallocate using forbidden allocator");
    }

    template<class U>
    struct rebind
    {
      using other = forbidden_allocator<U>;
    };
  };

  // Keeps track of the number of bytes allocated in a user-declared location
  template<class T = void>
  class tracking_allocator
  {
  public:
    tracking_allocator(size_t* counter) : _counter(counter) { }

    template<class U>
    tracking_allocator(tracking_allocator<U> other) : _counter(other._counter) { }

    using value_type = T;

    auto allocate(size_t n)
    {
      *_counter += sizeof(T) * n;
      return std::allocator<T>().allocate(n);
    }
    auto deallocate(T* ptr, size_t n)
    {
      *_counter -= sizeof(T) * n;
      return std::allocator<T>().deallocate(ptr, n);
    }

    template<class U>
    struct rebind
    {
      using other = tracking_allocator<U>;
    };

  private:
    template<class U>
    friend class tracking_allocator;

    size_t* _counter = 0;
  };
}

using namespace schedulers;

TEST_CASE("The packaged task frees the callable on destruction if not released.", "[package_task_as_c_callback]")
{
  int n = 0;
  {
    tracked_callable f{&n};
    REQUIRE(n == 1);
    {
      auto callback = package_task_as_c_callback(f);
      REQUIRE(n == 2);
    }
    REQUIRE(n == 1);
  }
  REQUIRE(n == 0);
}

TEST_CASE("The packaged task does not free the callable on destruction if released.", "[package_task_as_c_callback]")
{
  int n = 0;
  {
    tracked_callable f{&n};
    void (*f_ptr)(void*);
    void* data;

    REQUIRE(n == 1);
    {
      auto callback = package_task_as_c_callback(f);
      REQUIRE(n == 2);
      auto pointers = callback.release();
      f_ptr = pointers.callback;
      data = pointers.data;
    }
    REQUIRE(n == 2);
    f_ptr(data); // Prevent debugger/analyzer warnings about leaking memory
  }
  REQUIRE(n == 0);
}

TEST_CASE("Calling the packaged task frees the callable on success.", "[package_task_as_c_callback]")
{
  int n = 0;
  {
    tracked_callable f{&n};
    REQUIRE(n == 1);

    auto callback = package_task_as_c_callback(f);
    auto pointers = callback.release();
    REQUIRE(n == 2);
    // Calling the pointer must delete the copy
    pointers.callback(pointers.data);
    REQUIRE(n == 1);
  }
  REQUIRE(n == 0);
}

TEST_CASE("Calling the packaged task frees the callable on failure.", "[package_task_as_c_callback]")
{
  int n = 0;
  {
    tracked_throwing_callable f{&n};
    REQUIRE(n == 1);

    auto callback = package_task_as_c_callback(f);
    auto pointers = callback.release();
    REQUIRE(n == 2);
    // Calling the pointer must delete the copy even if it throws
    REQUIRE_THROWS_AS(pointers.callback(pointers.data), exception_t);
    REQUIRE(n == 1);
  }
  REQUIRE(n == 0);
}

TEST_CASE("Calling the packaged task directly releases ownership.", "[package_task_as_c_callback]")
{
  int n = 0;
  {
    tracked_callable f{&n};
    REQUIRE(n == 1);

    auto callback = package_task_as_c_callback(f);
    REQUIRE(n == 2);
    callback();
    REQUIRE(n == 1);
  }
  REQUIRE(n == 0);
}

TEST_CASE("Custom allocator is used when packaged task is destroyed.", "[package_task_as_c_callback]")
{
  struct T
  {
    char data[sizeof(void*) * 100]; // Make sure we don't trigger alloc elision
    void operator()() { }
  };

  size_t counter = 0;
  {
    auto callback = package_task_as_c_callback(tracking_allocator<>{&counter}, T{});
    REQUIRE(counter > 0);
  }
  REQUIRE(counter == 0);
}

TEST_CASE("Custom allocator is used when packaged task is invoked with success.", "[package_task_as_c_callback]")
{
  struct T
  {
    char data[sizeof(void*) * 100]; // Make sure we don't trigger alloc elision
    void operator()() { }
  };

  size_t counter = 0;

  auto callback = package_task_as_c_callback(tracking_allocator<>{&counter}, T{});
  REQUIRE(counter > 0);
  auto pointers = callback.release();
  REQUIRE(counter > 0);
  // Calling the pointer must delete using the allocator
  pointers.callback(pointers.data);
  REQUIRE(counter == 0);
}

TEST_CASE("Custom allocator is used when packaged task is invoked with failure.", "[package_task_as_c_callback]")
{
  struct T
  {
    char data[sizeof(void*) * 100]; // Make sure we don't trigger alloc elision
    void operator()() { throw exception_t{}; }
  };

  size_t counter = 0;

  auto callback = package_task_as_c_callback(tracking_allocator<>{&counter}, T{});
  REQUIRE(counter > 0);
  auto pointers = callback.release();
  REQUIRE(counter > 0);
  // Calling the pointer must delete using the allocator even if it throws
  REQUIRE_THROWS_AS(pointers.callback(pointers.data), exception_t);
  REQUIRE(counter == 0);
}

TEST_CASE("No allocation for small and trivially copyable types.", "[package_task_as_c_callback]")
{
  static bool b = false;
  package_task_as_c_callback(forbidden_allocator<>{}, [] { b = true; })();
  REQUIRE(b);
}

TEST_CASE("Unallocated objects are restored with same value.", "[package_task_as_c_callback]")
{
  static int x;
  // Not using a lambda so the compiler has to assume "p" can change
  struct check1
  {
    void operator()() { REQUIRE(p == &x); }
    int* p;
  };
  package_task_as_c_callback(forbidden_allocator<>{},check1{&x})();

  static auto i = static_cast<std::uintptr_t>(0x1234567890ABCDEF);
  // Not using a lambda so the compiler has to assume "x" can change
  struct check2
  {
    void operator()() { REQUIRE(x == i); }
    std::uintptr_t x;
  };
  package_task_as_c_callback(forbidden_allocator<>{},check2{i})();
}

TEST_CASE("std::reference_wrapper<T> does not allocate.", "[package_task_as_c_callback]")
{
  auto f = [] {};
  package_task_as_c_callback(forbidden_allocator<>{}, std::ref(f))();
}

TEST_CASE("The cv qualifiers of std::reference_wrapper<cv T> are preserved.", "[package_task_as_c_callback]")
{
  struct F { void operator()() { } };
  struct F_c { void operator()() const { } };
  struct F_cv { void operator()() const volatile { } };
  struct F_v { void operator()() volatile { } };

  F f{};
  const F_c f_c{};
  const volatile F_cv f_cv{};
  volatile F_v f_v{};

  // There are no assertions here, this just has to compile (and not allocate)
  package_task_as_c_callback(forbidden_allocator<>{},std::ref(f));
  package_task_as_c_callback(forbidden_allocator<>{},std::ref(f_c));
  package_task_as_c_callback(forbidden_allocator<>{},std::ref(f_cv));
  package_task_as_c_callback(forbidden_allocator<>{},std::ref(f_v));
}
