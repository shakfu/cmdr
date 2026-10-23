find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  # One CTest test per test method, read from the file; editing it
  # reconfigures, so new methods register themselves.
  set(CMDR_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/test_cmdr.py")
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
# cmdr with fixture extensions: probe works; clash defines a name twice.
foreach(fixture IN ITEMS probe clash)
  cmdr_add_binary(test_ext_${fixture} "${CMAKE_CURRENT_SOURCE_DIR}/ext/${fixture}")
endforeach()
# Importing an optional extension built without says how to build it in.
foreach(ext IN LISTS CMDR_MISSING_EXTENSIONS)
  string(TOUPPER "${ext}" option)
  add_test(NAME missing_${ext} COMMAND cmdr -c "import ${ext}")
  set_tests_properties(missing_${ext} PROPERTIES TIMEOUT 60
    PASS_REGULAR_EXPRESSION "import ${ext}: ext/${ext}/${ext}.cmdr:1: cmdr was built without ${ext}; configure with -DCMDR_${option}=ON")
endforeach()
# examples/agent with its dry agent, which runs no programs.
add_test(NAME example_agent_workflows
  COMMAND cmdr "${PROJECT_SOURCE_DIR}/examples/agent/tests/test_workflows.cmdr"
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
set_tests_properties(example_agent_workflows PROPERTIES TIMEOUT 60
  ENVIRONMENT "AW_RUNS=${CMAKE_CURRENT_BINARY_DIR}/agent-runs;CMDR_PATH=${PROJECT_SOURCE_DIR}/examples/agent")
# The demo copies its sample into a git repository and runs python3 on it.
find_program(CMDR_GIT git)
find_program(CMDR_PYTHON3 python3)
if(CMDR_GIT AND CMDR_PYTHON3)
  add_test(NAME example_agent_demo
    COMMAND cmdr "${PROJECT_SOURCE_DIR}/examples/agent/demo.cmdr")
  set_tests_properties(example_agent_demo PROPERTIES TIMEOUT 60
    ENVIRONMENT "AW_RUNS=${CMAKE_CURRENT_BINARY_DIR}/agent-demo"
    PASS_REGULAR_EXPRESSION "Bug report:.*tests: exit status 0")
endif()
# examples/agent/agent.cmdr, with the dry agent.
set(CMDR_AW "${PROJECT_SOURCE_DIR}/examples/agent/agent.cmdr")
add_test(NAME example_aw_ask COMMAND cmdr "${CMDR_AW}" ask . dry:opus where is it)
add_test(NAME example_aw_fix COMMAND cmdr "${CMDR_AW}" fix . README.md reproduce@dry diagnose@dry)
add_test(NAME example_aw_usage COMMAND cmdr "${CMDR_AW}")
set_tests_properties(example_aw_ask PROPERTIES PASS_REGULAR_EXPRESSION "dry read [.] opus.*where is it")
set_tests_properties(example_aw_fix PROPERTIES PASS_REGULAR_EXPRESSION "Bug report:")
set_tests_properties(example_aw_usage PROPERTIES PASS_REGULAR_EXPRESSION "usage: agent.cmdr ask DIR")
set_tests_properties(example_aw_ask example_aw_fix example_aw_usage PROPERTIES TIMEOUT 60
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  ENVIRONMENT "AW_RUNS=${CMAKE_CURRENT_BINARY_DIR}/agent-runs")
# The claude adapter reads claude's stream-json with jq, as the stub claude
# writes it.
find_program(CMDR_JQ jq)
# The claude adapter's sandbox, with stub bwrap and claude on PATH. With
# AW_SANDBOX=1, a failing bwrap stops claude; without it, bwrap is not run.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMDR_JQ)
  set(CMDR_AW_STUBS "${PROJECT_SOURCE_DIR}/examples/agent/tests/stubs")
  foreach(case IN ITEMS blocked ok off)
    add_test(NAME example_aw_sandbox_${case} COMMAND cmdr "${CMDR_AW}" ask . claude where is it)
    set_tests_properties(example_aw_sandbox_${case} PROPERTIES TIMEOUT 60
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
  endforeach()
  set_tests_properties(example_aw_sandbox_blocked PROPERTIES
    PASS_REGULAR_EXPRESSION "sandbox unavailable: stub bwrap" FAIL_REGULAR_EXPRESSION "stub claude ran"
    ENVIRONMENT "AW_RUNS=${CMAKE_CURRENT_BINARY_DIR}/agent-runs;PATH=${CMDR_AW_STUBS}:$ENV{PATH};AW_SANDBOX=1")
  set_tests_properties(example_aw_sandbox_ok PROPERTIES PASS_REGULAR_EXPRESSION "stub claude ran.*--settings"
    ENVIRONMENT "AW_RUNS=${CMAKE_CURRENT_BINARY_DIR}/agent-runs;PATH=${CMDR_AW_STUBS}:$ENV{PATH};AW_SANDBOX=1;AW_STUB_BWRAP=0")
  set_tests_properties(example_aw_sandbox_off PROPERTIES
    PASS_REGULAR_EXPRESSION "stub claude ran" FAIL_REGULAR_EXPRESSION "--settings|sandbox unavailable"
    ENVIRONMENT "AW_RUNS=${CMAKE_CURRENT_BINARY_DIR}/agent-runs;PATH=${CMDR_AW_STUBS}:$ENV{PATH}")
endif()
# claude's write steps run in auto mode, or AW_CLAUDE_WRITE_MODE from a fixed
# set; read steps always run in plan mode.
set(CMDR_AW_ENV "AW_RUNS=${CMAKE_CURRENT_BINARY_DIR}/agent-runs;PATH=${PROJECT_SOURCE_DIR}/examples/agent/tests/stubs:$ENV{PATH}")
if(CMDR_JQ)
  foreach(case IN ITEMS auto read accept bad)
    set(cmd edit . claude fix it)
    if(case STREQUAL "read")
      set(cmd ask . claude where is it)
    endif()
    add_test(NAME example_aw_claude_${case} COMMAND cmdr "${CMDR_AW}" ${cmd})
    set_tests_properties(example_aw_claude_${case} PROPERTIES TIMEOUT 60
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" ENVIRONMENT "${CMDR_AW_ENV}")
  endforeach()
  set_tests_properties(example_aw_claude_auto PROPERTIES
    PASS_REGULAR_EXPRESSION "stub claude ran.*--permission-mode auto")
  set_tests_properties(example_aw_claude_read PROPERTIES
    PASS_REGULAR_EXPRESSION "stub claude ran.*--permission-mode plan")
  set_tests_properties(example_aw_claude_accept PROPERTIES
    PASS_REGULAR_EXPRESSION "stub claude ran.*--permission-mode acceptEdits"
    ENVIRONMENT "${CMDR_AW_ENV};AW_CLAUDE_WRITE_MODE=acceptEdits")
  set_tests_properties(example_aw_claude_bad PROPERTIES
    PASS_REGULAR_EXPRESSION "AW_CLAUDE_WRITE_MODE must be auto or acceptEdits" FAIL_REGULAR_EXPRESSION "stub claude ran"
    ENVIRONMENT "${CMDR_AW_ENV};AW_CLAUDE_WRITE_MODE=bypassPermissions")
  # Detached runs: a dry agent, and a stub claude that sleeps until cancelled.
  add_test(NAME example_agent_runs
    COMMAND cmdr "${PROJECT_SOURCE_DIR}/examples/agent/tests/test_runs.cmdr")
  add_test(NAME example_aw_start
    COMMAND sh -c "id=$(\"$1\" \"$2\" start ask . dry where is it) && \"$1\" \"$2\" wait \"$id\" && \"$1\" \"$2\" status \"$id\" && \"$1\" \"$2\" result \"$id\"" sh
            $<TARGET_FILE:cmdr> "${CMDR_AW}")
  set_tests_properties(example_agent_runs example_aw_start PROPERTIES TIMEOUT 60
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    ENVIRONMENT "${CMDR_AW_ENV};AW_RUNS=${CMAKE_CURRENT_BINARY_DIR}/agent-detached;AW_CMDR=$<TARGET_FILE:cmdr>;AW_STUB_SLEEP=30;CMDR_PATH=${PROJECT_SOURCE_DIR}/examples/agent")
  set_tests_properties(example_aw_start PROPERTIES
    PASS_REGULAR_EXPRESSION "\tdone\t[0-9]+s\task@dry\task [.] dry where is it\n.*dry read [.] .*where is it")
endif()
# The llama agent, with a stub curl; jq and git are real.
if(CMDR_GIT AND CMDR_JQ)
  set(CMDR_AW_STUBS "${PROJECT_SOURCE_DIR}/examples/agent/tests/stubs")
  add_test(NAME example_agent_llama
    COMMAND cmdr "${PROJECT_SOURCE_DIR}/examples/agent/tests/test_llama.cmdr")
  set_tests_properties(example_agent_llama PROPERTIES TIMEOUT 60 FIXTURES_SETUP aw_llama_project
    ENVIRONMENT "AW_RUNS=${CMAKE_CURRENT_BINARY_DIR}/agent-llama;CMDR_PATH=${PROJECT_SOURCE_DIR}/examples/agent;PATH=${CMDR_AW_STUBS}:$ENV{PATH}")
  # An edit rejected twice fails with the reasons and the reply.
  add_test(NAME example_aw_llama_rejected
    COMMAND cmdr "${CMDR_AW}" edit "${CMAKE_CURRENT_BINARY_DIR}/agent-llama/llama-project" llama ALWAYSBAD)
  set_tests_properties(example_aw_llama_rejected PROPERTIES TIMEOUT 60 FIXTURES_REQUIRED aw_llama_project
    PASS_REGULAR_EXPRESSION "edits not applied:.*SEARCH must match exactly one place in stats.py.*Reply:"
    ENVIRONMENT "AW_RUNS=${CMAKE_CURRENT_BINARY_DIR}/agent-llama;PATH=${CMDR_AW_STUBS}:$ENV{PATH}")
  if(EXISTS "${PROJECT_SOURCE_DIR}/.git")
    add_test(NAME example_aw_llama_too_large COMMAND cmdr "${CMDR_AW}" ask . llama where is it)
    set_tests_properties(example_aw_llama_too_large PROPERTIES TIMEOUT 60
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      PASS_REGULAR_EXPRESSION "project exceeds AW_LLAMA_MAX_BYTES" FAIL_REGULAR_EXPRESSION "off by one"
      ENVIRONMENT "AW_RUNS=${CMAKE_CURRENT_BINARY_DIR}/agent-runs;PATH=${CMDR_AW_STUBS}:$ENV{PATH};AW_LLAMA_MAX_BYTES=10")
  endif()
endif()
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
        target_include_directories(${test} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
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
  set(CMDR_MK_EXT "${PROJECT_SOURCE_DIR}/scripts/mk_ext.py")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMDR_MK_EXT}")
  set(CMDR_SKELETON "${CMAKE_CURRENT_BINARY_DIR}/skeleton")
  file(REMOVE_RECURSE "${CMDR_SKELETON}")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMDR_MK_EXT}" mk-ext-skeleton
                          --root "${CMDR_SKELETON}" --c-test
                  OUTPUT_QUIET RESULT_VARIABLE status)
  if(status)
    message(FATAL_ERROR "scripts/mk_ext.py failed: ${status}")
  endif()
  cmdr_add_binary(test_ext_skeleton "${CMDR_SKELETON}/mk-ext-skeleton")
  cmdr_add_extension_tests(test_ext_skeleton "${CMDR_SKELETON}/mk-ext-skeleton")
  list(APPEND CMDR_TEST_TARGETS test_ext_skeleton)
endif()
foreach(target IN LISTS CMDR_LIBRARIES)
  string(REPLACE "cmdr_" "test_api_" test "${target}")
  add_executable(${test} test_api.c)
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
          "${CMAKE_CURRENT_SOURCE_DIR}/consumer" "${CMAKE_BINARY_DIR}/consumer"
          --build-generator "${CMAKE_GENERATOR}"
          --build-options "-DCMAKE_PREFIX_PATH=${CMDR_STAGED}"
                          "-DCMDR_REQUIRED_VERSION=${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
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
