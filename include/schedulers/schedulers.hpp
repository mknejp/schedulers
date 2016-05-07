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

#pragma once
#include "schedulers/package_task_as_c_callback.hpp"
#include <deque>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#elif defined(__APPLE__)
#include <dispatch/dispatch.h>

#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>

#elif defined(__ANDROID__) || defined(ANDROID)
#define SCHEDULERS_FOR_ANDROID
#if !defined(SCHEDULERS_FOR_JAVA)
#define SCHEDULERS_FOR_JAVA
#endif
#endif

#if defined(SCHEDULERS_FOR_JAVA)
#include <djinni_support.hpp>
#endif

namespace schedulers
{
  /**
   Schedules tasks to the default system-provided thread pool to minimize thread oversubscription.
   
   This is a good general-purpose default for any background tasks as it leaves the burden of thread management to the system, avoids unnecessary thread oversubscription, and may improve overall work balancing.
   - On Win32 this is the system thread pool.
   - On Apple platforms this is the `libdispatch` default global queue.
   - When compiling with Emscripten it uses `emscripten_async_call`.
   - On Android (or if `SCHEDULERS_FOR_JAVA` is defined before including `schedulers.hpp`) it is a thread pool that has a corresponding `java.util.concurrent.Executor` interface avilable allowing the pool to be shared equally between C++ and Java.
   - Otherwise it is a simple thread pool.

   \todo: WinRT
   */
  class default_scheduler;
  /**
   A thread pool with customizable task queue and thread object.

   The thread pool implements task stealing between the per-thread queues using the `try_push()` and `try_pop()` methods of `WorkQueue`. If the queues do not support task stealing these methods should always return `false` and do nothing.

   The number of threads is fixed upon creation of the pool.

   \tparam WorkQueue The type used for the per-thread work queue. Must be `DefaultConstructible`. All calls to the queues (except the constructor and destructor) must be data race free. The nested type `work_t` must have one of these constructor signatures: `work_t(std::allocator_arg_t, Alloc, F)`, or `work_t(F, Alloc)` if `std::uses_allocator<work_t, Alloc>::value` is `true`, or `work_t(F)` otherwise.
   \tparam ThreadHandle The type used to own the system threads. The factory provided in the constructor is called to create and launch each thread. The type must have `join()` method with the same semantics as `std::thread::join()`.
   */
  template<class WorkQueue, class ThreadHandle>
  class basic_thread_pool;
  /**
   Schedules tasks to a user-created thread pool.
   
   \note If your application uses Java you will not be able to call Java methods via JNI from tasks on this scheduler unless your thread factory makes the necessary precautions.
   */
  class thread_pool;
  /**
   Schedules tasks to a user-provided `libdispatch` queue.
   */
  class libdispatch_queue;
  /**
   Schedules tasks to the main queue of `libdispatch`.
   */
  class libdispatch_main;
  /**
   Schedules to `libdispatch`'s global queue with default priority.

   \note Avoid using this class directly and use system_thread_pool instead as it will automatically adjust to the platform you're on.
   */
  class libdispatch_global_default;
  /**
   Schedules tasks to a Win32 message queue.
   */
  class win32_message_queue;
  /**
   Schedules on the Win32 default thread pool.

   \note Avoid using this class directly and use system_thread_pool instead as it will automatically adjust to the platform you're on.
   */
  class win32_default_pool;
  /**
   Schedules on `emscripten_async_call`.

   \note Avoid using this class directly and use system_thread_pool instead as it will automatically adjust to the platform you're on.
   */
  class emscripten_async;
  /**
   Schedules to a C++ thread pool but provdes a `java.util.concurrent.Executor` interface to also support scheduling for Java tasks.

   \note Avoid using this class directly and use system_thread_pool instead as it will automatically adjust to the platform you're on.
   */
  class java_shared_native_pool;
  /**
   Schedules to Android's main thread `ALooper`.
   */
  class android_main_looper;
  /**
   Turns a scheduler into one with reference semantics.
   
   This is basically a `shared_ptr<const Scheduler>` with a call operator.
   */
  template<class Scheduler>
  class shared_scheduler;
  template<class Scheduler, class... Args>
  auto make_shared_scheduler(Args&&... args);
  /**
   Provides a shared interface for all schedulers which are available on this system.
   
   Provides the static `available` member and overloads of the call operator.
   
   In order to use this derive from it using CRTP, make available_scheduler<YourClass> a friend and provide a private `schedule` function with the signature as shown below:
   ```cpp
   class my_scheduler : public available_scheduler<my_scheduler>
   {
   private:
     friend available_scheduler<my_scheduler>;
   
     template<class Alloc, class F>
     void schedule(const Alloc& alloc, F&& f) const { ... }
   };
   ```
   */
  template<class Derived>
  struct available_scheduler;

