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
#include <cassert>
#include <functional>
#include <memory>
#include <type_traits>

namespace schedulers
{
  template<class...>
  using void_t = void;

  template<bool B>
  using bool_constant = std::integral_constant<bool, B>;

  using std::forward;
  using std::move;
  
  // Special tag that matches any parameter
  struct dont_care_t
  {
    template<class Arg>
    dont_care_t(Arg&&) { }
  };

  template<class F, class = void_t<>>
  struct is_invokable : std::false_type { };

  template<class F, class... Args>
  struct is_invokable<F(Args...), void_t<decltype(std::declval<F>()(std::declval<Args>()...))>> : std::true_type { };


  // This *really* should be part of std. No support for arrays needed here.
  template<class T, class Alloc, class... Args>
  auto allocate_unique(const Alloc& alloc, Args&&... args);

  template<class T, class X, class... Args>
  auto allocate_unique(const std::allocator<X>& alloc, Args&&... args);

  template<class T, class Alloc>
  auto make_allocator_deleter(const Alloc& alloc);

  // Set of overloads to determine if a callable is considered "NULL"
  template<class T>
  constexpr bool not_null(const T&) noexcept { return true; }
  template<class R, class... Args>
  constexpr bool not_null(R(*f)(Args...)) noexcept { return f != nullptr; }
  template<class R, class... Args>
  constexpr bool not_null(const std::function<R(Args...)>& f) noexcept { return bool(f); }
  constexpr bool not_null(std::nullptr_t) noexcept { return false; }

  namespace detail
  {
    /**
     A stripped-down specialized version of std::function used for holding move-only callables in a task queue
    
     The lifecycle of a work_item is:
     1. emplace in task queue
     2. move out of queue
     3. call it
     4. destroy it
     
     Doing anything else is undefined.
     */
    class work_item;
  }
}

template<class Alloc>
struct std::uses_allocator<schedulers::detail::work_item, Alloc> : std::true_type { };

////////////////////////////////////////////////////////////////////////////////
// allocator nonsense that should be in std
//

template<class T, class Alloc>
auto schedulers::make_allocator_deleter(const Alloc& alloc)
{
  using rebound_alloc = typename std::allocator_traits<Alloc>::template rebind_alloc<T>;
  using rebound_traits = typename std::allocator_traits<Alloc>::template rebind_traits<T>;
  return [alloc = rebound_alloc{alloc}] (auto p) mutable
  {
    if(p)
    {
      rebound_traits::destroy(alloc, p);
      rebound_traits::deallocate(alloc, p, 1);
    }
  };
}

template<class T, class Alloc, class... Args>
auto schedulers::allocate_unique(const Alloc& alloc, Args&&... args)
{
  using rebound_alloc = typename std::allocator_traits<Alloc>::template rebind_alloc<T>;
  using rebound_traits = typename std::allocator_traits<Alloc>::template rebind_traits<T>;
  auto real_alloc = rebound_alloc{alloc};

  auto deleter = make_allocator_deleter<T>(alloc);

  // In case construction fails we still have to deallocate the storage
  auto temp_deleter = [&real_alloc] (auto p)
  {
    if(p)
    {
      rebound_traits::deallocate(real_alloc, p, 1);
    }
  };
  auto temp = std::unique_ptr<T, decltype(temp_deleter)>{rebound_traits::allocate(real_alloc, 1), move(temp_deleter)};
  rebound_traits::construct(real_alloc, temp.get(), forward<Args>(args)...);

  auto result = std::unique_ptr<T, decltype(deleter)>(temp.release(), move(deleter));
  return result;
}

template<class T, class X, class... Args>
auto schedulers::allocate_unique(const std::allocator<X>& alloc, Args&&... args)
{
  return std::make_unique<T>(forward<Args>(args)...);
}

////////////////////////////////////////////////////////////////////////////////
// work_item
//

class schedulers::detail::work_item
{
public:
  work_item();
  work_item(const work_item&) = delete;
  work_item(work_item&& other) noexcept;
  work_item& operator=(work_item&& other) noexcept;
  ~work_item();

  template<class Alloc, class F>
  work_item(std::allocator_arg_t, const Alloc& alloc, F&& f);

  template<class Alloc>
  work_item(std::allocator_arg_t, const Alloc& alloc, std::nullptr_t) = delete;

  explicit operator bool() const noexcept { return _target != nullptr; }
  auto operator()() && -> void { move(*_target)(); }

private:
  struct base
  {
    virtual auto destroy() noexcept -> void = 0;
    virtual auto destroy_dealloc() noexcept -> void = 0;
    virtual auto move_and_destroy(base* memory) noexcept -> void = 0;
    virtual auto operator()() && -> void = 0;
  protected:
    ~base() = default;
  };

