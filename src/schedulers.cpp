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

using schedulers::thread_pool;
using schedulers::main_thread_task_queue;
using schedulers::thread_pool_task_queue;

const main_thread_task_queue main_thread_task_queue::_instance{};

////////////////////////////////////////////////////////////////////////////////
// main_thread_task_queue
//

auto main_thread_task_queue::clear() const noexcept -> void
{
  std::deque<detail::work_item> temp;
  lock_t lock{_mutex};
  swap(_queue, temp);
}

auto main_thread_task_queue::push(detail::work_item&& f) const -> void
{
  lock_t lock{_mutex};
  _queue.emplace_back(move(f));
}

auto main_thread_task_queue::try_pop(detail::work_item& f) const -> bool
{
  lock_t lock{_mutex};
  if(_queue.empty())
  {
    return false;
  }
  f = move(_queue.front());
  _queue.pop_front();
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// thread_pool_work_queue
//

auto thread_pool_task_queue::done() const -> void
{
  {
    lock_t lock{_mutex};
    _done = true;
  }
  _ready.notify_all();
}

auto thread_pool_task_queue::try_pop(detail::work_item& f) const -> bool
{
  lock_t lock{_mutex, std::try_to_lock};
  if(!lock || _queue.empty())
  {
    return false;
  }
  f = move(_queue.front());
  _queue.pop_front();
  return true;
}

auto thread_pool_task_queue::try_push(detail::work_item& f)  const -> bool
{
  {
    lock_t lock{_mutex, std::try_to_lock};
    if(!lock)
    {
      return false;
    }
    _queue.emplace_back(move(f));
  }
  _ready.notify_one();
  return true;
}

auto thread_pool_task_queue::pop(detail::work_item& f) const -> bool
{
  lock_t lock{_mutex};
  while(_queue.empty() && !_done)
  {
    _ready.wait(lock);
  }
  if(_queue.empty())
  {
    return false;
  }
  f = move(_queue.front());
  _queue.pop_front();
  return true;
}

auto thread_pool_task_queue::push(detail::work_item&& f) const -> void
{
  {
    lock_t lock{_mutex};
    _queue.emplace_back(move(f));
  }
  _ready.notify_one();
}

////////////////////////////////////////////////////////////////////////////////
// thread_pool
//

namespace
{
  auto make_thread = [] (int index, const auto& queue, auto&& f)
  {
    return std::thread(std::forward<decltype(f)>(f));
  };
}

schedulers::thread_pool::thread_pool(int num_threads)
: basic_thread_pool(make_thread)
{ }
