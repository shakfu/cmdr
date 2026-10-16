cmake_minimum_required(VERSION 3.16)
project(cmdr LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Commands share one callback signature, so most ignore some parameters.
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

add_executable(cmdr src/main.c src/cmdr.c src/prelude.c)

# isfinite() and related helpers may live in libm.
find_library(CMDR_LIBM m)
if(CMDR_LIBM)
  target_link_libraries(cmdr PRIVATE ${CMDR_LIBM})
endif()

option(CMDR_BUILD_TESTS "Build the test suite and the check target" ON)
if(CMDR_BUILD_TESTS)
  enable_testing()
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME test_cmdr
      COMMAND ${CMAKE_COMMAND} -E env "CMDR_BIN=$<TARGET_FILE:cmdr>"
              "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cmdr.py")
  else()
    message(STATUS "Python 3 not found: skipping the test_cmdr checks")
  endif()
  add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS cmdr
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running cmdr checks"
  )
endif()
