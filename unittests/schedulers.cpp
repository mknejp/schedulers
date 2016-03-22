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
