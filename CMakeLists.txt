cmake_minimum_required(VERSION 3.16)
project(cmdr VERSION 0.3.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Commands share one callback signature, so most ignore some parameters.
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
# An undeclared POSIX function, such as realpath without _XOPEN_SOURCE, would
# return int and truncate a pointer. -O2 can hide this through _FORTIFY_SOURCE.
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CmdrExtensions)

set(CMDR_PRELUDE_C "")
foreach(name IN ITEMS prelude prelude-effects)
  string(REPLACE "-" "_" symbol "cmdr_${name}_source")
  cmdr_embed(CMDR_PRELUDE_C "${CMAKE_CURRENT_SOURCE_DIR}/src/${name}.cmdr" ${symbol})
endforeach()
cmdr_generate("${CMAKE_CURRENT_BINARY_DIR}/prelude.c" "${CMDR_PRELUDE_C}")

include(GNUInstallDirs)

# libcmdr.a and libcmdr.so: the interpreter, primitives, sessions, and
# prelude, for embedding. Both use one set of position-independent objects.
option(CMDR_SHARED "Build libcmdr.so as well as libcmdr.a" ON)
add_library(cmdr_objects OBJECT src/cmdr.c src/format.c src/primitives.c src/session.c
            "${CMAKE_CURRENT_BINARY_DIR}/prelude.c")
set_target_properties(cmdr_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
# run-with's dir needs a chdir file action: POSIX.1-2024's, or the older _np
# extension. Checked under src/primitives.c's feature macros. Without either,
# dir is an error.
include(CheckSymbolExists)
set(CMAKE_REQUIRED_DEFINITIONS -D_POSIX_C_SOURCE=200809L)
check_symbol_exists(posix_spawn_file_actions_addchdir spawn.h CMDR_HAVE_ADDCHDIR)
if(NOT CMDR_HAVE_ADDCHDIR)
  list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE)
  check_symbol_exists(posix_spawn_file_actions_addchdir_np spawn.h CMDR_HAVE_ADDCHDIR_NP)
endif()
unset(CMAKE_REQUIRED_DEFINITIONS)
# The last directory import searches; fixed at configure time.
target_compile_definitions(cmdr_objects PRIVATE
  CMDR_LIBDIR="${CMAKE_INSTALL_FULL_DATADIR}/cmdr/lib")
foreach(feature IN ITEMS CMDR_HAVE_ADDCHDIR CMDR_HAVE_ADDCHDIR_NP)
  if(${feature})
    target_compile_definitions(cmdr_objects PRIVATE ${feature})
  endif()
endforeach()
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

file(GLOB entries LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ext/*")
set(CMDR_EXTENSIONS "")
foreach(entry IN LISTS entries)
  if(IS_DIRECTORY "${entry}")
    list(APPEND CMDR_EXTENSIONS "${entry}")
  endif()
endforeach()
# Extensions in examples/ that need libraries the core does not. CMDR_NAME is
# OFF, ON (configure fails if a library is missing), or AUTO (built if found).
# examples/NAME/extension.cmake finds the libraries: it sets found, or reason,
# and may set sources and libraries for cmdr.
set(CMDR_OPTIONAL_EXTENSIONS "")
set(CMDR_MISSING_EXTENSIONS "")
set(CMDR_OPTIONAL_SOURCES "")
set(CMDR_OPTIONAL_LIBRARIES "")
foreach(ext IN ITEMS snd midi)
  string(TOUPPER "${ext}" option)
  set(CMDR_${option} OFF CACHE STRING "Build examples/${ext}'s extension into cmdr: OFF, ON, or AUTO")
  set_property(CACHE CMDR_${option} PROPERTY STRINGS OFF ON AUTO)
  set(found FALSE)
  set(reason "CMDR_${option} is OFF")
  set(sources "")
  set(libraries "")
  if(CMDR_${option} STREQUAL "AUTO" OR CMDR_${option})
    include("${CMAKE_CURRENT_SOURCE_DIR}/examples/${ext}/extension.cmake")
  endif()
  if(found)
    list(APPEND CMDR_OPTIONAL_EXTENSIONS "${CMAKE_CURRENT_SOURCE_DIR}/examples/${ext}/ext/${ext}")
    list(APPEND CMDR_OPTIONAL_SOURCES ${sources})
    list(APPEND CMDR_OPTIONAL_LIBRARIES ${libraries})
  elseif(CMDR_${option} STREQUAL "AUTO" OR NOT CMDR_${option})
    message(STATUS "Building cmdr without ${ext}: ${reason}")
    list(APPEND CMDR_MISSING_EXTENSIONS ${ext})
  else()
    message(FATAL_ERROR "CMDR_${option}=${CMDR_${option}}: ${reason}")
  endif()
endforeach()
cmdr_add_binary(cmdr ${CMDR_EXTENSIONS} ${CMDR_OPTIONAL_EXTENSIONS}
                MISSING ${CMDR_MISSING_EXTENSIONS})
target_sources(cmdr PRIVATE ${CMDR_OPTIONAL_SOURCES})
target_link_libraries(cmdr PRIVATE ${CMDR_OPTIONAL_LIBRARIES})

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
  # ctest expects the test file in the build root.
  enable_testing()
  add_subdirectory(tests)
  # The tests of each optional extension built into cmdr.
  foreach(dir IN LISTS CMDR_OPTIONAL_EXTENSIONS)
    get_filename_component(ext "${dir}" NAME)
    add_subdirectory(examples/${ext})
  endforeach()
endif()
