cmake_minimum_required(VERSION 3.0.0)

project(schedulers)

option(SCHEDULERS_FOR_JAVA "Add support for Java Executors. Requires the Djinni support library." OFF)
option(SCHEDULERS_FOR_ANDROID "Add support for Android's Loopers." OFF)

enable_testing()

include_directories(include)

set(SCHEDULERS_LIB_SRC
  ${CMAKE_CURRENT_LIST_DIR}/include/schedulers/package_task_as_c_callback.hpp
  ${CMAKE_CURRENT_LIST_DIR}/include/schedulers/schedulers.hpp
  ${CMAKE_CURRENT_LIST_DIR}/include/schedulers/utils.hpp

  ${CMAKE_CURRENT_LIST_DIR}/src/schedulers.cpp
)

add_library(schedulers ${SCHEDULERS_LIB_SRC})

if(SCHEDULERS_FOR_ANDROID)
  target_sources(schedulers PRIVATE "src/schedulers-android.cpp")
  target_compile_definitions(schedulers PUBLIC SCHEDULERS_FOR_ANDROID)
endif()

if(SCHEDULERS_FOR_JAVA)
  target_sources(schedulers PRIVATE "src/schedulers-jni.cpp")
  target_compile_definitions(schedulers PUBLIC SCHEDULERS_FOR_JAVA)
endif()

if(SCHEDULERS_FOR_JAVA)
  find_package(JNI REQUIRED QUIET)
  find_package(djinni REQUIRED QUIET)

  target_include_directories(schedulers PUBLIC ${JNI_INCLUDE_DIRS})
  target_link_libraries(schedulers PUBLIC ${JNI_LIBRARIES} djinni::djinni)
endif()

set_property(TARGET schedulers PROPERTY CXX_STANDARD 14)

add_subdirectory(test)
add_subdirectory(unittests)
