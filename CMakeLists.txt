cmake_minimum_required(VERSION 3.16)
project(cmdr VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Commands share one callback signature, so most ignore some parameters.
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# Embed src/prelude.cmdr as a NUL-terminated array; C11 has no #embed.
set(CMDR_PRELUDE "${CMAKE_CURRENT_SOURCE_DIR}/src/prelude.cmdr")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMDR_PRELUDE}")
file(READ "${CMDR_PRELUDE}" CMDR_PRELUDE_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CMDR_PRELUDE_BYTES "${CMDR_PRELUDE_HEX}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/prelude.c.tmp"
     "const char cmdr_prelude_source[] = {${CMDR_PRELUDE_BYTES}0};\n")
# Copy only on change, so reconfiguring does not force a rebuild.
configure_file("${CMAKE_CURRENT_BINARY_DIR}/prelude.c.tmp"
               "${CMAKE_CURRENT_BINARY_DIR}/prelude.c" COPYONLY)

add_executable(cmdr src/main.c src/cmdr.c src/primitives.c
               "${CMAKE_CURRENT_BINARY_DIR}/prelude.c")
target_compile_definitions(cmdr PRIVATE CMDR_VERSION="${PROJECT_VERSION}")

# isfinite() and related helpers may live in libm.
find_library(CMDR_LIBM m)
if(CMDR_LIBM)
  target_link_libraries(cmdr PRIVATE ${CMDR_LIBM})
endif()

# Line editing for the interactive REPL; without it, cmdr reads plain lines.
option(CMDR_EDITOR "Build line editing with the vendored linenoise" ON)
if(CMDR_EDITOR)
  set(LINENOISE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/linenoise")
  add_library(linenoise STATIC "${LINENOISE}/src/linenoise.c"
                               "${LINENOISE}/src/internal/utf8.c")
  target_include_directories(linenoise PUBLIC "${LINENOISE}/include"
                                       PRIVATE "${LINENOISE}/include/internal")
  # linenoise declares no POSIX feature macros. Under strict C, strdup and
  # fdopen are undeclared, their int returns truncate pointers, and the REPL
  # crashes on Enter. The error keeps an update from reintroducing this.
  set_target_properties(linenoise PROPERTIES C_EXTENSIONS ON)
  target_compile_options(linenoise PRIVATE -Werror=implicit-function-declaration)
  target_sources(cmdr PRIVATE src/editor.c)
  target_link_libraries(cmdr PRIVATE linenoise)
  target_compile_definitions(cmdr PRIVATE CMDR_EDITOR)
endif()

option(CMDR_BUILD_TESTS "Build the test suite and the check target" ON)
if(CMDR_BUILD_TESTS)
  enable_testing()
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    # One CTest test per test method, read from the file; editing it
    # reconfigures, so new methods register themselves.
    set(CMDR_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cmdr.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMDR_TESTS}")
    file(STRINGS "${CMDR_TESTS}" CMDR_TEST_LINES REGEX "^    def test_[A-Za-z0-9_]+\\(")
    foreach(line IN LISTS CMDR_TEST_LINES)
      string(REGEX REPLACE "^    def (test_[A-Za-z0-9_]+)\\(.*" "\\1" name "${line}")
      add_test(NAME "${name}"
        COMMAND ${CMAKE_COMMAND} -E env "CMDR_BIN=$<TARGET_FILE:cmdr>"
                "CMDR_EDITOR=$<BOOL:${CMDR_EDITOR}>"
                "${Python3_EXECUTABLE}" "${CMDR_TESTS}" "Commands.${name}")
      # unittest reports a skip as success; show it as skipped.
      set_tests_properties("${name}" PROPERTIES TIMEOUT 60
                           SKIP_REGULAR_EXPRESSION "OK \\(skipped=")
    endforeach()
  else()
    message(STATUS "Python 3 not found: skipping the test_cmdr checks")
  endif()
  include(ProcessorCount)
  ProcessorCount(CMDR_JOBS)
  if(NOT CMDR_JOBS)
    set(CMDR_JOBS 1)
  endif()
  add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j ${CMDR_JOBS}
    DEPENDS cmdr
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running cmdr checks"
  )
endif()
