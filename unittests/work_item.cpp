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

#include "schedulers/utils.hpp"
#include "test_tools.hpp"

using schedulers::detail::work_item;

SCENARIO("Empty work_item bejavior", "[work_item]")
{
  GIVEN("a default constructed work_item")
  {
    work_item wi;
    THEN("it converts to false")
    {
      REQUIRE(bool(wi) == false);
    }
  }
}

SCENARIO("Non-empty work_item bejavior", "[work_item]")
{
  bool b = false;
  GIVEN("a work_item constructed from a function")
  {
    work_item wi(std::allocator_arg, std::allocator<char>(), [&b] { b = true; } );
    THEN("it converts to true")
    {
      REQUIRE(bool(wi) == true);
    }
    WHEN("calling the work_item")
    {
      std::move(wi)();
      THEN("it calls the stored function")
      {
        REQUIRE(b == true);
      }
    }
  }

  GIVEN("a work_item with a rvalue-ref only call operator")
  {
    // This is really a compile-time only test
    struct rvalue_ref_callable
    {
      auto operator()() && -> void { }
    };
    work_item wi{std::allocator_arg, std::allocator<char>(), rvalue_ref_callable{}};
    THEN("it is callable")
    {
      std::move(wi)();
    }
  }
}

SCENARIO("work_item small-buffer-optimization selection strategy", "[work_item]")
{
  GIVEN("a work_item constructed from a small nothrow-move-constructible function")
  {
    size_t bytes = 0;
    int instances = 0;
    work_item wi(std::allocator_arg, tracking_allocator<>{&bytes}, tracked_callable{&instances});

    THEN("small-object-optimization is enabled")
    {
      REQUIRE(instances == 1);
      REQUIRE(bytes == 0);
    }
  }

  GIVEN("a work_item constructed from a small throwing-move-constructible function")
  {
    struct throwing_move_constructible : tracked_callable
    {
      using tracked_callable::tracked_callable;
      throwing_move_constructible(const throwing_move_constructible& other) noexcept(false)
      : tracked_callable(other)
      { }
    };
    size_t bytes = 0;
    int instances = 0;
    work_item wi(std::allocator_arg, tracking_allocator<>{&bytes}, throwing_move_constructible{&instances});

    THEN("small-object-optimization is disabled")
    {
      REQUIRE(instances == 1);
      REQUIRE(bytes > 0);
    }
  }

  GIVEN("a work_item constructed from a large function")
  {
    struct large_function : tracked_callable
    {
      using tracked_callable::tracked_callable;

      int x[100];
    };
    size_t bytes = 0;
    int instances = 0;
    work_item wi(std::allocator_arg, tracking_allocator<>{&bytes}, large_function{&instances});

    THEN("small-object-optimization is disabled")
    {
      REQUIRE(instances == 1);
      REQUIRE(bytes > 0);
    }
  }
}

SCENARIO("work_item behavior with small-object-optimization active", "[work_item]")
{
  size_t bytes = 0;
  int instances = 0;
  GIVEN("a work_item with small-buffer-optimization active")
  {
    work_item wi(std::allocator_arg, tracking_allocator<>{&bytes}, tracked_callable{&instances});

    THEN("no allocation is performed")
    {
      REQUIRE(instances == 1);
      REQUIRE(bytes == 0);
    }

    WHEN("move-constructing a new work_item from it")
    {
      work_item wi2{std::move(wi)};

      THEN("no allocation is performed")
      {
        REQUIRE(bytes == 0);
      }
      THEN("the function object is destructibly moved")
      {
        REQUIRE(instances == 1);
      }
      THEN("the new instance converts to true")
      {
        REQUIRE(bool(wi2) == true);
      }
      THEN("the previous instance converts to false")
      {
        REQUIRE(bool(wi) == false);
      }
    }

    WHEN("move-assigning to an existing work_item")
    {
      work_item wi2;
      wi2 = std::move(wi);

      THEN("no allocation is performed")
      {
        REQUIRE(bytes == 0);
      }
      THEN("the function object is destructibly moved")
      {
        REQUIRE(instances == 1);
      }
      THEN("the assigned-to instance converts to true")
      {
        REQUIRE(bool(wi2) == true);
      }
      THEN("the assigned-from instance converts to false")
      {
        REQUIRE(bool(wi) == false);
      }
    }
  }
  REQUIRE(bytes == 0);
  REQUIRE(instances == 0);
}

SCENARIO("work_item behavior with small-buffer-optimization inactive", "[work_item]")
{
  size_t bytes = 0;
  int instances = 0;

  GIVEN("a work_item with small-buffer-optimization inactive")
  {
    struct large_function : tracked_callable
    {
      using tracked_callable::tracked_callable;

      int x[100];
    };
    work_item wi(std::allocator_arg, realloc_forbidden_allocator<>{&bytes}, large_function{&instances});

    THEN("allocation is performed")
    {
      REQUIRE(instances == 1);
      REQUIRE(bytes > 0);
    }

    WHEN("move-constructing a new work_item from it")
    {
      work_item wi2{std::move(wi)};

      THEN("the function object is not copied")
      {
        REQUIRE(instances == 1);
      }
      THEN("the new instance converts to true")
      {
        REQUIRE(bool(wi2) == true);
      }
      THEN("the previous instance converts to false")
      {
        REQUIRE(bool(wi) == false);
      }
    }

    WHEN("move-assigning to an existing work_item")
    {
      work_item wi2;
      wi2 = std::move(wi);

      THEN("the function object is not copied")
      {
        REQUIRE(instances == 1);
      }
      THEN("the assigned-to instance converts to true")
      {
        REQUIRE(bool(wi2) == true);
      }
      THEN("the assigned-from instance converts to false")
      {
        REQUIRE(bool(wi) == false);
      }
    }
  }
  REQUIRE(bytes == 0);
  REQUIRE(instances == 0);
}
