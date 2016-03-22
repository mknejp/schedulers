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

#define SCHEDULERS_FOR_ANDROID
#include "schedulers/schedulers.hpp"
#include <android/looper.h>
#include <fcntl.h>
#include <cerrno>

using namespace schedulers;

namespace
{
  auto looper_callback(int fd, int events, void* data) -> int
  {
    std::int8_t byte;
		// We cannot loop here otherwise we could get into infinite loops if the callback enqueues a new callback on the main thread. However we know there are as many bytes waiting in the pipe as there are scheduled callbacks so this callback will be called again for each.
		if(read(fd, &byte, sizeof(byte)) > 0)
    {
      std::function<void()> f;
      if(main_thread_task_queue::get().try_pop(f))
      {
        f();
      }
    }
    return 1; // Continue receiving events
  }
}

android_main_looper::android_main_looper()
: _looper(ALooper_forThread())
{
  assert(_looper && "no android looper in current thread?");

  int fd[2];
  if(pipe2(fd, O_NONBLOCK | O_CLOEXEC) == 0)
  {
    _read_fd = fd[0];
    _write_fd = fd[1];
  }
  else
  {
    throw std::system_error{errno, std::system_category(), "Unable to create pipe for ALooper."};
  }

  if(ALooper_addFd(_looper, _read_fd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, looper_callback, nullptr) != 1)
  {
    close(_read_fd);
    close(_write_fd);
    throw std::system_error{errno, std::system_category(), "Unable to add pipe to ALooper."};
  }
}

android_main_looper::~android_main_looper()
{
  ALooper_removeFd(_looper, _read_fd);
  close(_read_fd);
  close(_write_fd);
  main_thread_task_queue::get().clear();
}

auto android_main_looper::post() const -> void
{
  std::int8_t byte = 0;
  if(write(_write_fd, &byte, sizeof(byte)) < sizeof(byte))
  {
    throw std::system_error{errno, std::system_category(), "ALooper buffer overflow."};
  }
}
