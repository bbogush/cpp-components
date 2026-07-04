find_program(RUN_CLANG_TIDY_EXECUTABLE run-clang-tidy)
if(NOT RUN_CLANG_TIDY_EXECUTABLE)
  message(FATAL_ERROR "run-clang-tidy is required when ENABLE_CLANG_TIDY is ON")
endif()

if(NOT CLANG_TIDY_BINARY_DIR)
  message(FATAL_ERROR "CLANG_TIDY_BINARY_DIR is required")
endif()

if(NOT CLANG_TIDY_SOURCE_DIR)
  message(FATAL_ERROR "CLANG_TIDY_SOURCE_DIR is required")
endif()

set(_compile_commands "${CLANG_TIDY_BINARY_DIR}/compile_commands.json")
if(NOT EXISTS "${_compile_commands}")
  message(FATAL_ERROR
    "compile_commands.json not found in ${CLANG_TIDY_BINARY_DIR}. "
    "Enable CMAKE_EXPORT_COMPILE_COMMANDS.")
endif()

set(_jobs "${CLANG_TIDY_JOBS}")
if(NOT _jobs OR _jobs EQUAL 0)
  include(ProcessorCount)
  ProcessorCount(_jobs)
  if(_jobs EQUAL 0)
    set(_jobs 1)
  endif()
endif()

message(STATUS "Running clang-tidy on ${CLANG_TIDY_SOURCE_DIR}/src/ (${_jobs} jobs)")
execute_process(
  COMMAND "${RUN_CLANG_TIDY_EXECUTABLE}"
          -p "${CLANG_TIDY_BINARY_DIR}"
          -j "${_jobs}"
          src/
  WORKING_DIRECTORY "${CLANG_TIDY_SOURCE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY
)
