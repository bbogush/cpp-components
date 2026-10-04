find_program(CLANG_FORMAT_EXECUTABLE clang-format)
if(NOT CLANG_FORMAT_EXECUTABLE)
  message(FATAL_ERROR "clang-format is required for clang-format-check")
endif()

find_package(Git QUIET)
if(NOT GIT_EXECUTABLE)
  message(FATAL_ERROR "git is required to list the files to check")
endif()

# Defaults to the repository root so the script also runs standalone:
#   cmake -P cmake/clang_format.cmake
if(NOT CLANG_FORMAT_SOURCE_DIR)
  get_filename_component(CLANG_FORMAT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

execute_process(
  COMMAND "${GIT_EXECUTABLE}" ls-files -- "*.cpp" "*.cc" "*.h" "*.hpp"
  WORKING_DIRECTORY "${CLANG_FORMAT_SOURCE_DIR}"
  OUTPUT_VARIABLE _files
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY
)
string(REPLACE "\n" ";" _files "${_files}")
list(LENGTH _files _file_count)

message(STATUS "Checking formatting of ${_file_count} files with ${CLANG_FORMAT_EXECUTABLE}")
execute_process(
  COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${_files}
  WORKING_DIRECTORY "${CLANG_FORMAT_SOURCE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY
)
