# ext/snd links libsox, and libsoxr for varispeed. Included by the top-level
# CMakeLists.txt when CMDR_SND is ON or AUTO.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(SND IMPORTED_TARGET sox soxr)
endif()
if(SND_FOUND)
  find_package(Threads REQUIRED)
  set(found TRUE)
  set(libraries PkgConfig::SND Threads::Threads)
else()
  set(reason "pkg-config cannot find sox and soxr")
endif()
