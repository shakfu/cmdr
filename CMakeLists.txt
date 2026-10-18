cmake_minimum_required(VERSION 3.16)
project(cmdr VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Commands share one callback signature, so most ignore some parameters.
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# Append FILE to OUT as a NUL-terminated array named SYMBOL; C11 has no #embed.
function(cmdr_embed out file symbol)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
  file(READ "${file}" hex HEX)
  string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
  set(${out} "${${out}}const char ${symbol}[] = {${bytes}0};\n" PARENT_SCOPE)
endfunction()

# Write PATH, copying only on change, so reconfiguring does not force a rebuild.
function(cmdr_generate path content)
  file(WRITE "${path}.tmp" "${content}")
  configure_file("${path}.tmp" "${path}" COPYONLY)
endfunction()

set(CMDR_PRELUDE_C "")
foreach(name IN ITEMS prelude prelude-effects)
  string(REPLACE "-" "_" symbol "cmdr_${name}_source")
  cmdr_embed(CMDR_PRELUDE_C "${CMAKE_CURRENT_SOURCE_DIR}/src/${name}.cmdr" ${symbol})
endforeach()
cmdr_generate("${CMAKE_CURRENT_BINARY_DIR}/prelude.c" "${CMDR_PRELUDE_C}")

# Build each extension directory NAME/ into TARGET: its .c files, which define
# `const cmdr cmdr_ext_NAME`, and its .cmdr files, embedded in name order.
# See README.md, "Extensions".
function(cmdr_add_extensions target)
  set(code "#include \"extensions.h\"\n")
  set(layers "")
  set(scripts "")
  foreach(dir IN LISTS ARGN)
    get_filename_component(name "${dir}" NAME)
    if(NOT EXISTS "${dir}/README.md")
      message(FATAL_ERROR "ext/${name}: missing README.md")
    endif()
    string(MAKE_C_IDENTIFIER "${name}" id)
    file(GLOB sources CONFIGURE_DEPENDS "${dir}/*.c")
    if(sources)
      target_sources(${target} PRIVATE ${sources})
      string(APPEND code "extern const cmdr cmdr_ext_${id};\n")
      string(APPEND layers "    {\"ext/${name}\", &cmdr_ext_${id}},\n")
    endif()
    file(GLOB files CONFIGURE_DEPENDS "${dir}/*.cmdr")
    set(i 0)
    foreach(file IN LISTS files)
      get_filename_component(base "${file}" NAME)
      cmdr_embed(code "${file}" cmdr_ext_${id}_script_${i})
      string(APPEND scripts "    {\"ext/${name}/${base}\", cmdr_ext_${id}_script_${i}},\n")
      math(EXPR i "${i} + 1")
    endforeach()
  endforeach()
  string(APPEND code "const cmdr_extension cmdr_extensions[] = {\n${layers}    {0}};\n"
                     "const cmdr_script cmdr_extension_scripts[] = {\n${scripts}    {0}};\n")
  set(path "${CMAKE_CURRENT_BINARY_DIR}/${target}-extensions.c")
  cmdr_generate("${path}" "${code}")
  target_sources(${target} PRIVATE "${path}")
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
endfunction()

include(GNUInstallDirs)

# libcmdr.a and libcmdr.so: the interpreter, primitives, sessions, and
# prelude, for embedding. Both use one set of position-independent objects.
option(CMDR_SHARED "Build libcmdr.so as well as libcmdr.a" ON)
add_library(cmdr_objects OBJECT src/cmdr.c src/primitives.c src/session.c
            "${CMAKE_CURRENT_BINARY_DIR}/prelude.c")
set_target_properties(cmdr_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(cmdr_objects PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
add_library(cmdr_static STATIC $<TARGET_OBJECTS:cmdr_objects>)
set(CMDR_LIBRARIES cmdr_static)
if(CMDR_SHARED)
  add_library(cmdr_shared SHARED $<TARGET_OBJECTS:cmdr_objects>)
  # Before 1.0, a minor version may break the API.
  set_target_properties(cmdr_shared PROPERTIES VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
  list(APPEND CMDR_LIBRARIES cmdr_shared)
endif()

# isfinite() and related helpers may live in libm.
find_library(CMDR_LIBM m)
foreach(target IN LISTS CMDR_LIBRARIES)
  string(REPLACE "cmdr_" "" kind "${target}")
  set_target_properties(${target} PROPERTIES OUTPUT_NAME cmdr EXPORT_NAME ${kind})
  target_include_directories(${target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  if(CMDR_LIBM)
    target_link_libraries(${target} PUBLIC m)
  endif()
endforeach()

add_executable(cmdr src/main.c)
target_link_libraries(cmdr PRIVATE cmdr_static)
target_compile_definitions(cmdr PRIVATE CMDR_VERSION="${PROJECT_VERSION}")
file(GLOB entries LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ext/*")
set(CMDR_EXTENSIONS "")
foreach(entry IN LISTS entries)
  if(IS_DIRECTORY "${entry}")
    list(APPEND CMDR_EXTENSIONS "${entry}")
  endif()
endforeach()
cmdr_add_extensions(cmdr ${CMDR_EXTENSIONS})

# find_package(cmdr) gives cmdr::static and cmdr::shared; pkg-config gives cmdr.
install(TARGETS cmdr RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS ${CMDR_LIBRARIES} EXPORT cmdrTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/cmdr.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
set(CMDR_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/cmdr")
install(EXPORT cmdrTargets NAMESPACE cmdr:: DESTINATION ${CMDR_CMAKE_DIR})
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cmdrConfig.cmake"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/cmdrTargets.cmake\")\n")
include(CMakePackageConfigHelpers)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/cmdrConfigVersion.cmake"
                                 COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cmdrConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/cmdrConfigVersion.cmake"
        DESTINATION ${CMDR_CMAKE_DIR})
# The .pc file finds its prefix from its own location, so --prefix works.
file(RELATIVE_PATH CMDR_PC_PREFIX "/${CMAKE_INSTALL_LIBDIR}/pkgconfig" "/")
set(CMDR_PC_LIBS_PRIVATE "")
if(CMDR_LIBM)
  set(CMDR_PC_LIBS_PRIVATE "-lm")
endif()
configure_file(cmake/cmdr.pc.in "${CMAKE_CURRENT_BINARY_DIR}/cmdr.pc" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cmdr.pc"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

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
                "CMDR_CLASH_BIN=$<TARGET_FILE:test_ext_clash>"
                "CMDR_PROBE_BIN=$<TARGET_FILE:test_ext_probe>"
                "${Python3_EXECUTABLE}" "${CMDR_TESTS}" "Commands.${name}")
      # unittest reports a skip as success; show it as skipped.
      set_tests_properties("${name}" PROPERTIES TIMEOUT 60
                           SKIP_REGULAR_EXPRESSION "OK \\(skipped=")
    endforeach()
  else()
    message(STATUS "Python 3 not found: skipping the test_cmdr checks")
  endif()
  # cmdr with fixture extensions: probe works; clash collides with existing names.
  foreach(fixture IN ITEMS probe clash)
    add_executable(test_ext_${fixture} src/main.c)
    target_link_libraries(test_ext_${fixture} PRIVATE cmdr_static)
    target_compile_definitions(test_ext_${fixture} PRIVATE CMDR_VERSION="${PROJECT_VERSION}")
    cmdr_add_extensions(test_ext_${fixture} "${CMAKE_CURRENT_SOURCE_DIR}/tests/ext/${fixture}")
  endforeach()
  # Embedding checks; threads exercise the per-thread call depth.
  find_package(Threads REQUIRED)
  set(CMDR_TEST_TARGETS cmdr test_ext_probe test_ext_clash)
  # Each extension's tests/: BINARY runs each .cmdr file, which fails at its
  # first failing line; each .c file is a program linked with the extension's
  # sources, with tests/ext_check.h. Appends programs to CMDR_TEST_TARGETS.
  function(cmdr_add_extension_tests binary)
    foreach(dir IN LISTS ARGN)
      get_filename_component(name "${dir}" NAME)
      file(GLOB scripts CONFIGURE_DEPENDS "${dir}/tests/*.cmdr")
      file(GLOB programs CONFIGURE_DEPENDS "${dir}/tests/*.c")
      if(NOT scripts AND NOT programs)
        message(FATAL_ERROR "ext/${name}: no tests/*.cmdr or tests/*.c")
      endif()
      file(GLOB sources CONFIGURE_DEPENDS "${dir}/*.c")
      foreach(file IN LISTS scripts programs)
        get_filename_component(stem "${file}" NAME_WE)
        set(test "ext_${name}_${stem}")
        if(file MATCHES "[.]c$")
          add_executable(${test} "${file}" ${sources})
          target_link_libraries(${test} PRIVATE cmdr_static)
          target_include_directories(${test} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
          add_test(NAME ${test} COMMAND ${test})
          list(APPEND CMDR_TEST_TARGETS ${test})
        else()
          add_test(NAME ${test} COMMAND ${binary} "${file}" WORKING_DIRECTORY "${dir}/tests")
        endif()
        set_tests_properties(${test} PROPERTIES TIMEOUT 60)
      endforeach()
    endforeach()
    set(CMDR_TEST_TARGETS ${CMDR_TEST_TARGETS} PARENT_SCOPE)
  endfunction()
  cmdr_add_extension_tests(cmdr ${CMDR_EXTENSIONS})
  # A fresh scripts/mk_ext.py skeleton must build and pass its own tests.
  if(Python3_Interpreter_FOUND)
    set(CMDR_MK_EXT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mk_ext.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMDR_MK_EXT}")
    set(CMDR_SKELETON "${CMAKE_CURRENT_BINARY_DIR}/skeleton")
    file(REMOVE_RECURSE "${CMDR_SKELETON}")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMDR_MK_EXT}" mk-ext-skeleton
                            --root "${CMDR_SKELETON}" --c-test
                    OUTPUT_QUIET RESULT_VARIABLE status)
    if(status)
      message(FATAL_ERROR "scripts/mk_ext.py failed: ${status}")
    endif()
    add_executable(test_ext_skeleton src/main.c)
    target_link_libraries(test_ext_skeleton PRIVATE cmdr_static)
    target_compile_definitions(test_ext_skeleton PRIVATE CMDR_VERSION="${PROJECT_VERSION}")
    cmdr_add_extensions(test_ext_skeleton "${CMDR_SKELETON}/mk-ext-skeleton")
    cmdr_add_extension_tests(test_ext_skeleton "${CMDR_SKELETON}/mk-ext-skeleton")
    list(APPEND CMDR_TEST_TARGETS test_ext_skeleton)
  endif()
  foreach(target IN LISTS CMDR_LIBRARIES)
    string(REPLACE "cmdr_" "test_api_" test "${target}")
    add_executable(${test} tests/test_api.c)
    target_link_libraries(${test} PRIVATE ${target} Threads::Threads)
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 60)
    list(APPEND CMDR_TEST_TARGETS ${test})
  endforeach()
  # Install into the build tree, then build and run tests/consumer against it.
  set(CMDR_STAGED "${CMAKE_BINARY_DIR}/staged")
  add_test(NAME install COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}
                                --prefix ${CMDR_STAGED})
  add_test(NAME consumer
    COMMAND ${CMAKE_COMMAND} -E env
            "PKG_CONFIG_PATH=${CMDR_STAGED}/${CMAKE_INSTALL_LIBDIR}/pkgconfig"
            ${CMAKE_CTEST_COMMAND} --build-and-test
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer" "${CMAKE_BINARY_DIR}/consumer"
            --build-generator "${CMAKE_GENERATOR}"
            --build-options "-DCMAKE_PREFIX_PATH=${CMDR_STAGED}"
            --test-command ${CMAKE_CTEST_COMMAND} --output-on-failure)
  set_tests_properties(install PROPERTIES FIXTURES_SETUP staged)
  set_tests_properties(consumer PROPERTIES FIXTURES_REQUIRED staged TIMEOUT 120)
  include(ProcessorCount)
  ProcessorCount(CMDR_JOBS)
  if(NOT CMDR_JOBS)
    set(CMDR_JOBS 1)
  endif()
  add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j ${CMDR_JOBS}
    DEPENDS ${CMDR_TEST_TARGETS}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running cmdr checks"
  )
endif()