  /**
   Serves as a base class for schedulers which are not available on this system.

   Provides the static `available` member and marks the call operator as deleted.
   */
  struct unavailable_scheduler;

  namespace jni
  {
    struct default_scheduler;
    struct android_main_looper;
  }
  namespace objcpp
  {
    struct default_scheduler;
  }

  /**
   Special type of queue for "main thread"-type schedulers integrating into external systems.
   
   The difference to a normal task queue is that the main thread never waits on the queue if it isn't empty, that is the job of the OS/UI event loop. Instead we have to signal the system that we have a task ready, and when it's our turn we pop one item from the queue and return control to the system.
   */
  class main_thread_task_queue
  {
  public:
    /// Call from "main thread" scheduler's destructors to cleanup any pending tasks.
    auto clear() const noexcept -> void;
    auto push(detail::work_item&& f) const -> void;
    auto try_pop(detail::work_item& f) const -> bool;

    static auto get() noexcept -> const main_thread_task_queue&
    {
      return _instance;
    }

  private:
    using lock_t = std::unique_lock<std::mutex>;

    // It's OK to have this a global because you cannot use any "main thread" scheduling before a OS/UI specific mechanism is started in main(). It's also important this queue outlives any scheduler objects using it because we cannot remove entries from the OS/UI event loop that might still be referencing it *after* we destroy the main thread scheduler.
    static const main_thread_task_queue _instance;

    mutable std::mutex _mutex;
    mutable std::deque<detail::work_item> _queue;
  };
  /**
   The default task queue used in the thread_pool class.
   
   This also provides the minimum interface required for user-defined queues.
   */
  class thread_pool_task_queue;
}

////////////////////////////////////////////////////////////////////////////////
// core scheduler interface
//

template<class Derived>
struct schedulers::available_scheduler
{
  static constexpr std::true_type available{};

  using result_type = void;

  template<class F>
  void operator()(F&& f) const
  {
    self().schedule(std::allocator<char>{}, forward<F>(f));
  }

  template<class Alloc, class F>
  void operator()(const Alloc& alloc, F&& f) const
  {
    self().schedule(alloc, forward<F>(f));
  }

private:
  auto self() const -> const Derived& { return *static_cast<const Derived*>(this); }
};

struct schedulers::unavailable_scheduler
{
  static constexpr std::false_type available{};

  using result_type = void;

  template<class F>
  void operator()(F&& f) const = delete;

  template<class Alloc, class F>
  void operator()(const Alloc& alloc, F&& f) const = delete;
};

////////////////////////////////////////////////////////////////////////////////
// libdispatch queues
//

#if defined(__APPLE__)
class schedulers::libdispatch_queue : public available_scheduler<libdispatch_queue>
{
public:
  explicit libdispatch_queue(dispatch_queue_t queue) : _queue(queue) { }

private:
  friend available_scheduler<libdispatch_queue>;
  friend objcpp::default_scheduler;

  template<class Alloc, class F>
  void schedule(const Alloc& alloc, F&& f) const
  {
    auto callback = package_task_as_c_callback<dispatch_function_t>(alloc, forward<F>(f));
    dispatch_async_f(_queue, callback.get().data, callback.get().callback);
    callback.release();
  }

