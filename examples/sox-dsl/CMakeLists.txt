# cmdr-snd: cmdr with ext/ and ext/snd, which links libsox. Skipped when
# pkg-config cannot find it.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(SND IMPORTED_TARGET sox)
endif()
if(NOT SND_FOUND)
  message(STATUS "libsox not found: skipping examples/sox-dsl")
  return()
endif()

set(CMDR_SND_EXT "${CMAKE_CURRENT_SOURCE_DIR}/ext/snd")
cmdr_add_binary(cmdr-snd ${CMDR_EXTENSIONS} "${CMDR_SND_EXT}")
find_package(Threads REQUIRED)
target_link_libraries(cmdr-snd PRIVATE PkgConfig::SND Threads::Threads)
if(CMDR_EDITOR)
  target_sources(cmdr-snd PRIVATE "${PROJECT_SOURCE_DIR}/src/editor.c")
  target_link_libraries(cmdr-snd PRIVATE linenoise)
  target_compile_definitions(cmdr-snd PRIVATE CMDR_EDITOR)
endif()

if(NOT CMDR_BUILD_TESTS)
  return()
endif()
# ext/snd's tests/test.cmdr, the snd module's tests, and the demo. Each writes
# audio into its own SND_OUT.
cmdr_add_extension_tests(cmdr-snd "${CMDR_SND_EXT}")
add_test(NAME example_sox_dsl
  COMMAND cmdr-snd "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_snd.cmdr")
add_test(NAME example_sox_demo COMMAND cmdr-snd "${CMAKE_CURRENT_SOURCE_DIR}/demo.cmdr")
foreach(test IN ITEMS ext_snd_test example_sox_dsl example_sox_demo)
  set_tests_properties(${test} PROPERTIES TIMEOUT 60
    ENVIRONMENT "SND_OUT=${CMAKE_CURRENT_BINARY_DIR}/${test}")
endforeach()
set_property(TEST example_sox_dsl APPEND PROPERTY ENVIRONMENT
  "CMDR_PATH=${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(example_sox_demo PROPERTIES
  PASS_REGULAR_EXPRESSION "telephone.wav rate 8000 channels 1")
add_dependencies(check cmdr-snd)
