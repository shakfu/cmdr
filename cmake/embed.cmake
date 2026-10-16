# Write SOURCE to OUTPUT as `static const unsigned char NAME[]` and NAME_len.
# Usage: cmake -DSOURCE=... -DOUTPUT=... -DNAME=... -P embed.cmake
file(READ "${SOURCE}" hex HEX)
string(LENGTH "${hex}" digits)
math(EXPR size "${digits} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE "${OUTPUT}"
  "static const unsigned char ${NAME}[] = {${bytes}};\n"
  "static const size_t ${NAME}_len = ${size};\n")