  dispatch_queue_t _queue;
};

class schedulers::libdispatch_main : public available_scheduler<libdispatch_main>
{
public:
  libdispatch_main(const libdispatch_main&) = delete;
  libdispatch_main(libdispatch_main&&) = delete;
  ~libdispatch_main()
  {
    main_thread_task_queue::get().clear();
  }

private:
  friend available_scheduler<libdispatch_main>;

  template<class Alloc, class F>
  auto scheduler(const Alloc& alloc, F&& f) const
  {
    main_thread_task_queue::get().push({alloc, forward<F>(f)});
    dispatch_async_f(dispatch_get_main_queue(), nullptr, [] (void*)
                     {
                       detail::work_item f;
                       if(main_thread_task_queue::get().try_pop(f))
                       {
                         move(f)();
                       }
                     });
  }
};

class schedulers::libdispatch_global_default : public libdispatch_queue
{
public:
  libdispatch_global_default()
  : libdispatch_queue{dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0)}
  { }
};

#else
class schedulers::libdispatch_queue : public unavailable_scheduler { };
class schedulers::libdispatch_main : public unavailable_scheduler { };
class schedulers::libdispatch_global_default : public unavailable_scheduler { };
#endif

////////////////////////////////////////////////////////////////////////////////
// win32_default_pool
//

#if defined(_WIN32)
class schedulers::win32_default_pool : public available_scheduler<win32_default_pool>
{
private:
  friend available_scheduler<win32_system_pool>;

  template<class Alloc, class F>
  void schedule(const Alloc& alloc, F&& f) const
  {
    auto callback = package_task_as_c_callback<LPTHREAD_START_ROUTINE>(alloc, forward<F>(f));
    ::QueueUserWorkItem(callback.get().callback, callback.get().data, WT_EXECUTEDEFAULT);
    callback.release();
  }
};
#else
class schedulers::win32_default_pool : public unavailable_scheduler { };
#endif

////////////////////////////////////////////////////////////////////////////////
// emscripten_async
//

#if defined(__EMSCRIPTEN__)
class schedulers::emscripten_async : public available_scheduler<emscripten_async>
{
private:
  friend available_scheduler<emscripten_async>;

  template<class Alloc, class F>
  void schedule(const Alloc& alloc, F&& f) const
  {
    auto callback = package_task_as_c_callback<em_arg_callback_func>(alloc, forward<F>(f));
    ::emscripten_async_call(callback.get().callback, callback.get().data, 0);
    callback.release();
  }
};
#else
class schedulers::emscripten_async : public unavailable_scheduler { };
#endif

////////////////////////////////////////////////////////////////////////////////
// shared_scheduler
//

namespace schedulers
{
  namespace detail
  {
    template<class Scheduler, bool = Scheduler::available()>
    class shared_scheduler_base;
  }
}

template<class Scheduler>
class schedulers::detail::shared_scheduler_base<Scheduler, true>
: public available_scheduler<shared_scheduler_base<Scheduler, true>>
{
public:
  shared_scheduler_base() = default;

  template<class... Args>
  shared_scheduler_base(Args&&... args)
  : _ptr(std::make_shared<const Scheduler>(forward<Args>(args)...))
  { }

  template<class Alloc, class... Args>
  shared_scheduler_base(std::allocator_arg_t, const Alloc& alloc, Args&&... args)
  : _ptr(std::allocate_shared<const Scheduler>(alloc, forward<Args>(args)...))
  { }

private:
  friend available_scheduler<shared_scheduler<Scheduler>>;

  template<class Alloc, class F>
  void schedule(const Alloc& alloc, F&& f) const
  {
    (*_ptr)(alloc, forward<F>(f));
  }

  std::shared_ptr<const Scheduler> _ptr;
};

template<class Scheduler>
class schedulers::detail::shared_scheduler_base<Scheduler, false>
: public unavailable_scheduler
{ };

template<class Scheduler>
class schedulers::shared_scheduler
: public detail::shared_scheduler_base<Scheduler>
{
public:
  using detail::shared_scheduler_base<Scheduler>::shared_scheduler_base;
};

