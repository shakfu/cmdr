# ext/midi's tests, the midi module's tests, and the demo, added by the
# top-level CMakeLists.txt when cmdr is built with midi. Each writes files into
# its own MIDI_OUT. tests/test_midi.c skips its playing checks, with
# status 77, where no MIDI service runs.
set(CMDR_MIDI_EXT "${CMAKE_CURRENT_SOURCE_DIR}/ext/midi")
cmdr_add_extension_tests(cmdr "${CMDR_MIDI_EXT}")
target_sources(ext_midi_test_midi PRIVATE "${CMDR_MIDI_EXT}/smf.cpp")
target_link_libraries(ext_midi_test_midi PRIVATE libremidi)
set_tests_properties(ext_midi_test_midi PROPERTIES SKIP_RETURN_CODE 77)
# Under umask 027, so the test sees outputs follow the umask.
add_test(NAME example_midi COMMAND sh -c "umask 027 && exec \"$0\" \"$1\""
  $<TARGET_FILE:cmdr> "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_midi.cmdr")
add_test(NAME example_midi_demo COMMAND cmdr "${CMAKE_CURRENT_SOURCE_DIR}/demo.cmdr")
foreach(test IN ITEMS ext_midi_test example_midi example_midi_demo)
  set_tests_properties(${test} PROPERTIES TIMEOUT 60
    ENVIRONMENT "MIDI_OUT=${CMAKE_CURRENT_BINARY_DIR}/${test}")
endforeach()
add_dependencies(check ext_midi_test_midi)
