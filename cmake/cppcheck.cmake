find_program(CPPCHECK_EXECUTABLE cppcheck)
if(NOT CPPCHECK_EXECUTABLE)
  message(FATAL_ERROR "cppcheck is required when ENABLE_CPPCHECK is ON")
endif()

if(NOT CPPCHECK_BINARY_DIR)
  message(FATAL_ERROR "CPPCHECK_BINARY_DIR is required")
endif()

if(NOT CPPCHECK_SOURCE_DIR)
  message(FATAL_ERROR "CPPCHECK_SOURCE_DIR is required")
endif()

set(_compile_commands "${CPPCHECK_BINARY_DIR}/compile_commands.json")
if(NOT EXISTS "${_compile_commands}")
  message(FATAL_ERROR "compile_commands.json not found in ${CPPCHECK_BINARY_DIR}. "
                      "Enable CMAKE_EXPORT_COMPILE_COMMANDS.")
endif()

set(_jobs "${CPPCHECK_JOBS}")
if(NOT _jobs OR _jobs EQUAL 0)
  include(ProcessorCount)
  ProcessorCount(_jobs)
  if(_jobs EQUAL 0)
    set(_jobs 1)
  endif()
endif()

message(STATUS "Running cppcheck")
execute_process(
  COMMAND
    "${CPPCHECK_EXECUTABLE}" --project=${_compile_commands} --file-filter=*/cpp_components/*
    --enable=warning,style,performance,portability --inline-suppr --error-exitcode=1 --quiet -j
    ${_jobs} COMMAND_ERROR_IS_FATAL ANY)