template<class Scheduler, class... Args>
auto schedulers::make_shared_scheduler(Args&&... args)
{
  return shared_scheduler<std::decay_t<Scheduler>>{forward<Args>(args)...};
}

////////////////////////////////////////////////////////////////////////////////
// custom_thread_pool
//

template<class WorkQueue, class ThreadHandle>
class schedulers::basic_thread_pool
: public available_scheduler<basic_thread_pool<WorkQueue, ThreadHandle>>
{
public:
  using work_t = typename WorkQueue::work_t;
  static_assert(std::is_default_constructible<work_t>(), "Work item of work queue must be default constructible");
  static_assert(std::is_convertible<decltype(!std::declval<work_t>()), bool>(), "Work item of work queue must be contextually convertible to bool");

  /**
   Create a thread pool with the given thread factory and number of threads.
   
   \param f A factory for threads. It is called with the zero-based thread index, a reference to the thread's own work queue, and a `Callable<void()>`. The thread owned by the returned handle must call a copy of the provided function in the context of the new thread and exit in a timely fashion once it returns.
   \param num_threads Determines how many threads are created for the pool.
   */
  template<class ThreadFactory>
  basic_thread_pool(ThreadFactory f, unsigned num_threads = std::thread::hardware_concurrency());
  /**
   The destructor blocks until all threads in the pool exit.

   What happens to any scheduled but not yet executed tasks is the responsibility of `WorkQueue`.

   \warning The destructor must not run on a thread belonging to the thread pool otherwise it will deadlock.
   */
  ~basic_thread_pool();

private:
  friend available_scheduler<basic_thread_pool<WorkQueue, ThreadHandle>>;

  template<class ThreadFactory>
  auto start(ThreadFactory& f, std::true_type /* ok */) -> void;

  template<class ThreadFactory>
  auto start(ThreadFactory& f, std::false_type /* not ok */) -> void;

  // A little dance to figure out how to construct work_t
  template<class Alloc, class F>
  auto schedule(const Alloc& alloc, F&& f) const -> void;
  // Supports (allocator_arg, alloc, f)
  template<class Alloc, class F>
  auto schedule(const Alloc& alloc,
                F&& f,
                std::true_type /* has allocator_arg_t */,
                dont_care_t /* uses_allocator */) const -> void;
  // Supports (f, alloc)
  template<class Alloc, class F>
  auto schedule(const Alloc& alloc,
                F&& f,
                std::false_type /* has allocator_arg_t */,
                std::true_type /* uses_allocator */) const -> void;
  template<class Alloc, class F>
  // No allocator support
  auto schedule(const Alloc& alloc,
                F&& f,
                std::false_type /* has allocator_arg_t */,
                std::false_type /* uses_allocator */) const -> void;
  auto schedule(work_t&& work) const -> void;

  auto run(int index) const -> void;

  const unsigned _num_threads;
  std::vector<WorkQueue> _queues{_num_threads};
  std::vector<ThreadHandle> _threads;
  mutable std::atomic<unsigned> _next_thread{0}; // Must be unsigned because it can overflow
};

template<class WorkQueue, class ThreadHandle>
template<class ThreadFactory>
schedulers::basic_thread_pool<WorkQueue, ThreadHandle>::basic_thread_pool(ThreadFactory f,
                                                                          unsigned num_threads)
: _num_threads(std::max(1u, num_threads))
{
  auto thread_proc = [this, i = 0] {};
  constexpr auto thread_factory_ok = std::is_constructible<ThreadHandle, std::result_of_t<ThreadFactory&(unsigned, WorkQueue&, decltype(thread_proc))>>();

  static_assert(thread_factory_ok, "ThreadFactory must be Callable<R(unsigned, WorkQueue&, F)> with F a Callable<void()> and R convertible to ThreadHandle");

  start(f, thread_factory_ok);
}

