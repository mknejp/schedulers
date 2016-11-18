add_executable(
  schedulers-test

  main.cpp
  package_task_as_c_callback.cpp
  schedulers.cpp
  work_item.cpp

  test_tools.hpp
)

set_target_properties(schedulers-test PROPERTIES
  CXX_STANDARD 14
  CXX_STANDARD_REQUIRED true
  CXX_EXTENSIONS false
)

target_link_libraries(schedulers-test schedulers)
add_test(NAME schedulers-test COMMAND schedulers-test)
