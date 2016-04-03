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
#include "schedulers/utils.hpp"
#include <tuple>

namespace schedulers
{
  /**
   Package any function object with the signature `void()` in such a way that it can be used with C-based callback APIs.

   A copy of `f` is packaged in such a fashion that it is compatible with virtually every C-based callback API. They usually expect a function pointer of signature `void(*)(void*)` and a single `void*` value holding the user data which is passed to the callback.

   The exact type returned is unspecified and depends heavily on the type traits of `F`. It is a RAII wrapper owning the resources required to wrap the function object, which are released in the detructor, providing exception safety. The values required for the C API are accessible from its `.get()` member which returns a c_callback_data object. Once ownership of the data has been safely passed to C use `.release()` to give up ownership of the data (returns the same value as `.get()`). This follows the same idiom as using `std::unique_ptr` for passing a pointer to a C interface in an exception safe way.

   ```cpp
   extern "C" void plain_old_c_function(void(*callback)(void*), void* data); // Calls callback(data) at some point

   auto c_callback = package_task_as_c_callback([...] { ... });
   
   ... // potentially throwing/returning C++ code

   plain_old_c_function(c_allback.get().callback, c_callback.get().data);
   c_callback.release(); // Must give up ownership here
   ```
   The returned type is always `MoveConstructible` and optionally `CopyConstructible`. The function pointer is created in such a fashion that it automatically takes care of all cleanup after invoking `callback` with `data` as parameter, even in the presence of an exception. As a consequence it is undefined behavior to invoke the callback multiple times.
   
   The returned type is itself a function object callable as `void()`. Calling it implies a call to `.release()`.
   
   \tparam FunctionPointerType
    The exact type of function pointer used as C callback. It defaults to `void(*)(void*)` but can be specified manually if needed (must still conform to the `void(void*)` signature). This is necessary for some APIs which have special qualifiers on their function pointers. For example all Win32 callbacks must have the `__stdcall` calling convention. These APIs usually provide typedefs like `LPTHREAD_START_ROUTINE` in which case you would call `package_task_as_c_callback<LPTHREAD_START_ROUTINE>(...)`. Likewise if wrapping callables for Grand Central Dispatch one would use `package_task_as_c_callback<dispatch_function_t>(...)`.
   */
  template<class FunctionPointerType = void(*)(void*), class F>
  auto package_task_as_c_callback(F&& f);

  /**
   This overload is identical to the single-parameter version except it allows you to control how memory is allocated if necessary.
   */
  template<class FunctionPointerType = void(*)(void*), class Alloc, class F>
  auto package_task_as_c_callback(const Alloc& alloc, F&& f);

  template<class FunctionPointerType>
  struct c_callback_data
  {
    /// The function pointer to be passed as C callback
    FunctionPointerType callback;
    /// The value to be passed as C callback data
    void* data;
  };

  struct bad_callback { };

  namespace detail
  {
    // DataOwner must have members get() and release() returning something convertible to void*
    template<class FunctionPointerType, class DataOwner>
    class c_callback;

    // General purpose version: must allocate copy of f
    template<class FunctionPointerType, class F, class Alloc>
    auto package_task_as_c_callback_impl(std::true_type /* valid types */,
                                         F&& f,
                                         const Alloc& alloc,
                                         std::false_type /* can elide alloc */);

    // Specialization in case allocation is not necessary
    template<class FunctionPointerType, class F>
    auto package_task_as_c_callback_impl(std::true_type /* valid types */,
                                         F&& f,
                                         dont_care_t /* alloc */,
                                         std::true_type /* can elide alloc */);

    // Specialization for std::reference_wrapper: never allocates
    template<class FunctionPointerType, class T>
    auto package_task_as_c_callback_impl(std::true_type /* valid types */,
                                         std::reference_wrapper<T> f,
                                         dont_care_t /* alloc */,
                                         dont_care_t /* can elide alloc */);

    template<class FunctionPointerType>
    auto package_task_as_c_callback_impl(std::false_type,
                                         dont_care_t,
                                         dont_care_t,
                                         dont_care_t) -> bad_callback;

    template<class FunctionPointerType, class DataOwner>
    auto make_c_callback(FunctionPointerType f_ptr, DataOwner owner);
  }
}

////////////////////////////////////////////////////////////////////////////////
// package_task_as_c_callback
//

template<class FunctionPointerType, class F>
auto schedulers::package_task_as_c_callback(F&& f)
{
  return schedulers::package_task_as_c_callback(std::allocator<char>{}, forward<F>(f));
}

template<class FunctionPointerType, class Alloc, class F>
auto schedulers::package_task_as_c_callback(const Alloc& alloc, F&& f)
{
  using function_t = std::decay_t<F>;

  constexpr auto function_ptr_ok = is_invokable<FunctionPointerType(void*)>();
  constexpr auto callable_ok = is_invokable<function_t&&()>();
  constexpr auto all_ok = function_ptr_ok && callable_ok;

  static_assert(function_ptr_ok, "function ppointer for a C callback must be callable with a void* argument");
  static_assert(callable_ok, "function object for C callback must be callable with no arguments");

  constexpr auto can_elide_alloc = sizeof(function_t) <= sizeof(void*) && std::is_trivially_copyable<function_t>();

  return detail::package_task_as_c_callback_impl<FunctionPointerType>(bool_constant<all_ok>(),
                                                                      forward<F>(f),
                                                                      alloc,
                                                                      bool_constant<can_elide_alloc>());
}

