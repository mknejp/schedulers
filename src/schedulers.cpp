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

const main_thread_task_queue main_thread_task_queue::_instance{};

////////////////////////////////////////////////////////////////////////////////
// main_thread_task_queue
//

auto main_thread_task_queue::clear() const noexcept -> void
{
  std::deque<std::function<void()>> temp;
  lock_t lock{_mutex};
  swap(_queue, temp);
}

auto main_thread_task_queue::push(std::function<void()>&& f) const -> void
{
  lock_t lock{_mutex};
  _queue.emplace_back(move(f));
}

auto main_thread_task_queue::try_pop(std::function<void()>& f) const -> bool
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
// task_system::task_queue
//

auto thread_pool::task_queue::done() const -> void
{
  {
    lock_t lock{_mutex};
    _done = true;
  }
  _ready.notify_all();
}

auto thread_pool::task_queue::try_pop(std::function<void()>& f) const -> bool
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

auto thread_pool::task_queue::try_push(std::function<void()>& f)  const -> bool
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

auto thread_pool::task_queue::pop(std::function<void()>& f) const -> bool
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

auto thread_pool::task_queue::push(std::function<void()>&& f) const -> void
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

thread_pool::thread_pool(unsigned num_threads)
: thread_pool([] (unsigned, auto&& f) { return std::thread{forward<decltype(f)>(f)}; }, num_threads)
{
}

thread_pool::~thread_pool()
{
  for(auto&& q : _task_queues)
  {
    q.done();
  }
  for(auto&& t : _threads)
  {
    t.join();
  }
}

auto thread_pool::push(std::function<void()> f) const -> void
{
  assert(f && "empty function object scheduled to thread pool");
  auto thread = _next_thread++;

  for(unsigned i = 0; i < _num_threads; ++i)
  {
    if(_task_queues[(thread + i) % _num_threads].try_push(f))
    {
      return;
    }
  }
  _task_queues[(thread % _num_threads)].push(move(f));
}

auto thread_pool::run(unsigned index) const -> void
{
  while(true)
  {
    std::function<void()> f;
    constexpr unsigned rounds = 8; // How many times we try to steal before sticking to our own queue

    for(unsigned i = 0; i < _num_threads * rounds; ++i)
    {
      if(_task_queues[(index + i) % _num_threads].try_pop(f))
      {
        break;
      }
    }

    if(!f && !_task_queues[index].pop(f))
    {
      break;
    }

    f();
  }
}
