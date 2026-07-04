# cpp-project-template

A minimal C++ project template with CMake, Docker-based development environment, and VS Code Dev Containers support.

**Stack:** C++20, CMake 3.20+, Ubuntu 24.04 (GCC, clang-tidy, CMake, Git)

## Prerequisites

- [Docker](https://docs.docker.com/get-docker/) — for the development environment
- CMake 3.20+ — only needed if building outside Docker

## Development environment

Docker provides a consistent build environment. Scripts live in `docker/`:

| Script | Purpose |
|--------|---------|
| `docker_run.sh` | Start an interactive shell (builds the image on first run) |
| `docker_build.sh` | Rebuild the `cpp-env` image |
| `docker_clean.sh` | Stop containers and remove the `cpp-env` image |

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

As an alternative to starting Docker manually, VS Code can configure the environment automatically using the [Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) extension. It opens the project in the same Docker image and installs the C++ and CMake extensions.

1. Open the project in VS Code
2. **Ctrl+Shift+P** → **Dev Containers: Reopen in Container**

The container uses `docker/Dockerfile` (same as `docker_run.sh`).

## Build

CMake is the build system. Presets are defined in `CMakePresets.json`:

| Preset | Output directory | Description |
|--------|------------------|-------------|
| `release` | `build/release` | Optimized build |
| `relwithdebinfo` | `build/relwithdebinfo` | Optimized build with debug symbols |
| `debug` | `build/debug` | Unoptimized build with debug symbols |
| `asan` | `build/asan` | Sanitizer-instrumented build |
| `tests` | `build/tests` | Inherits `asan` with unit tests enabled |
| `coverage` | `build/coverage` | Inherits `debug` with tests and `src/` coverage instrumentation |
| `clang-tidy` | `build/clang-tidy` | Inherits `debug` with static analysis on `src/` |

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

Run the executable:

```bash
./build/release/cpp-project-template
# or
./build/debug/cpp-project-template
# or
./build/asan/cpp-project-template
```

Clean build outputs:

```bash
rm -rf build
```

## Testing

Unit tests use [Google Test](https://github.com/google/googletest), fetched automatically by CMake. The `tests` preset builds in `build/tests` with `BUILD_TESTING` enabled and AddressSanitizer/UBSan instrumentation.

```bash
cmake --preset tests
cmake --build --preset tests
ctest --preset tests
```

## Coverage

The `coverage` preset builds the `src/` targets with GCC coverage instrumentation
(`--coverage`) and enables the unit tests. Coverage is scoped to `src/` — the
report filters out test sources and Google Test headers.

Configure, build, and run the tests to produce coverage data, then build the
`coverage-report` preset. That step prints coverage summaries with `lcov` and
generates an HTML report with `genhtml` (included in the Docker image):

```bash
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage
cmake --build --preset coverage-report
```

The last step writes an HTML report to
`build/coverage/coverage-report/index.html`.

## Clang-Tidy

Static analysis uses [clang-tidy](https://clang.llvm.org/extra/clang-tidy/) with
the project `.clang-tidy` config. Analysis is scoped to `src/` and uses
`compile_commands.json` from the `clang-tidy` preset (included in the Docker
image).

Configure, then run the `clang-tidy-check` build preset:

```bash
cmake --preset clang-tidy
cmake --build --preset clang-tidy-check
```

Files are analyzed in parallel via `run-clang-tidy`. Job count defaults to the
CPU count; set it at configure time with `-DCLANG_TIDY_JOBS=8`:

```bash
cmake --preset clang-tidy -DCLANG_TIDY_JOBS=8
cmake --build --preset clang-tidy-check
```

## Project layout

```
.
├── CMakeLists.txt          # Root CMake project
├── CMakePresets.json       # Release, debug, tests, ASan, coverage, and clang-tidy presets
├── src/                    # Application source
├── tests/                  # Google Test unit tests
├── docker/                 # Docker image and helper scripts
└── .devcontainer/          # VS Code Dev Container config
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) and [CODE_STYLE.md](CODE_STYLE.md).

## License

Apache License 2.0 — see [LICENSE](LICENSE).
