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

namespace
{
  std::unique_ptr<schedulers::android_main_looper> theLooper;
}

CJNIEXPORT void JNICALL Java_de_knejp_schedulerstest_MainActivity_createMainLooperScheduler(JNIEnv *jniEnv, jobject /*this*/)
{
  theLooper = std::make_unique<schedulers::android_main_looper>();
}

CJNIEXPORT void JNICALL Java_de_knejp_schedulerstest_MainActivity_destroyMainLooperScheduler(JNIEnv *jniEnv, jobject /*this*/)
{
  theLooper = nullptr;
}

CJNIEXPORT void JNICALL Java_de_knejp_schedulerstest_MainActivity_executeOnMainLooperScheduler(JNIEnv *jniEnv, jobject /*this*/, jobject j_r)
{
  struct java_lang_Runnable
  {
    const djinni::GlobalRef<jclass> clazz{djinni::jniFindClass("java/lang/Runnable")};
    const jmethodID method_run{djinni::jniGetMethodID(clazz.get(), "run", "()V")};
  };

  try {
    DJINNI_FUNCTION_PROLOGUE1(jniEnv, nativeRef);

    // Don't bother the proxy cache for runnables as every task needs its own std::function anyway
    class F
    {
    public:
      F(JNIEnv* jniEnv, jobject runnable)
          : _runnable(jniEnv->NewGlobalRef(runnable))
      { }
      F(const F& other)
      {
        auto jniEnv = djinni::jniGetThreadEnv();
        _runnable = other._runnable ? jniEnv->NewGlobalRef(other._runnable) : nullptr;
      }
      F(F&& other) noexcept
      {
        _runnable = other._runnable;
        other._runnable = nullptr;
      }
      ~F()
      {
        if(_runnable)
        {
          djinni::jniGetThreadEnv()->DeleteGlobalRef(_runnable);
        }
      }

      void operator()() const
      {
        assert(_runnable && "lost runnable reference");
        auto& data = djinni::JniClass<java_lang_Runnable>::get();
        auto* env = djinni::jniGetThreadEnv();
        env->CallVoidMethod(_runnable, data.method_run);
        djinni::jniExceptionCheck(env);
      }

    private:
      // Can't use djinni::GlobalRef<jobject> because std::function requires CopyConstructible
      jobject _runnable;
    };

    (*theLooper)(F{jniEnv, j_r});

  } JNI_TRANSLATE_EXCEPTIONS_RETURN(jniEnv, )
}
