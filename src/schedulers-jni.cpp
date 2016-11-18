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

#include "schedulers/djinni/schedulers-jni.hpp"
#include <jni.h>
#include <cstdlib>

using namespace schedulers;

static_assert(sizeof(jlong) >= sizeof(void(*)(void*)), "must be able to fit function pointer into jlong");

namespace
{
  struct java_lang_Runnable
  {
    const djinni::GlobalRef<jclass> clazz{djinni::jniFindClass("java/lang/Runnable")};
    const jmethodID method_run{djinni::jniGetMethodID(clazz.get(), "run", "()V")};
  };
  struct de_knejp_schedulers_SharedNativeThreadPoolExecutor
  {
    const djinni::GlobalRef<jclass> clazz{djinni::jniFindClass("de/knejp/schedulers/SharedNativeThreadPoolExecutor")};
  };
  struct de_knejp_schedulers_NativeWorkerCallstack
  {
    const djinni::GlobalRef<jclass> clazz{djinni::jniFindClass("de/knejp/schedulers/NativeWorkerCallstack")};
    const jmethodID method_anchor{djinni::jniGetStaticMethodID(clazz.get(),
                                                               "anchor",
                                                               "(JJ)V")};
  };
}

////////////////////////////////////////////////////////////////////////////////
// java_shared_native_pool
//

jni::default_scheduler::default_scheduler()
: JniInterface("de/knejp/schedulers/SharedNativeThreadPoolExecutor")
{ }

jni::default_scheduler::~default_scheduler() = default;

namespace
{
  // Sometimes in jni.h the signature is
  // AttachCurrentThread(void**, void*) and sometimes it's
  // AttachCurrentThread(JNIEnv**, void*).
  template<class Env>
  JNIEnv* attachCurrentThread(jint(JavaVM::*f)(Env**, void*), JavaVM* jvm, void* args)
  {
    Env *e;
    return ((jvm->*f)(&e, args) == JNI_OK ? static_cast<JNIEnv*>(e) : nullptr);
  }

  auto make_java_attached_thread = [] (int idx, const auto& queue, auto&& f)
  {
    // Ensure the classes are initialized on a thread with a class loader (i.e. main thread)
    djinni::JniClass<java_lang_Runnable>::get();
    djinni::JniClass<de_knejp_schedulers_SharedNativeThreadPoolExecutor>::get();
    djinni::JniClass<de_knejp_schedulers_NativeWorkerCallstack>::get();
    djinni::JniClass<jni::default_scheduler>::get();

    JavaVM* jvm;
    if(djinni::jniGetThreadEnv()->GetJavaVM(&jvm) != JNI_OK)
    {
      throw std::runtime_error{"Could not retrieve current JVM."};
    }
    return std::thread{[f = std::forward<decltype(f)>(f), jvm, idx] () mutable
      {
        auto name = "SharedNativeWorker#" + std::to_string(idx);
        auto attrs = JavaVMAttachArgs
        {
          JNI_VERSION_1_6,
          const_cast<char*>(name.c_str()),
          nullptr,
        };
        auto env = attachCurrentThread(&JavaVM::AttachCurrentThread, jvm, &attrs);
        if(!env)
        {
          // Since we're on a custom thread this will terminate the program but hopefully at least display an error message somewhere.
          throw std::runtime_error{"Unable to attach JVM to native thread."};
        }

        struct detach_at_scope_exit
        {
          ~detach_at_scope_exit()
          {
            jvm->DetachCurrentThread();
          }
          JavaVM* jvm;
        };

        detach_at_scope_exit detach_at_scope_exit{jvm};

        // Transfer a pointer to f through a call into Java so we have the app's class loader installed in this thread before we try to do any class lookup via JNI.
        void(*callback)(void*) = [] (void* data)
        {
          (*static_cast<decltype(f)*>(data))();
        };

        const auto& data = djinni::JniClass<de_knejp_schedulers_NativeWorkerCallstack>::get();
        env->CallStaticVoidMethod(data.clazz.get(),
                                  data.method_anchor,
                                  reinterpret_cast<jlong>(callback),
                                  reinterpret_cast<jlong>(&f));
      }};
  };
}

CJNIEXPORT void JNICALL Java_de_knejp_schedulers_NativeWorkerCallstack_run(JNIEnv* jniEnv,
                                                                           jobject /*this*/,
                                                                           jlong j_callback,
                                                                           jlong j_data)
{
  try
  {
    DJINNI_FUNCTION_PROLOGUE0(jniEnv);

    auto callback = reinterpret_cast<void(*)(void*)>(j_callback);
    auto data = reinterpret_cast<void*>(j_data);
    callback(data);

  }
  JNI_TRANSLATE_EXCEPTIONS_RETURN(jniEnv, )
}

java_shared_native_pool::java_shared_native_pool(int num_threads)
: _pool(std::make_shared<pool_t>(make_java_attached_thread, num_threads))
{
}

auto jni::default_scheduler::fromCpp(JNIEnv* jniEnv, const CppType& c)
-> djinni::LocalRef<JniType>
{
  const auto& pool = static_cast<const java_shared_native_pool&>(c);
  return {jniEnv, djinni::JniClass<jni::default_scheduler>::get()._toJava(jniEnv, pool._pool)};
}

CJNIEXPORT void JNICALL Java_de_knejp_schedulers_SharedNativeThreadPoolExecutor_nativeShutdown(JNIEnv* jniEnv,
                                                                                               jobject /*this*/,
                                                                                               jlong nativeRef)
{
  try
  {
    DJINNI_FUNCTION_PROLOGUE1(jniEnv, nativeRef);
    delete reinterpret_cast<djinni::CppProxyHandle<thread_pool>*>(nativeRef);
  }
  JNI_TRANSLATE_EXCEPTIONS_RETURN(jniEnv, )
}

CJNIEXPORT void JNICALL Java_de_knejp_schedulers_SharedNativeThreadPoolExecutor_native_1execute(JNIEnv* jniEnv,
                                                                                                jobject /*this*/,
                                                                                                jlong nativeRef,
                                                                                                jobject j_r)
{
  try
  {
    DJINNI_FUNCTION_PROLOGUE1(jniEnv, nativeRef);
    const auto& ref = djinni::objectFromHandleAddress<thread_pool>(nativeRef);

    // Don't bother the proxy cache for runnables as every task needs its own std::function anyway
    class F
    {
    public:
      F(JNIEnv* jniEnv, jobject runnable) noexcept
      : _runnable(jniEnv->NewGlobalRef(runnable))
      { }
      F(const F& other) noexcept
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

    (*ref)(F{jniEnv, j_r});

  }
  JNI_TRANSLATE_EXCEPTIONS_RETURN(jniEnv, )
}
