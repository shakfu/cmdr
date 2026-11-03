# Fuzz targets

One target per reader of untrusted input. Each file's header states the properties it checks.

| Target | Code |
|-|-|
| `fuzz_parse` | `cmdr_scan`, `cmdr_check_syntax`, `cmdr_format`, and compiling a line |
| `fuzz_json` | `ext/json`'s scanner, through the commands that parse JSON |
| `fuzz_csv` | `csv.rows`, `csv.dicts`, and `csv.format` |
| `fuzz_base64` | `base64.decode` and `base64.encode` |

`ctest` replays each target over `corpus/TARGET/`, through `replay.c`. `fuzz_parse` also replays every `ext/*/tests/*.cmdr`.

To fuzz, configure with clang and `-DCMDR_FUZZ=ON`, which links libFuzzer in place of `replay.c`. Apple's clang has no libFuzzer runtime; use Homebrew's LLVM:

```sh
CC=/opt/homebrew/opt/llvm/bin/clang \
CFLAGS='-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined -g -O1' \
cmake -S . -B build-fuzz -DCMDR_FUZZ=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fuzz --target fuzz_csv
cp -R tests/fuzz/corpus/csv /tmp/corpus-csv
ASAN_OPTIONS=detect_leaks=1 build-fuzz/tests/fuzz_csv -max_total_time=60 -rss_limit_mb=0 /tmp/corpus-csv
```

libFuzzer adds inputs to its first corpus directory, so fuzz a copy. On macOS, LeakSanitizer is off unless `ASAN_OPTIONS=detect_leaks=1`. `-rss_limit_mb=0` stops libFuzzer's RSS thread, whose allocation LeakSanitizer reports at exit.

A crash writes `crash-HASH`. Fix the bug, then add the input to `corpus/TARGET/` so ctest replays it.
