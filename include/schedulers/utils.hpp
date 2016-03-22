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
#include <memory>
#include <type_traits>

namespace schedulers
{
  template<class...>
  using void_t = void;

  template<bool B>
  using bool_constant = std::integral_constant<bool, B>;
  
  // Special tag that matches any parameter
  struct dont_care_t
  {
    template<class... Args>
    dont_care_t(Args&&...) { }
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
}

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
  auto temp = std::unique_ptr<T, decltype(temp_deleter)>{rebound_traits::allocate(real_alloc, 1), std::move(temp_deleter)};
  rebound_traits::construct(real_alloc, temp.get(), std::forward<Args>(args)...);

  auto result = std::unique_ptr<T, decltype(deleter)>(temp.release(), std::move(deleter));
  return result;
}

template<class T, class X, class... Args>
auto schedulers::allocate_unique(const std::allocator<X>& alloc, Args&&... args)
{
  return std::make_unique<T>(std::forward<Args>(args)...);
}
