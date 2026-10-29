find_program(CLANG_FORMAT_EXECUTABLE clang-format)
if(NOT CLANG_FORMAT_EXECUTABLE)
  message(FATAL_ERROR "clang-format is required for format-check")
endif()

find_program(CMAKE_FORMAT_EXECUTABLE cmake-format)
if(NOT CMAKE_FORMAT_EXECUTABLE)
  message(FATAL_ERROR "cmake-format is required for format-check")
endif()

find_package(Git QUIET)
if(NOT GIT_EXECUTABLE)
  message(FATAL_ERROR "git is required to list the files to check")
endif()

# Defaults to the repository root so the script also runs standalone: cmake -P cmake/format.cmake
if(NOT FORMAT_SOURCE_DIR)
  get_filename_component(FORMAT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

# Lists git-tracked files matching the given pathspecs
function(list_tracked_files out_var)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" ls-files -- ${ARGN}
    WORKING_DIRECTORY "${FORMAT_SOURCE_DIR}"
    OUTPUT_VARIABLE files
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  string(REPLACE "\n" ";" files "${files}")
  set(${out_var}
      "${files}"
      PARENT_SCOPE)
endfunction()

list_tracked_files(cxx_files "*.cpp" "*.cc" "*.h" "*.hpp")
list_tracked_files(cmake_files "CMakeLists.txt" "*/CMakeLists.txt" "*.cmake")

# Runs both checks before failing so one run reports every violation
set(failed "")

list(LENGTH cxx_files file_count)
message(STATUS "Checking formatting of ${file_count} files with ${CLANG_FORMAT_EXECUTABLE}")
execute_process(
  COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${cxx_files}
  WORKING_DIRECTORY "${FORMAT_SOURCE_DIR}"
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  list(APPEND failed clang-format)
endif()

list(LENGTH cmake_files file_count)
message(STATUS "Checking formatting of ${file_count} files with ${CMAKE_FORMAT_EXECUTABLE}")
execute_process(
  COMMAND "${CMAKE_FORMAT_EXECUTABLE}" --check ${cmake_files}
  WORKING_DIRECTORY "${FORMAT_SOURCE_DIR}"
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  list(APPEND failed cmake-format)
endif()

if(failed)
  list(JOIN failed ", " failed)
  message(FATAL_ERROR "Formatting check failed: ${failed}")
endif()