template<class WorkQueue, class ThreadHandle>
schedulers::basic_thread_pool<WorkQueue, ThreadHandle>::~basic_thread_pool()
{
  for(auto&& q : _queues)
  {
    q.done();
  }
  for(auto&& t : _threads)
  {
    t.join();
  }
}

template<class WorkQueue, class ThreadHandle>
template<class ThreadFactory>
auto schedulers::basic_thread_pool<WorkQueue, ThreadHandle>::start(ThreadFactory& f,
                                                                   std::true_type /* ok */)
-> void
{
  assert(_num_threads > 0 && "invalid number of threads");

  _threads.reserve(_num_threads);
  for(int i = 0; i < _num_threads; ++i)
  {
    _threads.emplace_back(f(i, _queues[i], [this, i] { run(i); }));
  }
}

template<class WorkQueue, class ThreadHandle>
template<class Alloc, class F>
auto schedulers::basic_thread_pool<WorkQueue, ThreadHandle>::schedule(const Alloc& alloc, F&& f) const
-> void
{
  constexpr auto has_allocator_arg = std::is_constructible<work_t, std::allocator_arg_t, Alloc, F&&>::value;
  constexpr auto uses_alloc = std::uses_allocator<work_t, Alloc>::value;

  schedule(alloc, forward<F>(f), bool_constant<has_allocator_arg>(), bool_constant<uses_alloc>());
}

template<class WorkQueue, class ThreadHandle>
template<class Alloc, class F>
auto schedulers::basic_thread_pool<WorkQueue, ThreadHandle>::schedule(const Alloc& alloc,
                                                                      F&& f,
                                                                      std::true_type /* has allocator_arg_t */,
                                                                      dont_care_t /* uses_allocator */) const
-> void
{
  schedule(work_t{std::allocator_arg, alloc, forward<F>(f)});
}

template<class WorkQueue, class ThreadHandle>
template<class Alloc, class F>
auto schedulers::basic_thread_pool<WorkQueue, ThreadHandle>::schedule(const Alloc& alloc,
                                                                      F&& f,
                                                                      std::false_type /* has allocator_arg_t */,
                                                                      std::true_type /* uses_allocator */) const
-> void
{
  schedule(work_t{forward<F>(f), alloc});
}

template<class WorkQueue, class ThreadHandle>
template<class Alloc, class F>
auto schedulers::basic_thread_pool<WorkQueue, ThreadHandle>::schedule(const Alloc& /*alloc*/,
                                                                      F&& f,
                                                                      std::false_type /* has allocator_arg_t */,
                                                                      std::false_type /* uses_allocator */) const
-> void
{
  schedule(work_t{forward<F>(f)});
}

template<class WorkQueue, class ThreadHandle>
auto schedulers::basic_thread_pool<WorkQueue, ThreadHandle>::schedule(work_t&& f) const -> void
{
  auto thread = _next_thread++;

  for(unsigned i = 0; i < _num_threads; ++i)
  {
    if(_queues[(thread + i) % _num_threads].try_push(f))
    {
      return;
    }
  }
  _queues[(thread % _num_threads)].push(move(f));
}

template<class WorkQueue, class ThreadHandle>
auto schedulers::basic_thread_pool<WorkQueue, ThreadHandle>::run(int index) const -> void
{
  while(true)
  {
    work_t f;
    constexpr unsigned rounds = 8; // How many times we try to steal before sticking to our own queue

    for(int i = 0; i < _num_threads * rounds; ++i)
    {
      if(_queues[(index + i) % _num_threads].try_pop(f))
      {
        break;
      }
    }

    if(!f && !_queues[index].pop(f))
    {
      break;
    }

    move(f)();
  }
}

////////////////////////////////////////////////////////////////////////////////
// thread_pool
//

class schedulers::thread_pool_task_queue
{
public:
  // The type of function object stored in the queue
  using work_t = detail::work_item;