  template<class Alloc, class F>
  struct fun_with_alloc final : base
  {
    template<class T>
    fun_with_alloc(const Alloc& alloc, T&& x)
    : data{alloc, forward<T>(x)}
    { }
    auto destroy() noexcept -> void override
    {
      data.~decltype(data)();
    }
    auto destroy_dealloc() noexcept -> void override
    {
      using rebound_alloc = typename std::allocator_traits<Alloc>::template rebind_alloc<fun_with_alloc>;
      auto real_alloc = rebound_alloc{move(std::get<0>(data))};
      destroy();
      std::allocator_traits<rebound_alloc>::deallocate(real_alloc, this, 1);
    }
    auto move_and_destroy(base* memory) noexcept -> void override
    {
      assert(false && "must not be called");
    }
    auto operator()() && -> void override { move(std::get<1>(data))(); }
    std::tuple<Alloc, F> data;
  };

  template<class F>
  struct fun_without_alloc final : base
  {
    template<class T>
    fun_without_alloc(T&& x)
    : f(forward<T>(x))
    { }
    auto destroy() noexcept -> void override
    {
      f.~F();
    }
    auto destroy_dealloc() noexcept -> void override
    {
      assert(false && "must not be called");
    }
    auto move_and_destroy(base* memory) noexcept -> void override
    {
      new (static_cast<void*>(memory)) fun_without_alloc{move(f)};
      destroy();
    }
    auto operator()() && -> void override { move(f)(); }
    F f;
  };

  // Buffer size follows standard recommendation: vtbl pointer + two-pointer-sized callable
  using buffer_t = std::aligned_storage_t<3 * sizeof(void*)>;

  static auto as_base(buffer_t* buf) noexcept
  {
    return static_cast<base*>(static_cast<void*>(buf));
  }

  auto is_embedded() const noexcept
  {
    return static_cast<void*>(_target) == &_buffer;
  }

  template<class Alloc, class F>
  auto emplace(const Alloc& alloc,
               F&& f,
               std::true_type /* ok */) -> void;

  template<class Alloc, class F>
  auto emplace_impl(const Alloc& alloc,
                    F&& f,
                    std::true_type /* can embed */) -> void;
  template<class Alloc, class F>
  auto emplace_impl(const Alloc& alloc,
                    F&& f,
                    std::false_type /* can embed */) -> void;

  template<class Alloc, class F>
  auto emplace(const Alloc& alloc, F&& f, std::false_type /* not ok */) -> void;

  buffer_t _buffer;
  base* _target;
};

inline schedulers::detail::work_item::work_item()
: _target(nullptr)
{ }

inline schedulers::detail::work_item::work_item(work_item&& other) noexcept
{
  _target = nullptr;
  *this = move(other);
}

inline auto schedulers::detail::work_item::operator=(work_item&& other) noexcept
-> schedulers::detail::work_item&
{
  assert(!_target && "must move to empty work_item");
  assert(other._target && "moved from work_item twice");
  if(other.is_embedded())
  {
    other._target->move_and_destroy(as_base(&_buffer));
    _target = as_base(&_buffer);
  }
  else
  {
    _target = other._target;
  }
  other._target = nullptr;
  return *this;
}

inline schedulers::detail::work_item::~work_item()
{
  if(_target)
  {
    if(is_embedded())
    {
      _target->destroy();
    }
    else
    {
      _target->destroy_dealloc();
    }
  }
}

template<class Alloc, class F>
schedulers::detail::work_item::work_item(std::allocator_arg_t, const Alloc& alloc, F&& f)
{
  assert(not_null(f) && "function is NULL");

  using decayed = std::decay_t<F>;

  constexpr auto move_constructible = std::is_move_constructible<decayed>();
  constexpr auto invokable = is_invokable<decayed&&()>();

  static_assert(move_constructible, "F requires MoveConstructible");
  static_assert(invokable, "F requires Callable<void()>");

  constexpr auto ok = move_constructible && invokable;
  emplace(alloc,
          forward<F>(f),
          bool_constant<ok>());
}

template<class Alloc, class F>
auto schedulers::detail::work_item::emplace(const Alloc& alloc,
                                            F&& f,
                                            std::true_type /* ok */) -> void
{
  using decayed = std::decay_t<F>;
  using embedded = fun_without_alloc<decayed>;
  constexpr auto can_embed = sizeof(embedded) <= sizeof(_buffer) && std::is_nothrow_move_constructible<decayed>();
  emplace_impl(alloc, forward<F>(f), bool_constant<can_embed>());
}

template<class Alloc, class F>
auto schedulers::detail::work_item::emplace_impl(const Alloc& alloc,
                                                 F&& f,
                                                 std::true_type /* can embed */) -> void
{
  using decayed = std::decay_t<F>;
  new (static_cast<void*>(&_buffer)) fun_without_alloc<decayed>(forward<F>(f));
  _target = as_base(&_buffer);
}

template<class Alloc, class F>
auto schedulers::detail::work_item::emplace_impl(const Alloc& alloc,
                                                 F&& f,
                                                 std::false_type /* can embed */) -> void
{
  using decayed = std::decay_t<F>;
  auto p = allocate_unique<fun_with_alloc<Alloc, decayed>>(alloc, alloc, forward<F>(f));
  _target = p.release();
}
