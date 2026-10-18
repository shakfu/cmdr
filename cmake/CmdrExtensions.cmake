# Code generation for the prelude and extensions. Paths under src/ use
# PROJECT_SOURCE_DIR, since these functions are also called from tests/.

# Append FILE to OUT as a NUL-terminated array named SYMBOL; C11 has no #embed.
function(cmdr_embed out file symbol)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
  file(READ "${file}" hex HEX)
  string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
  set(${out} "${${out}}const char ${symbol}[] = {${bytes}0};\n" PARENT_SCOPE)
endfunction()

# Write PATH, copying only on change, so reconfiguring does not force a rebuild.
function(cmdr_generate path content)
  file(WRITE "${path}.tmp" "${content}")
  configure_file("${path}.tmp" "${path}" COPYONLY)
endfunction()

# Build each extension directory NAME/ into TARGET: its .c files, which define
# `const cmdr cmdr_ext_NAME`, and its .cmdr files, embedded in name order.
# See README.md, "Extensions".
function(cmdr_add_extensions target)
  set(code "#include \"extensions.h\"\n")
  set(layers "")
  set(scripts "")
  foreach(dir IN LISTS ARGN)
    get_filename_component(name "${dir}" NAME)
    if(NOT EXISTS "${dir}/README.md")
      message(FATAL_ERROR "ext/${name}: missing README.md")
    endif()
    string(MAKE_C_IDENTIFIER "${name}" id)
    file(GLOB sources CONFIGURE_DEPENDS "${dir}/*.c")
    if(sources)
      target_sources(${target} PRIVATE ${sources})
      string(APPEND code "extern const cmdr cmdr_ext_${id};\n")
      string(APPEND layers "    {\"ext/${name}\", &cmdr_ext_${id}},\n")
    endif()
    file(GLOB files CONFIGURE_DEPENDS "${dir}/*.cmdr")
    set(i 0)
    foreach(file IN LISTS files)
      get_filename_component(base "${file}" NAME)
      cmdr_embed(code "${file}" cmdr_ext_${id}_script_${i})
      string(APPEND scripts "    {\"ext/${name}/${base}\", cmdr_ext_${id}_script_${i}},\n")
      math(EXPR i "${i} + 1")
    endforeach()
  endforeach()
  string(APPEND code "const cmdr_extension cmdr_extensions[] = {\n${layers}    {0}};\n"
                     "const cmdr_script cmdr_extension_scripts[] = {\n${scripts}    {0}};\n")
  set(path "${CMAKE_CURRENT_BINARY_DIR}/${target}-extensions.c")
  cmdr_generate("${path}" "${code}")
  target_sources(${target} PRIVATE "${path}")
  target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")
endfunction()

# The cmdr executable as NAME, built with the extension directories in ARGN.
function(cmdr_add_binary name)
  add_executable(${name} "${PROJECT_SOURCE_DIR}/src/main.c")
  target_link_libraries(${name} PRIVATE cmdr_static)
  target_compile_definitions(${name} PRIVATE CMDR_VERSION="${PROJECT_VERSION}")
  cmdr_add_extensions(${name} ${ARGN})
endfunction()
