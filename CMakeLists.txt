cmake_minimum_required(VERSION 3.6.0)

project(schedulers)

option(SCHEDULERS_FOR_JAVA "Add support for Java Executors. Requires the Djinni support library." OFF)
option(SCHEDULERS_FOR_ANDROID "Add support for Android's Loopers." OFF)

include(GNUInstallDirs)

###############################################################################
# sources
#

add_library(schedulers
  "include/schedulers/djinni/schedulers-jni.hpp"
  "include/schedulers/djinni/schedulers-objcpp.hpp"
  "include/schedulers/package_task_as_c_callback.hpp"
  "include/schedulers/schedulers.hpp"

  "src/schedulers.cpp"
)

if(SCHEDULERS_FOR_ANDROID)
  target_sources(schedulers PRIVATE "src/schedulers-android.cpp")
  target_compile_definitions(schedulers PUBLIC SCHEDULERS_FOR_ANDROID)
endif()

if(SCHEDULERS_FOR_JAVA)
  target_sources(schedulers PRIVATE "src/schedulers-jni.cpp")
  target_compile_definitions(schedulers PUBLIC SCHEDULERS_FOR_JAVA)
endif()

source_group("" FILES
  "include/schedulers/package_task_as_c_callback.hpp"
  "include/schedulers/schedulers.hpp"
  "src/schedulers-android.cpp"
  "src/schedulers-jni.cpp"
  "src/schedulers.cpp"
)
source_group("djinni" FILES
  "include/schedulers/djinni/schedulers-objcpp.hpp"
  "include/schedulers/djinni/schedulers-jni.hpp"
)
###############################################################################
# configuration
#

set_target_properties(schedulers PROPERTIES
  CXX_STANDARD 14
  CXX_STANDARD_REQUIRED true
  CXX_EXTENSIONS false
)

target_include_directories(schedulers
  PRIVATE
  "src"
  PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

###############################################################################
# dependencies
#

if(SCHEDULERS_FOR_JAVA)
  find_package(JNI REQUIRED QUIET)
  find_package(djinni REQUIRED QUIET)

  target_include_directories(schedulers PUBLIC ${JNI_INCLUDE_DIRS})
  target_link_libraries(schedulers PUBLIC ${JNI_LIBRARIES} djinni::djinni)
endif()

###############################################################################
# installation
#

install(TARGETS schedulers
  RUNTIME   DESTINATION "${CMAKE_INSTALL_BINDIR}"
  ARCHIVE   DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY   DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  FRAMEWORK DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(DIRECTORY "include/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

###############################################################################
# tests
#

enable_testing()

add_subdirectory(test)
add_subdirectory(unittests)
