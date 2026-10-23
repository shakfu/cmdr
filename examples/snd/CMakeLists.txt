# ext/snd's tests/test.cmdr, the snd module's tests, and the demo, added by the
# top-level CMakeLists.txt when cmdr is built with snd. Each writes audio into
# its own SND_OUT.
cmdr_add_extension_tests(cmdr "${CMAKE_CURRENT_SOURCE_DIR}/ext/snd")
# Under umask 027, so the test sees outputs follow the umask.
add_test(NAME example_sox_dsl COMMAND sh -c "umask 027 && exec \"$0\" \"$1\""
  $<TARGET_FILE:cmdr> "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_snd.cmdr")
add_test(NAME example_sox_demo COMMAND cmdr "${CMAKE_CURRENT_SOURCE_DIR}/demo.cmdr")
foreach(test IN ITEMS ext_snd_test example_sox_dsl example_sox_demo)
  set_tests_properties(${test} PROPERTIES TIMEOUT 60
    ENVIRONMENT "SND_OUT=${CMAKE_CURRENT_BINARY_DIR}/${test}")
endforeach()
set_tests_properties(example_sox_demo PROPERTIES
  PASS_REGULAR_EXPRESSION "telephone.wav rate 8000 channels 1")
# Ctrl-C during snd.run inside try.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME example_sox_interrupt
    COMMAND ${CMAKE_COMMAND} -E env "CMDR_SND_BIN=$<TARGET_FILE:cmdr>"
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_interrupt.py")
  set_tests_properties(example_sox_interrupt PROPERTIES TIMEOUT 60)
endif()
