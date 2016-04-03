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
#include <cassert>
#include <deque>
#include <functional>
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
    auto push(std::function<void()>&& f) const -> void;
    auto try_pop(std::function<void()>& f) const -> bool;

    static auto get() noexcept -> const main_thread_task_queue&
    {
      return _instance;
    }

  private:
    using lock_t = std::unique_lock<std::mutex>;

    // It's OK to have this a global because you cannot use any "main thread" scheduling before a OS/UI specific mechanism is started in main(). It's also important this queue outlives any scheduler objects using it because we cannot remove entries from the OS/UI event loop that might still be referencing it *after* we destroy the main thread scheduler.
    static const main_thread_task_queue _instance;

    mutable std::mutex _mutex;
    mutable std::deque<std::function<void()>> _queue;
  };
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
    main_thread_task_queue::get().push({std::allocator_arg, alloc, forward<F>(f)});
    dispatch_async_f(dispatch_get_main_queue(), nullptr, [] (void*)
                     {
                       std::function<void()> f;
                       if(main_thread_task_queue::get().try_pop(f))
                       {
                         f();
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
// thread_pool
//

class schedulers::thread_pool : public available_scheduler<thread_pool>
{
public:
  /**
   Create a thread pool using the given number of standard C++ threads.
   */
  explicit thread_pool(unsigned num_threads = std::thread::hardware_concurrency());
  /**
   Create a thread pool using the provided thread factory to create the given number of threads.
   
   \param thread_factory A callable object with signature `std::thread(unsigned, void())`. The thread owned by the returned `std::thread` instance must call the provided thread proc on the new thread and must exit once that returns.
   */
  template<class F>
  explicit thread_pool(F thread_factory, unsigned num_threads = std::thread::hardware_concurrency())
  : _num_threads(num_threads)
  {
    for(unsigned i = 0; i < _num_threads; ++i)
    {
      _threads.push_back(thread_factory(i, [this, i] { run(i); }));
      assert(_threads[i].joinable() && "thread_factory must return joinable threads");
    }
  }
  /**
   The destructor blocks until all threads in the pool exit.
   
   Any scheduled but not yet executed tasks are no longer called but instead destroyed. When using the constructor overload with a custom `thread_factory` you have to make sure the custom thread exits in a timely fashion once the thread proc returns to avoid unnecessary stalling in the destructor.
   
   \warn The destructor must not run on a thread belonging to the thread pool otherwise it will deadlock.
   */
  ~thread_pool();

private:
  friend available_scheduler<thread_pool>;

  using lock_t = std::unique_lock<std::mutex>;

  class task_queue
  {
  public:
    auto done() const -> void;
    auto pop(std::function<void()>& f) const -> bool;
    auto push(std::function<void()>&& f) const -> void;
    auto try_pop(std::function<void()>& f) const -> bool;
    auto try_push(std::function<void()>& f)  const -> bool;

  private:
    mutable std::mutex _mutex;
    mutable std::deque<std::function<void()>> _queue;
    mutable std::condition_variable _ready;
    mutable bool _done{false};
  };

  template<class Alloc, class F>
  auto schedule(const Alloc& alloc, F&& f) const -> void
  {
    auto fun = std::function<void()>{std::allocator_arg, alloc, std::forward<F>(f)};
    push(std::move(fun));
  }

  auto push(std::function<void()> f) const -> void;
  auto run(unsigned index) const -> void;

  const unsigned _num_threads;
  std::vector<task_queue> _task_queues{_num_threads};
  std::vector<std::thread> _threads;
  mutable std::atomic<unsigned> _next_thread{0};
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
   
   \warn There must be a Java frame and class loader on the current thread's callstack to run this constructor.
   */
  java_shared_native_pool() : java_shared_native_pool(std::thread::hardware_concurrency()) { }
  /**
   Create a new C++ thread pool designed to be interoperable with Java as a `java.util.concurrent.Executor` with `num_threads` threads.

   \warn There must be a Java frame and class loader on the current thread's callstack to run this constructor.
   */
  explicit java_shared_native_pool(unsigned num_threads);
  java_shared_native_pool(const java_shared_native_pool&) = default;
  java_shared_native_pool(java_shared_native_pool&&) = default;
  /**
   \warn The destructor must not run on a thread belonging to the thread pool otherwise it will deadlock.
   */
  ~java_shared_native_pool() = default;

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

  // Use shared_ptr so it can be passed to Java via Djinni without forcing java_shared_native_pool into a shared_ptr
  std::shared_ptr<thread_pool> _pool;
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
#elif defined(__APPLE__)
: public libdispatch_global_default
#elif defined(__EMSCRIPTEN__)
: public emscripten_async
#elif defined(_WIN32)
: public win32_default_pool
#else
: public thread_pool
#endif
{
};
