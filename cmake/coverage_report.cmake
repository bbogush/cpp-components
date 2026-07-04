find_program(LCOV_EXECUTABLE lcov)
find_program(GENHTML_EXECUTABLE genhtml)

if(NOT LCOV_EXECUTABLE OR NOT GENHTML_EXECUTABLE)
  message(FATAL_ERROR "lcov and genhtml are required for coverage-report")
endif()

if(NOT COVERAGE_BINARY_DIR)
  message(FATAL_ERROR "COVERAGE_BINARY_DIR is required")
endif()

set(_coverage_info "${COVERAGE_BINARY_DIR}/coverage.info")
set(_coverage_src_info "${COVERAGE_BINARY_DIR}/coverage-src.info")
set(_coverage_report_dir "${COVERAGE_BINARY_DIR}/coverage-report")

execute_process(
  COMMAND "${LCOV_EXECUTABLE}"
          --capture
          --directory "${COVERAGE_BINARY_DIR}"
          --ignore-errors mismatch
          --output-file "${_coverage_info}"
  COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
  COMMAND "${LCOV_EXECUTABLE}"
          --extract "${_coverage_info}" "*/src/*"
          --ignore-errors mismatch
          --output-file "${_coverage_src_info}"
  COMMAND_ERROR_IS_FATAL ANY
)

message(STATUS "Coverage summary (src/ only):")
execute_process(
  COMMAND "${LCOV_EXECUTABLE}" --summary "${_coverage_src_info}"
  COMMAND_ERROR_IS_FATAL ANY
)

message(STATUS "Coverage by file (src/ only):")
execute_process(
  COMMAND "${LCOV_EXECUTABLE}" --list "${_coverage_src_info}"
  COMMAND_ERROR_IS_FATAL ANY
)

file(MAKE_DIRECTORY "${_coverage_report_dir}")
execute_process(
  COMMAND "${GENHTML_EXECUTABLE}"
          "${_coverage_src_info}"
          --output-directory "${_coverage_report_dir}"
  COMMAND_ERROR_IS_FATAL ANY
)
