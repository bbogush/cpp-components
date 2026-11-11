# cpp-components

Reusable C++26 components packaged as a static library for consumption via CMake FetchContent.

**Stack:** C++26, CMake 3.25+, Ubuntu 26.04 (GCC, clang-tidy, cppcheck, CMake, Git, Node.js for
prettier, shfmt)

## Prerequisites

- [Docker](https://docs.docker.com/get-docker/) - for the development environment
- CMake 3.25+ - only needed if building outside Docker
- System libraries: Boost (`libboost-dev`), OpenSSL, libcurl, liburing (`liburing-dev`)
- Linux kernel >= 5.10 (Boost.Asio uses the io_uring backend)

## Using as a dependency

Pull this repository with FetchContent and link the umbrella target:

```cmake
include(FetchContent)
FetchContent_Declare(
  cpp-components
  GIT_REPOSITORY <repo-url>
  GIT_TAG <tag-or-commit>
)
FetchContent_MakeAvailable(cpp-components)

target_link_libraries(my_app PRIVATE cpp_components::cpp_components)
```

Include headers with the `cpp_components/` prefix:

```cpp
#include "cpp_components/executor/executor.h"
```

The consumer must provide Boost, OpenSSL, libcurl, and liburing (same packages as this project's
Docker image).

## Development environment

Docker provides a consistent build environment. Scripts live in `docker/`:

| Script            | Purpose                                                    |
| ----------------- | ---------------------------------------------------------- |
| `docker_run.sh`   | Start an interactive shell (builds the image on first run) |
| `docker_build.sh` | Rebuild the `cpp-env` image                                |
| `docker_clean.sh` | Stop containers and remove the `cpp-env` image             |

Start the environment from the project root:

```bash
./docker/docker_run.sh
```

Your project directory is mounted at `/project` inside the container.

Clean up when done:

```bash
./docker/docker_clean.sh
```

## VS Code Dev Containers

As an alternative to starting Docker manually, VS Code can configure the environment automatically
using the
[Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)
extension. It opens the project in the same Docker image and installs the C++, CMake, Prettier and
shfmt extensions.

1. Open the project in VS Code
2. **Ctrl+Shift+P** -> **Dev Containers: Reopen in Container**

The container uses `docker/Dockerfile` (same as `docker_run.sh`).

## Build

CMake is the build system. Presets are defined in `CMakePresets.json`:

| Preset                    | Output directory                | Description                                                     |
| ------------------------- | ------------------------------- | --------------------------------------------------------------- |
| `release`                 | `build/release`                 | Optimized build                                                 |
| `relwithdebinfo`          | `build/relwithdebinfo`          | Optimized build with debug symbols and frame pointers           |
| `debug`                   | `build/debug`                   | Unoptimized build with debug symbols                            |
| `tests`                   | `build/tests`                   | Inherits `debug` with unit tests enabled                        |
| `asan`                    | `build/asan`                    | Sanitizer-instrumented build                                    |
| `tests-asan`              | `build/tests-asan`              | Inherits `asan` with unit tests enabled                         |
| `tsan`                    | `build/tsan`                    | ThreadSanitizer-instrumented build                              |
| `tests-tsan`              | `build/tests-tsan`              | Inherits `tsan` with unit tests enabled                         |
| `tests-valgrind-memcheck` | `build/tests-valgrind-memcheck` | Inherits `debug` with unit tests under Valgrind Memcheck        |
| `tests-coverage`          | `build/tests-coverage`          | Inherits `debug` with tests and coverage instrumentation        |
| `clang-tidy`              | `build/clang-tidy`              | Inherits `debug` with clang-tidy                                |
| `cppcheck`                | `build/cppcheck`                | Inherits `debug` with cppcheck                                  |
| `benchmarks`              | `build/benchmarks`              | Inherits `relwithdebinfo` with Google Benchmark targets enabled |

From inside the container (or locally with CMake installed):

```bash
cmake --preset release
cmake --build --preset release
```

Or for a debug build with symbols:

```bash
cmake --preset debug
cmake --build --preset debug
```

Or for AddressSanitizer:

```bash
cmake --preset asan
cmake --build --preset asan
```

The build product is the static library `libcpp_components.a` (for example under `build/release/`).

Clean build outputs:

```bash
rm -rf build
```

## Testing

Unit tests use [Google Test](https://github.com/google/googletest), fetched automatically by CMake.
The `tests` preset builds in `build/tests` with the same flags as `debug`.

```bash
cmake --preset tests
cmake --build --preset tests
ctest --preset tests
```

The `tests-asan` preset builds in `build/tests-asan` and AddressSanitizer/UBSan instrumentation.

```bash
cmake --preset tests-asan
cmake --build --preset tests-asan
ctest --preset tests-asan
```

The `tests-tsan` preset builds in `build/tests-tsan` and ThreadSanitizer instrumentation.

```bash
cmake --preset tests-tsan
cmake --build --preset tests-tsan
ctest --preset tests-tsan
```

The `tests-valgrind-memcheck` preset builds in `build/tests-valgrind-memcheck` and runs each test
under Valgrind Memcheck.

```bash
cmake --preset tests-valgrind-memcheck
cmake --build --preset tests-valgrind-memcheck
ctest --preset tests-valgrind-memcheck
```

## Benchmarks

Benchmarks use [Google Benchmark](https://github.com/google/benchmark), fetched automatically by
CMake. The `benchmarks` preset builds in `build/benchmarks` with `BUILD_BENCHMARKS` enabled and
RelWithDebInfo (`-O3 -g -fno-omit-frame-pointer`) so timings stay optimized while `perf` can resolve
symbols and unwind stacks.

```bash
cmake --preset benchmarks
cmake --build --preset benchmarks
cmake --build --preset benchmarks-report
```

The `benchmarks-report` preset builds any missing benchmark binaries and runs them all (equivalent
to invoking each `*_benchmark` executable under `build/benchmarks/`). Each benchmark runs once, and
each binary writes its results to `build/benchmarks/benchmark-results/<binary>.json`.

May need to disable CPU scaling:

```bash
sudo cpupower frequency-set -g performance
```

Optional Linux `perf` wrapping (may need elevated privileges):

```bash
sudo perf stat ./build/benchmarks/secure_websocket_client_read_benchmark
sudo perf record -g ./build/benchmarks/secure_websocket_client_read_benchmark
sudo perf report
sudo perf report --stdio > perf_report.txt
```

## Coverage

The `tests-coverage` preset builds the `cpp_components/` targets with GCC coverage instrumentation
(`--coverage`) and enables the unit tests. Coverage is scoped to `cpp_components/` - the report
filters out test sources and Google Test headers.

Configure, build, and run the tests to produce coverage data, then build the `coverage-report`
preset. That step prints coverage summaries with `lcov` and generates an HTML report with `genhtml`
(included in the Docker image):

```bash
cmake --preset tests-coverage
cmake --build --preset tests-coverage
ctest --preset tests-coverage
cmake --build --preset coverage-report
```

The last step writes an HTML report to `build/tests-coverage/coverage-report/index.html`.

## Clang-Tidy

Static analysis uses [clang-tidy](https://clang.llvm.org/extra/clang-tidy/) with the project
`.clang-tidy` config. Analysis is scoped to `cpp_components/` and uses `compile_commands.json` from
the `clang-tidy` preset (included in the Docker image).

Configure, then run the `clang-tidy` build preset:

```bash
cmake --preset clang-tidy
cmake --build --preset clang-tidy
```

Files are analyzed in parallel via `run-clang-tidy`. Job count defaults to the CPU count; set it at
configure time with `-DCLANG_TIDY_JOBS=8`:

```bash
cmake --preset clang-tidy -DCLANG_TIDY_JOBS=8
cmake --build --preset clang-tidy
```

## Cppcheck

Static analysis uses [Cppcheck](https://cppcheck.sourceforge.io/). Analysis is scoped to
`cpp_components/` and uses `compile_commands.json` from the `cppcheck` preset (included in the
Docker image). Enabled severities are warning, style, performance, and portability. Findings fail
the build.

Configure, then run the `cppcheck` build preset:

```bash
cmake --preset cppcheck
cmake --build --preset cppcheck
```

Job count defaults to the CPU count; set it at configure time with `-DCPPCHECK_JOBS=8`:

```bash
cmake --preset cppcheck -DCPPCHECK_JOBS=8
cmake --build --preset cppcheck
```

## Formatting

The `format-check` build preset checks all git-tracked files without modifying them and fails on any
violation:

- C++ files (`*.cpp`, `*.cc`, `*.h`, `*.hpp`) with clang-format, using the project `.clang-format`.
- CMake files (`CMakeLists.txt`, `*.cmake`) with cmake-format, using the project
  `.cmake-format.yaml`.
- JSON, YAML and Markdown files (`*.json`, `*.yml`, `*.yaml`, `*.md`) with prettier, using the
  project `.prettierrc.json`. The pinned prettier version is fetched with `npx`, so Node.js is
  required (installed in the Docker image).
- Shell scripts (`*.sh`) with shfmt, using the indentation settings from the project
  `.editorconfig`.

```bash
cmake --preset debug
cmake --build --preset format-check
```

or

```bash
cmake -P cmake/format.cmake
```

VS Code formats on save when the project extensions are installed (see
`.devcontainer/devcontainer.json`). To format from the command line:

```
clang-format -i file.cpp file.h
```

```
cmake-format -i CMakeLists.txt
```

```
prettier --write file.md file.json file.yaml
```

```
shfmt -w script.sh
```

## Project layout

```
.
|-- CMakeLists.txt          # Root CMake project
|-- CMakePresets.json       # Release, debug, tests, ASan, TSan, Valgrind, coverage, clang-tidy, cppcheck and benchmarks
|-- cpp_components/         # Library sources and public headers
|-- tests/                  # Google Test unit tests
|-- benchmarks/             # Google Benchmark targets
|-- docker/                 # Docker image and helper scripts
`-- .devcontainer/          # VS Code Dev Container config
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) and [CODE_STYLE.md](CODE_STYLE.md).

## License

Apache License 2.0 - see [LICENSE](LICENSE).
