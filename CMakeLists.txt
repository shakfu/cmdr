cmake_minimum_required(VERSION 3.16)
project(cmdr LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(cmdr_lib STATIC
  src/cmdr.c
  src/value.c
  src/prelude.c
  src/essentials.c
)
set_target_properties(cmdr_lib PROPERTIES OUTPUT_NAME cmdr)
target_include_directories(cmdr_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# isfinite() and related helpers may live in libm.
find_library(CMDR_LIBM m)
if(CMDR_LIBM)
  target_link_libraries(cmdr_lib PUBLIC ${CMDR_LIBM})
endif()

add_executable(cmdr
  src/main.c
  src/commands.c
)
target_link_libraries(cmdr PRIVATE cmdr_lib)

option(CMDR_BUILD_TESTS "Build the test suite and the check target" ON)
if(CMDR_BUILD_TESTS)
  enable_testing()
  find_package(Python3 COMPONENTS Interpreter)

  add_executable(test_api tests/test_api.c)
  target_link_libraries(test_api PRIVATE cmdr_lib)
  set_target_properties(test_api PROPERTIES EXCLUDE_FROM_ALL TRUE)
  add_test(NAME test_api COMMAND test_api)
  if(Python3_Interpreter_FOUND)
    add_test(
      NAME test_cmdr
      COMMAND ${CMAKE_COMMAND} -E env
              "CMDR_BIN=$<TARGET_FILE:cmdr>"
              "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cmdr.py"
    )
  else()
    message(STATUS "Python 3 not found: skipping the test_cmdr integration checks")
  endif()

  add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS cmdr test_api
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running cmdr checks"
  )
endif()
