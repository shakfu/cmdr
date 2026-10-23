# ext/midi links the vendored libremidi, which needs C++ and CMake 3.22.
# Included by the top-level CMakeLists.txt when CMDR_MIDI is ON or AUTO, in
# file scope, as enable_language requires.
include(CheckLanguage)
check_language(CXX)
if(NOT CMAKE_CXX_COMPILER OR CMAKE_VERSION VERSION_LESS 3.22)
  set(reason "needs a C++ compiler and CMake 3.22")
else()
  enable_language(CXX)
  # A function, so libremidi's settings stay out of cmdr's scope. Only the
  # system MIDI APIs: no Boost, network, JACK, or keyboard backends. PipeWire's
  # backend would fetch readerwriterqueue from the network. EXCLUDE_FROM_ALL
  # keeps libremidi out of cmdr's install.
  function(cmdr_add_libremidi)
    set(BUILD_SHARED_LIBS OFF)
    foreach(option IN ITEMS BOOST NETWORK JACK PIPEWIRE KEYBOARD)
      set(LIBREMIDI_NO_${option} ON)
    endforeach()
    add_subdirectory("${PROJECT_SOURCE_DIR}/thirdparty/libremidi-5.4.3"
                     "${PROJECT_BINARY_DIR}/examples/midi/libremidi" EXCLUDE_FROM_ALL)
  endfunction()
  cmdr_add_libremidi()
  set(found TRUE)
  set(sources "${CMAKE_CURRENT_LIST_DIR}/ext/midi/smf.cpp")
  set(libraries libremidi)
endif()
