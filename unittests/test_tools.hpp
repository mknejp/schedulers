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

#include "catch.hpp"

// Keeps track of the number of instances without throwing when called
class tracked_callable
{
public:
  tracked_callable(int* counter) : _counter(counter) { ++*_counter; }
  tracked_callable(const tracked_callable& other) noexcept : _counter(other._counter) { ++*_counter; }
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

// Fails test if used to allocate more than once
template<class T = void>
class realloc_forbidden_allocator
{
public:
  realloc_forbidden_allocator(size_t* counter) : _counter(counter) { }

  template<class U>
  realloc_forbidden_allocator(realloc_forbidden_allocator<U> other) noexcept
  : _counter(other._counter)
  , _allocated(other._allocated)
  { }

  using value_type = T;

  T* allocate(size_t n)
  {
    if(*_allocated)
    {
      FAIL("attempted to allocate more than once");
    }
    *_allocated = true;
    *_counter += sizeof(T) * n;
    return std::allocator<T>().allocate(n);
  }
  void deallocate(T* ptr, size_t n)
  {
    *_counter -= sizeof(T) * n;
    std::allocator<T>().deallocate(ptr, n);
  }

  template<class U>
  struct rebind
  {
    using other = realloc_forbidden_allocator<U>;
  };

private:
  template<class U>
  friend class realloc_forbidden_allocator;

  size_t* _counter;
  std::shared_ptr<bool> _allocated = std::make_shared<bool>(false);
};

// Keeps track of the number of bytes allocated in a user-declared location
template<class T = void>
class tracking_allocator
{
public:
  tracking_allocator(size_t* counter) : _counter(counter) { }

  template<class U>
  tracking_allocator(tracking_allocator<U> other) noexcept
  : _counter(other._counter)
  { }

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
