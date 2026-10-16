cmake_minimum_required(VERSION 3.16)
project(cmdr LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

# Janet, from thirdparty/janet unless CMDR_JANET_DIR names a release tree
# (include/janet.h, lib/libjanet.a) or a source build (build/).
set(CMDR_JANET_DIR "" CACHE PATH "Use this Janet instead of thirdparty/janet")
find_package(Threads REQUIRED)
set(JANET_SYSTEM_LIBS Threads::Threads ${CMAKE_DL_LIBS})
foreach(lib m rt)
  find_library(CMDR_LIB_${lib} ${lib})
  if(CMDR_LIB_${lib})
    list(APPEND JANET_SYSTEM_LIBS ${CMDR_LIB_${lib}})
  endif()
endforeach()
add_library(janet_vm INTERFACE)
if(CMDR_JANET_DIR)
  find_path(JANET_INCLUDE_DIR janet.h
    HINTS "${CMDR_JANET_DIR}" PATH_SUFFIXES include build NO_DEFAULT_PATH)
  find_library(JANET_LIBRARY NAMES libjanet.a janet
    HINTS "${CMDR_JANET_DIR}" PATH_SUFFIXES lib build NO_DEFAULT_PATH)
  if(NOT JANET_INCLUDE_DIR OR NOT JANET_LIBRARY)
    message(FATAL_ERROR "Janet not found in CMDR_JANET_DIR=${CMDR_JANET_DIR}")
  endif()
  target_include_directories(janet_vm INTERFACE "${JANET_INCLUDE_DIR}")
  target_link_libraries(janet_vm INTERFACE "${JANET_LIBRARY}" ${JANET_SYSTEM_LIBS})
  find_program(JANET_EXECUTABLE janet
    HINTS "${CMDR_JANET_DIR}" PATH_SUFFIXES bin build NO_DEFAULT_PATH)
else()
  set(JANET_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/janet")
  add_library(janet STATIC "${JANET_SOURCE}/janet.c")
  # Janet's own build uses gnu99; third-party warnings are not ours to fix.
  set_target_properties(janet PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)
  target_compile_options(janet PRIVATE -w)
  target_include_directories(janet PUBLIC "${JANET_SOURCE}")
  target_link_libraries(janet PUBLIC ${JANET_SYSTEM_LIBS})
  target_link_libraries(janet_vm INTERFACE janet)
endif()

# The language lives in src/cmdr.janet, compiled into the binary.
set(CMDR_EMBED "${CMAKE_CURRENT_BINARY_DIR}/cmdr_janet.h")
add_custom_command(
  OUTPUT "${CMDR_EMBED}"
  COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/cmdr.janet
          -DOUTPUT=${CMDR_EMBED} -DNAME=cmdr_janet
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS src/cmdr.janet cmake/embed.cmake
  COMMENT "Embedding cmdr.janet"
)

add_executable(cmdr src/main.c "${CMDR_EMBED}")
target_include_directories(cmdr PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" include)
target_link_libraries(cmdr PRIVATE janet_vm)

# Line editing for the interactive REPL; without it, cmdr reads plain lines.
option(CMDR_LINENOISE "Build line editing from thirdparty/linenoise" ON)
if(CMDR_LINENOISE)
  set(LINENOISE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/linenoise")
  add_library(linenoise STATIC
    "${LINENOISE_SOURCE}/src/linenoise.c"
    "${LINENOISE_SOURCE}/src/internal/completion.c"
    "${LINENOISE_SOURCE}/src/internal/history.c"
    "${LINENOISE_SOURCE}/src/internal/keyparser.c"
    "${LINENOISE_SOURCE}/src/internal/render.c"
    "${LINENOISE_SOURCE}/src/internal/terminal_posix.c"
    "${LINENOISE_SOURCE}/src/internal/utf8.c")
  # gnu99, as linenoise builds itself: it declares no POSIX feature macros, so
  # strict C hides strdup and fdopen, whose implicit int returns truncate
  # pointers.
  set_target_properties(linenoise PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)
  target_include_directories(linenoise
    PUBLIC "${LINENOISE_SOURCE}/include"
    PRIVATE "${LINENOISE_SOURCE}/include/internal")
  target_sources(cmdr PRIVATE src/editor.c)
  target_link_libraries(cmdr PRIVATE linenoise)
  target_compile_definitions(cmdr PRIVATE CMDR_LINENOISE)
endif()

option(CMDR_BUILD_TESTS "Build the test suite and the check target" ON)
if(CMDR_BUILD_TESTS)
  enable_testing()
  find_package(Python3 COMPONENTS Interpreter)
  set(CMDR_CHECK_DEPENDS cmdr)
  if(NOT CMDR_JANET_DIR)
    # The janet interpreter, for running tests/test_api.janet.
    add_executable(janet_shell EXCLUDE_FROM_ALL "${JANET_SOURCE}/shell.c")
    set_target_properties(janet_shell PROPERTIES OUTPUT_NAME janet
      C_STANDARD 99 C_EXTENSIONS ON)
    target_compile_options(janet_shell PRIVATE -w)
    target_link_libraries(janet_shell PRIVATE janet)
    set(JANET_EXECUTABLE "$<TARGET_FILE:janet_shell>")
    list(APPEND CMDR_CHECK_DEPENDS janet_shell)
  endif()

  if(JANET_EXECUTABLE)
    add_test(NAME test_api
      COMMAND "${JANET_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_api.janet")
  else()
    message(STATUS "janet executable not found: skipping the test_api checks")
  endif()
  if(Python3_Interpreter_FOUND)
    add_test(
      NAME test_cmdr
      COMMAND ${CMAKE_COMMAND} -E env
              "CMDR_BIN=$<TARGET_FILE:cmdr>"
              "CMDR_LINENOISE=$<BOOL:${CMDR_LINENOISE}>"
              "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cmdr.py"
    )
  else()
    message(STATUS "Python 3 not found: skipping the test_cmdr integration checks")
  endif()

  add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS ${CMDR_CHECK_DEPENDS}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running cmdr checks"
  )
endif()