////////////////////////////////////////////////////////////////////////////////
// package_task_as_c_callback_impl
// specialization for callables where we cannot elide the allocation
//

template<class FunctionPointerType, class F, class Alloc>
auto schedulers::detail::package_task_as_c_callback_impl(std::true_type /* valid types */,
                                                         F&& f,
                                                         const Alloc& alloc,
                                                         std::false_type /* can elide alloc */)
{
  using function_t = std::decay_t<F>;

  // We use tuple as node because we can expect it to implement the empty base optimization.
  using node_t = std::tuple<Alloc, function_t>;

  using std::get;

  struct deleter
  {
    node_t* ptr;
    ~deleter()
    {
      make_allocator_deleter<node_t>(get<0>(*this->ptr))(this->ptr);
    }
  };

  FunctionPointerType f_ptr = [] (void* data)
  {
    deleter f{static_cast<node_t*>(data)};
    move(get<1>(*f.ptr))();
  };
  return make_c_callback(f_ptr, allocate_unique<node_t>(alloc, alloc, forward<F>(f)));
}

////////////////////////////////////////////////////////////////////////////////
// package_task_as_c_callback_impl
// specialization for callables which can be stored inside a void*
//

template<class FunctionPointerType, class F>
auto schedulers::detail::package_task_as_c_callback_impl(std::true_type /* valid types */,
                                                         F&& f,
                                                         dont_care_t,
                                                         std::true_type /* can elide alloc */)
{
  using function_t = std::decay_t<F>;

  static_assert(sizeof(function_t) <= sizeof(void*), "something went horribly wrong");
  static_assert(std::is_trivially_copyable<function_t>(), "something went horribly wrong");

  struct data_t
  {
    auto get() const noexcept { return ptr; }
    auto release() const noexcept { return ptr; }
    void* ptr;
  };

  // This union is only used to determine size/alignment.
  // Not using aligned_union because it's not guaranteed to be a char array.
  union converter_t
  {
    function_t f;
    void* p;
  };

  FunctionPointerType f_ptr = [] (void* data)
  {
    // We have to ensure the object we call is properly aligned.
    // Therefore we cannot just reinterpret "&data".
    alignas(converter_t) char converter[sizeof(converter_t)];
    memcpy(converter, &data, sizeof(data));
    auto* f = reinterpret_cast<function_t*>(converter);
    move(*f)();
  };

  alignas(converter_t) char converter[sizeof(converter_t)];
  memcpy(converter, &f, sizeof(f));
  return make_c_callback(f_ptr, data_t{*reinterpret_cast<void**>(converter)});
}

////////////////////////////////////////////////////////////////////////////////
// package_task_as_c_callback_impl
// specialization for std::reference_wrapper
//

template<class FunctionPointerType, class T>
auto schedulers::detail::package_task_as_c_callback_impl(std::true_type /* valid types */,
                                                         std::reference_wrapper<T> f,
                                                         dont_care_t /* alloc */,
                                                         dont_care_t /* can elide alloc */)
{
  struct data_t
  {
    auto get() const noexcept { return ptr; }
    auto release() const noexcept { return ptr; }
    void* ptr;
  };

  FunctionPointerType f_ptr = [] (void* data)
  {
    (*static_cast<T*>(data))();
  };
  void* data = const_cast<std::remove_cv_t<T>*>(std::addressof(f.get()));

  return make_c_callback(f_ptr, data_t{data});
}

////////////////////////////////////////////////////////////////////////////////
// detail::c_callback
//

template<class FunctionPointerType, class DataOwner>
class schedulers::detail::c_callback
{
public:
  auto get() const noexcept { return c_callback_data<FunctionPointerType>{_f, _data.get()}; }
  auto release() noexcept { return c_callback_data<FunctionPointerType>{_f, _data.release()}; }
  void operator()() { _f(_data.release()); }

protected:
  c_callback(FunctionPointerType f, DataOwner data)
  : _f(f), _data(move(data))
  { }

private:
  FunctionPointerType _f;
  DataOwner _data;
};

template<class FunctionPointerType, class DataOwner>
auto schedulers::detail::make_c_callback(FunctionPointerType f_ptr, DataOwner owner)
{
  using result_t = c_callback<FunctionPointerType, DataOwner>;

  struct constructible_result_t : result_t
  {
    constructible_result_t(FunctionPointerType f_ptr, DataOwner data)
    : result_t(f_ptr, move(data))
    { }
  };

  // Slice away the subobject with no public constructor
  return result_t{constructible_result_t{f_ptr, move(owner)}};
}
