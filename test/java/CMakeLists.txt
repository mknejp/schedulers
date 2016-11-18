if(NOT SCHEDULERS_FOR_JAVA)
  return()
endif()

find_package(Java REQUIRED QUIET)
include(UseJava)

# Native lib

add_library(schedulers-java-test
  SHARED
  ${CMAKE_CURRENT_LIST_DIR}/java-test.cpp
)

set_target_properties(schedulers-java-test PROPERTIES
  CXX_STANDARD 14
  CXX_STANDARD_REQUIRED true
  CXX_EXTENSIONS false
)

target_link_libraries(schedulers-java-test PRIVATE djinni::djinni schedulers)

# Java test runner

set(CMAKE_JNI_TARGET TRUE)
add_jar(schedulers-java-test-jar
  ${CMAKE_CURRENT_LIST_DIR}/de/knejp/schedulerstest/Test.java
  ${CMAKE_CURRENT_LIST_DIR}/de/knejp/schedulerstest/SharedNativeThreadPoolTest.java
  ${CMAKE_CURRENT_LIST_DIR}/../../src/java/de/knejp/schedulers/Schedulers.java
  ${CMAKE_CURRENT_LIST_DIR}/../../src/java/de/knejp/schedulers/SharedNativeThreadPoolExecutor.java
)

get_target_property(test1_jar_file schedulers-java-test-jar JAR_FILE)

add_dependencies(schedulers-java-test-jar schedulers-java-test)

add_test(NAME schedulers-java-test 
		 COMMAND ${Java_JAVA_EXECUTABLE} -cp ".:${test1_jar_file}" -Djava.library.path=$<TARGET_FILE_DIR:schedulers-java-test> de.knejp.schedulerstest.Test
)
