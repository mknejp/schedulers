find_package(Java REQUIRED)
include(UseJava)

find_package(JNI REQUIRED)
if (NOT JNI_FOUND)
  message(FATAL_ERROR
		  "Could not find JNI. Did you install a JDK? Set $JAVA_HOME to override"
		  )
endif()

# Native lib

add_library(schedulers-java-test
			SHARED
			${SCHEDULERS_LIB_SRC} ${CMAKE_CURRENT_LIST_DIR}/../../src/schedulers-jni.cpp
			${SCHEDULERS_LIB_SRC} ${CMAKE_CURRENT_LIST_DIR}/../../include/schedulers/schedulers-jni.hpp
			${CMAKE_CURRENT_LIST_DIR}/../../utils/djinni/support-lib/jni/djinni_main.cpp
			${CMAKE_CURRENT_LIST_DIR}/../../utils/djinni/support-lib/jni/djinni_support.cpp
			${CMAKE_CURRENT_LIST_DIR}/java-test.cpp
			)
include_directories(schedulers-java
					${CMAKE_CURRENT_LIST_DIR}/../../include
					${CMAKE_CURRENT_LIST_DIR}/../../utils/djinni/support-lib/jni
					${JNI_INCLUDE_DIRS}
  					)
set_property(TARGET schedulers-java-test PROPERTY CXX_STANDARD 14)
target_link_libraries(schedulers-java-test ${JNI_LIBRARIES})

get_target_property(test_native_library schedulers-java-test OUTPUT_NAME)

# Java test runner

set(CMAKE_JNI_TARGET TRUE)
add_jar(schedulers-java-test-jar
		${CMAKE_CURRENT_LIST_DIR}/de/knejp/schedulerstest/Test.java
		${CMAKE_CURRENT_LIST_DIR}/de/knejp/schedulerstest/SharedNativeThreadPoolTest.java
		${CMAKE_CURRENT_LIST_DIR}/../../src/java/de/knejp/schedulers/Schedulers.java
		${CMAKE_CURRENT_LIST_DIR}/../../src/java/de/knejp/schedulers/SharedNativeThreadPoolExecutor.java
		)

get_target_property(test1_jar_file schedulers-java-test-jar JAR_FILE)

add_dependencies(schedulers-java-test-jar
                 schedulers-java-test
                 )

add_test(NAME schedulers-java-test 
		 COMMAND ${Java_JAVA_EXECUTABLE} -cp ".:${test1_jar_file}" -Djava.library.path=$<TARGET_FILE_DIR:schedulers-java-test> de.knejp.schedulerstest.Test
		 )
