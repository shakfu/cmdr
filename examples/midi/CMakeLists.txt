# cmdr-midi: cmdr with ext/ and ext/midi, which links the vendored libremidi.
# Skipped without a C++ compiler, or before CMake 3.22, which libremidi needs.
include(CheckLanguage)
check_language(CXX)
if(NOT CMAKE_CXX_COMPILER OR CMAKE_VERSION VERSION_LESS 3.22)
  message(STATUS "No C++ compiler or CMake before 3.22: skipping examples/midi")
  return()
endif()
enable_language(CXX)

# Only the system MIDI APIs: no Boost, network, JACK, or keyboard backends.
# PipeWire's backend would fetch readerwriterqueue from the network.
set(BUILD_SHARED_LIBS OFF)
foreach(option IN ITEMS BOOST NETWORK JACK PIPEWIRE KEYBOARD)
  set(LIBREMIDI_NO_${option} ON)
endforeach()
# EXCLUDE_FROM_ALL keeps libremidi out of cmdr's install.
add_subdirectory("${PROJECT_SOURCE_DIR}/thirdparty/libremidi-5.4.3"
                 "${CMAKE_CURRENT_BINARY_DIR}/libremidi" EXCLUDE_FROM_ALL)

set(CMDR_MIDI_EXT "${CMAKE_CURRENT_SOURCE_DIR}/ext/midi")
cmdr_add_binary(cmdr-midi ${CMDR_EXTENSIONS} "${CMDR_MIDI_EXT}")
target_sources(cmdr-midi PRIVATE "${CMDR_MIDI_EXT}/smf.cpp")
target_link_libraries(cmdr-midi PRIVATE libremidi)
if(CMDR_EDITOR)
  target_sources(cmdr-midi PRIVATE "${PROJECT_SOURCE_DIR}/src/editor.c")
  target_link_libraries(cmdr-midi PRIVATE linenoise)
  target_compile_definitions(cmdr-midi PRIVATE CMDR_EDITOR)
endif()

if(NOT CMDR_BUILD_TESTS)
  return()
endif()
# ext/midi's tests, the midi module's tests, and the demo. Each writes files
# into its own MIDI_OUT. tests/test_midi.c skips its playing checks, with
# status 77, where no MIDI service runs.
cmdr_add_extension_tests(cmdr-midi "${CMDR_MIDI_EXT}")
target_sources(ext_midi_test_midi PRIVATE "${CMDR_MIDI_EXT}/smf.cpp")
target_link_libraries(ext_midi_test_midi PRIVATE libremidi)
set_tests_properties(ext_midi_test_midi PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME example_midi COMMAND cmdr-midi "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_midi.cmdr")
add_test(NAME example_midi_demo COMMAND cmdr-midi "${CMAKE_CURRENT_SOURCE_DIR}/demo.cmdr")
foreach(test IN ITEMS ext_midi_test example_midi example_midi_demo)
  set_tests_properties(${test} PROPERTIES TIMEOUT 60
    ENVIRONMENT "MIDI_OUT=${CMAKE_CURRENT_BINARY_DIR}/${test}")
endforeach()
add_dependencies(check cmdr-midi ext_midi_test_midi)
