# schedulers
*schedulers* is a library to make cross-platform development of multithreaded C++ code easier.

## Main Features
* Run tasks on backgorund threads
* Queue tasks on the UI thread (currently only Apple and Android)
* Automatically exploit default system-provided thread pools (Apple's GCD and Windows default thread pool)
* Use a *SharedNativeThreadPool* for sharing threads between Java and C++ for lower overheads (includes [Djinni](https://github.com/dropbox/djinni/) integration)

### Non-Features
The library does not attempt to be a panacea. It does exactly what it says on the title: provide schedulers to run code asynchronously. As such it is a very small and focused library. There are no fancy algorithms, futures, task systems, or similar. Those can all be implemented on top of it. My primary motivation for writing this library was the frustration with large "async" libraries out there which try to solve every problem under the sun, but in doing so often force a certain design up your throat and most of the time lack any integration capabilities with other similar libraries. They are often themselves "library unfriendly" because they are designed with the assumption that they serve as the only monolithic multithreading framework in your entire code base, including all dependencies.

### It's not Header-only?!
Unfortunately not. There is one file you have to compile (`src/schedulers.cpp)`, unless you are also using Java and Android in which case the number increases by one for each (`src/schedulers-jni.cpp` and `src/schedulers-android.cpp`). The `src/java` directory also contains Java files which must be included in your project in the Java/Android case or the integration with Java will not work.

## Getting Started
The project contains a `CMakeLists.txt` with the `schedulers` library target. All you have to do to use it in your own CMake project is link against the library and set these options if required:
- `SCHEDULERS_FOR_JAVA` enables the `java_shared_native_pool` scheduler. It can be used from C++ and Java (via the `java.util.concurrent.Executor` interface) alike. This scheduler requires the [dropbox/djinni](https://github.com/dropbox/djinni) library, specifically the pull request [dropbox/djinni#248](https://github.com/dropbox/djinni/pull/248). You also have to compile Java support code located at `src/java/**/*.java`. I hope to be able to wrap this all up with CMake (and gradle for Android) so the manual steps are not necessary.

### The Interface of a Scheduler
Schedulers in this library have a very simple interface: they are simple function objects.
```cpp
schedulers::thread_pool s;
s([] { /* your code goes here */ });
```
In order to not include unnecessary indirections and reference counting schedulers may not be copy- or move-constructible. In order to pass a scheduler as parameter to a higher-order function just use `std::ref`.
```cpp
template<class Scheduler, class F>
void my_async(Scheduler s, F f) {
  s(f);
}
schedulers::thread_pool pool;
my_async(std::ref(pool), [] { ... });
```
Note that this may change in the future depending on usability feedback.

### "Main Thread" Schedulers
These schedulers provide access to an application's "main" or "UI" thread. They are used to notify about the progress or completion of background tasks since it is usually forbidden to touch UI elements from non-UI threads. The infrastructure that makes these schedulers work is provided by the operating system or UI framework and requires that the main thread is running in some sort of 'event loop'.

#### libdispatch_main
This scheduler wraps the "main queue" of Apple's GCD that is provided by the system and runs all the UI code by default. If your application does not have a UI you have to call `libdispatch_main()` on your main thread to start the event loop.

#### android_main_looper
Uses the `ALooper` API of Android to schedule tasks on your application's main event loop. This only works if your application is rooted in an activity.

#### Others
More schedulers will be added over time.

### Background Work with default_scheduler
This is the default scheduler to use when doing stuff "in the background". It automatically adjusts to the system you are on at compile time and should be your go-to scheduler for everything that is not the main/UI thread. It is implemented in terms of publicly available schedulers in the library, so you could bypass it and use the concrete schedulers instead, but that should only be necessary if your system is not available in the library and you have to provide your own scheduler (pull requests welcome). Below is a table showing what this scheduler maps to on various environments.

| Platform | Concrete Class | Remarks |
|----------|----------------|---------|
| Apple | `libdispatch_global_default` | Uses the GCD global queue with "default" priority. |
| Win32 | `win32_default_pool` | Uses the default Windows thread pool provided for every application and shared with the Concurrency Runtime. |
| Java (+Android) | `java_shared_native_pool` | A thread pool which is configured in such a way that it can run C++ and Java tasks alike. It can be converted to a `java.util.concurrent.Executor` (see the Djinni-support section). Only available if the `SCHEDULERS_FOR_JAVA` CMake option is enabled. |
| Emscripten | `emscripten_async` | Delegates all calls to `emscripten_async_call()`. |
| otherwise | `thread_pool` | A traditional thread pool. |

As noted before, you usually don't have to touch the mentioned schedulers directly but simply use `default_scheduler` as-is.

### Other Schedulers
More to come...

### Determining Scheduler Availability
Not every scheduler is available on every system. For example `libdispatch_main` is a scheduler that cannot be used on non-Apple platforms. Traditionally if a library has a class that may only be used on certain systems you have to resolve to `#ifdef` hacking to make sure you don't access the type or function on other platforms. But that is ridiculous: the library already had to go through preprocessor madness to determine whether to make a symbol available or not, so why should you do the same again? The answer is: you shouldn't!

That's why every scheduler in the library has a nested `static constexpr` value called `available` which is either of type `std::true_typ` or `std::false_type`. This allows you to use compile-time evaluation or tag dispatching for scheduler selection instead of ugly preprocessor hacks (those were already done for you by the library). So imagine you are developing for iOS and Android. In order to determine whether to use `libdispatch_main` or `android_main_looper` simply use compile-time evaluation:
```cpp
namespace my_app
{
  using main_thread_scheduler_t = std::conditional_t<libdispatch_main::available(),
                                                     libdispatch_main,
                                                     android_main_looper>;
  const main_thread_scheduler_t main_thread_scheduler{};
}

```
It is *not an error* to refer to, or instantiate, an unavailable scheduler as long as you don't attempt to actually use it for scheduling tasks. This means the compiler will not complain if you use these types in an unevaluated context or in bodies of functions not instantiated for the target platform.

In a hypothetical scenario, involving more options, tag dispatching is your friend:
```cpp
scheduler_1 make_scheduler(std::true_type  /* has scheduler 1 */,
                           std::false_type /* has scheduler 2 */,
                           std::false_type /* some condition */) { return ...; }
scheduler_2 make_scheduler(std::true_type  /* has scheduler 1 */,
                           std::false_type /* has scheduler 2 */,
                           std::true_type  /* some condition */) { return ...; }
scheduler_3 make_scheduler(std::false_type /* has scheduler 1 */,
                           std::true_type  /* has scheduler 2 */,
                           ...             /* some condition */) { return ...; }
const auto scheduler = make_scheduler(scheduler_1::available,
                                      scheduler_2::available,
                                      some_constexpr_condition());
```
The more elaborate tag-dispatching mechanism gives you more control over determining what to do, all of that without preprocessor nonsense. This way you don't have to care about the exact preprocessor conditions required to determine a scheduler's availability as that information has already been determined by the library. All you have left to do here is figure out what conditions lead to what type to use. Something like this might be necessary if your applications supports several variaties of UI frameworks on the same platform and you need to integrate with the correct one depending on other build settings.

### Djinni Support
You will find a file named `schedulers.yaml` in the `djinni/` folder that can be used with the `@extern` command in Djinni IDL files. This allows you to convert the `default_scheduler` on iOS and Java/Android to a Objective-C or Java object.

However, because the C++ `default_scheduler` uses *libdispatch* there is not much point in converting it to Objective-C.

But if your application also has a Java side then you can convert `default_scheduler` to a `de.knejp.schedulers.SharedNativeThreadPoolExecutor` (which implements `java.util.concurrent.Executor`) and run Java tasks on the same thread pool as the C++ code. This avoids the creation of unnecessary threads for Java *and* C++ which often happens in other Java/C++ multilanguage projects.

Even though the lack of necessity to convert it to Objective-C the ability is implemented so you can use the same Djinni interface description on all platforms, you simply discard the value in Objective-C.

Due to technical limitations the conversion is only possible *from* C++ *to* Java/Objective-C and not back. This means, in Djinni terms, that you can only use the `schedulers_default_scheduler` type from the YAML file as return value form a `interface +c` method and nothing else.

Here is how an example interface would look like:
```
@extern "schedulers.yaml"
interface cpp_core +c {
  static create() : cpp_core;
  get_scheduler() : schedulers_default_scheduler;
}
```
This creates these two interfaces in Objective-C and Java:
```objc
@interface XYCppCore : NSObject {
+(XYCppCore*) create;
-(dispatch_queue_t)getScheduler;
}
```
```java
class CppCore {
  static CppCore create();
  de.knejp.schedulers.SharedNativeThreadPoolExecutor getScheduler();
}
```
And your implementation:
```cpp
class my_cpp_core : public cpp_core {
  schedulers::default_scheduler get_scheduler() override {
    return the_default_scheduler;
  }
};
```
In your Objective-C code you can simply chose not to call the `getScheduler()` method, whereas in Java it allows you to share resources with C++ by executing Java `Runnable` objects on the same thread pool.

#### A Note on SharedNativeThreadPoolExecutor
It is important to note that the C++ and Java objects share ownership of the thread pool, so even if the C++ side is destroyed the thread pool continues running and vice versa. Because the threads in the pool are not *daemon* threads they will prevent your application form exiting if the Java object is not garbage collected in time. This is done to guarantee proper cleanup of C++ objects by runnign destructors for queued up tasks at a well-known point in time. To avoid this problem call `SharedNativeThreadPoolExecutor.shutdown()` which will force the Java object to give up its ownership of the pool and, if it is the last reference, stops all pool threads, thus no longer keeping your application alive.
