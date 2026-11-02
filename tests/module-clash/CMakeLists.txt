# Configured by the module_clash test, which expects it to fail: extension
# directories one/ and two/ both define the module shared.
cmake_minimum_required(VERSION 3.16)
project(module_clash LANGUAGES NONE)
include("${CMDR_SOURCE_DIR}/cmake/CmdrExtensions.cmake")
foreach(dir IN ITEMS one two)
  file(WRITE "${PROJECT_BINARY_DIR}/${dir}/README.md" "")
  file(WRITE "${PROJECT_BINARY_DIR}/${dir}/shared.cmdr" "")
endforeach()
cmdr_add_extensions(none "${PROJECT_BINARY_DIR}/one" "${PROJECT_BINARY_DIR}/two")
