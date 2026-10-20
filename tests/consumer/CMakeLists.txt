# Builds against an installed cmdr; see the consumer test in ../../CMakeLists.txt.
cmake_minimum_required(VERSION 3.16)
project(cmdr_consumer LANGUAGES C)
enable_testing()

# The consumer test passes the version it built; before 1.0 only the same
# minor version is compatible.
find_package(cmdr ${CMDR_REQUIRED_VERSION} CONFIG REQUIRED)
set(targets cmdr::static)
if(TARGET cmdr::shared)
  list(APPEND targets cmdr::shared)
endif()
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(CMDR REQUIRED IMPORTED_TARGET cmdr)
  list(APPEND targets PkgConfig::CMDR)
endif()

foreach(target IN LISTS targets)
  string(REGEX REPLACE "[^A-Za-z]+" "_" name "consumer_${target}")
  add_executable(${name} consumer.c)
  set_target_properties(${name} PROPERTIES C_STANDARD 11)
  target_link_libraries(${name} PRIVATE ${target})
  add_test(NAME ${name} COMMAND ${name})
endforeach()