  /**
   Notify the queue to exit.

   Any threads waiting on pop() must exit in a timely fashion. Calls to try_pop() must fail even if the queue is not empty.
   */
  auto done() const -> void;
  /**
   Wait for a work item to appear in the queue and pop it.
   */
  auto pop(work_t& f) const -> bool;
  /**
   Push a new work item to the queue, blocking if necessary.
   */
  auto push(work_t&& f) const -> void;
  /**
   Try to pop a work item from the queue without blocking.

   If a work item is available and it can be popped without blocking then return `true` and extract the work item into `f`.
   */
  auto try_pop(work_t& f) const -> bool;
  /**
   Try to push a work item into the queue without blocking.

   If the work item can be pushed into the queue without blocking then copy it into `f` and return `true`. Otherwise return `false`.
   */
  auto try_push(work_t& f)  const -> bool;

private:
  using lock_t = std::unique_lock<std::mutex>;

  mutable std::mutex _mutex;
  mutable std::deque<work_t> _queue;
  mutable std::condition_variable _ready;
  mutable bool _done{false};
};

class schedulers::thread_pool
: public basic_thread_pool<thread_pool_task_queue, std::thread>
{
public:
  /**
   Create a thread pool using the given number of standard C++ threads.
   */
  explicit thread_pool(int num_threads = std::thread::hardware_concurrency());
};

////////////////////////////////////////////////////////////////////////////////
// java_shared_native_pool
//

#if defined(SCHEDULERS_FOR_JAVA)
class schedulers::java_shared_native_pool : public available_scheduler<java_shared_native_pool>
{
public:
  /**
   Create a new C++ thread pool designed to be interoperable with Java as a `java.util.concurrent.Executor` with `hardware_cuncurreny` threads.
   
   \warning There must be a Java frame and class loader on the current thread's callstack to run this constructor.
   */
  java_shared_native_pool() : java_shared_native_pool(std::thread::hardware_concurrency()) { }
  /**
   Create a new C++ thread pool designed to be interoperable with Java as a `java.util.concurrent.Executor` with `num_threads` threads.

   \warning There must be a Java frame and class loader on the current thread's callstack to run this constructor.
   */
  explicit java_shared_native_pool(int num_threads);

private:
  friend available_scheduler<java_shared_native_pool>;
  friend jni::default_scheduler;

  java_shared_native_pool(std::shared_ptr<thread_pool> pool)
  : _pool(move(pool))
  { }

  template<class Alloc, class F>
  void schedule(const Alloc& alloc, F&& f) const
  {
    (*_pool)(alloc, forward<F>(f));
  }

  using pool_t = basic_thread_pool<thread_pool_task_queue, std::thread>;

  // Use shared_ptr so it can be passed to Java via Djinni without forcing java_shared_native_pool into a shared_ptr
  std::shared_ptr<pool_t> _pool;
};
#else
class schedulers::java_shared_native_pool : public unavailable_scheduler { };
#endif

////////////////////////////////////////////////////////////////////////////////
// android_main_looper
//

#if defined(SCHEDULERS_FOR_ANDROID)
struct ALooper;

class schedulers::android_main_looper : public available_scheduler<android_main_looper>
{
public:
  android_main_looper();
  android_main_looper(const android_main_looper&) = delete;
  android_main_looper(android_main_looper&&) = delete;
  ~android_main_looper();

private:
  friend available_scheduler<android_main_looper>;

  template<class Alloc, class F>
  auto schedule(const Alloc& alloc, F&& f) const -> void
  {
    main_thread_task_queue::get().push({std::allocator_arg, alloc, forward<F>(f)});
    post();
  }

  auto post() const -> void;

  int _write_fd;
  int _read_fd;
  ALooper* _looper;
};
#else
class schedulers::android_main_looper : public unavailable_scheduler { };
#endif

////////////////////////////////////////////////////////////////////////////////
// default_scheduler
//

class schedulers::default_scheduler
#if defined(SCHEDULERS_FOR_JAVA)
: public java_shared_native_pool
#elif defined(__EMSCRIPTEN__)
: public emscripten_async
#elif defined(__APPLE__)
: public libdispatch_global_default
#elif defined(_WIN32)
: public win32_default_pool
#else
: public thread_pool
#endif
{
};
