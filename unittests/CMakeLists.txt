add_executable(
  schedulers-test

  main.cpp
  package_task_as_c_callback.cpp
  schedulers.cpp
  )

set_property(TARGET schedulers-test PROPERTY CXX_STANDARD 14)

target_link_libraries(schedulers-test schedulers)
