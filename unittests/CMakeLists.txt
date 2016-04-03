add_executable(
  schedulers-test

  main.cpp
  package_task_as_c_callback.cpp
  schedulers.cpp
  work_item.cpp

  test_tools.hpp
  )

set_property(TARGET schedulers-test PROPERTY CXX_STANDARD 14)

target_link_libraries(schedulers-test schedulers)
