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

#include "schedulers/schedulers.hpp"
#include "catch.hpp"

using namespace schedulers;

SCENARIO("thread_pool shuts down properly.", "[thread_pool]")
{
  GIVEN("a default constructed thread pool")
  {
    auto pool = std::make_unique<thread_pool>(std::thread::hardware_concurrency() - 1);

    WHEN("not scheduling any tasks")
    {
      AND_WHEN("running its destructor")
      {
        THEN("it shuts down")
        {
          pool.reset();
        }
      }
    }

    WHEN("enqueuing many tasks")
    {
      static volatile int sink;

      for(int i = 0; i < 100'000; ++i)
      {
        (*pool)([]
        {
          // simulate some work
          for(int i = 0; i < 1000; ++i)
            sink = i;
        });
      }
      AND_WHEN("running its destructor")
      {
        THEN("it shuts down")
        {
          pool.reset();
        }
      }
    }
  }
}

// Compile tests for custom work queues
namespace
{
  // A work item that *does not* specialize std::uses_allocator but is still constructible with std::allocator_args_t
  struct work_item_1
  {
    work_item_1() = default;
    template<class Alloc, class F>
    work_item_1(std::allocator_arg_t, const Alloc& a, F&& f) { }
    auto operator()() -> void { }
    explicit operator bool() const { return true; }
  };

  // A work item that *does* specialize std::uses_allocator but is still constructible with std::allocator_args_t
  struct work_item_2
  {
    work_item_2() = default;
    template<class Alloc, class F>
    work_item_2(std::allocator_arg_t, const Alloc& a, F&& f) { }
    auto operator()() -> void { }
    explicit operator bool() const { return true; }
  };

  // A work item that *does* specialize std::uses_allocator and accepts alloc as last argument
  struct work_item_3
  {
    work_item_3() = default;
    template<class Alloc, class F>
    work_item_3(std::allocator_arg_t, const Alloc& a, F&& f) { }
    auto operator()() -> void { }
    explicit operator bool() const { return true; }
  };

  // A work item that *does not* specialize std::uses_allocator and *does not* accept an allocator
  struct work_item_4
  {
    work_item_4() = default;
    template<class F>
    work_item_4(F&& f) { }
    auto operator()() -> void { }
    explicit operator bool() const { return true; }
  };

  volatile bool some_bool;

  template<class work_item>
  struct test_work_queue
  {
    using work_t = work_item;

    auto done() const -> void { }
    auto push(work_t&&) const -> void { }
    auto pop(work_t&) const -> bool { return some_bool; }
    auto try_pop(work_t&) const -> bool { return some_bool; }
    auto try_push(work_t&) const -> bool { return some_bool; }
  };

  struct some_fun
  {
    auto operator()() -> void { }
  };
}

template<class Alloc>
struct std::uses_allocator<work_item_2, Alloc> : std::true_type { };
template<class Alloc>
struct std::uses_allocator<work_item_3, Alloc> : std::true_type { };

template class schedulers::basic_thread_pool<test_work_queue<work_item_1>, std::thread>;
template void schedulers::basic_thread_pool<test_work_queue<work_item_1>, std::thread>::operator()(some_fun&&) const;

template class schedulers::basic_thread_pool<test_work_queue<work_item_2>, std::thread>;
template void schedulers::basic_thread_pool<test_work_queue<work_item_2>, std::thread>::operator()(some_fun&&) const;

template class schedulers::basic_thread_pool<test_work_queue<work_item_3>, std::thread>;
template void schedulers::basic_thread_pool<test_work_queue<work_item_3>, std::thread>::operator()(some_fun&&) const;

template class schedulers::basic_thread_pool<test_work_queue<work_item_4>, std::thread>;
template void schedulers::basic_thread_pool<test_work_queue<work_item_4>, std::thread>::operator()(some_fun&&) const;
