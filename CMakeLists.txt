cmake_minimum_required(VERSION 3.0.0)

project(schedulers)

enable_testing()

include_directories(include)

set(SCHEDULERS_LIB_SRC
  ${CMAKE_CURRENT_LIST_DIR}/include/schedulers/package_task_as_c_callback.hpp
  ${CMAKE_CURRENT_LIST_DIR}/include/schedulers/schedulers.hpp
  ${CMAKE_CURRENT_LIST_DIR}/include/schedulers/utils.hpp

  ${CMAKE_CURRENT_LIST_DIR}/src/schedulers.cpp
)

add_library(schedulers ${SCHEDULERS_LIB_SRC})

set_property(TARGET schedulers PROPERTY CXX_STANDARD 14)

add_subdirectory(test)
add_subdirectory(unittests)
