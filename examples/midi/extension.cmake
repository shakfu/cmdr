# ext/midi links the vendored libremidi, which needs C++20 and CMake 3.22.
# Included by the top-level CMakeLists.txt when CMDR_MIDI is ON or AUTO, in
# file scope, as enable_language requires.
include(CheckLanguage)
check_language(CXX)
if(CMAKE_CXX_COMPILER AND NOT CMAKE_VERSION VERSION_LESS 3.22)
  enable_language(CXX)
  # The compiler's flag, then the headers libremidi includes, which an older
  # standard library may lack.
  if(cxx_std_20 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX20_STANDARD_COMPILE_OPTION}")
    check_cxx_source_compiles("
      #include <bit>
      #include <compare>
      #include <span>
      template <typename T> concept small = sizeof(T) < 8;
      struct p { int x; auto operator<=>(const p &) const = default; };
      int main() { int a[2] = {}; std::span<int> s(a); return small<p> && s.size() == 2 && std::popcount(1u) == 1 ? 0 : 1; }"
      CMDR_HAVE_CXX20)
    unset(CMAKE_REQUIRED_FLAGS)
  endif()
endif()
if(NOT CMAKE_CXX_COMPILER OR CMAKE_VERSION VERSION_LESS 3.22)
  set(reason "needs a C++ compiler and CMake 3.22")
elseif(NOT CMDR_HAVE_CXX20)
  set(reason "needs a C++20 compiler and standard library")
else()
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
