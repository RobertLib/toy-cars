# Exercise the real build rules in an isolated fixture. No game assets or
# source files in the working tree are modified by this regression test.
cmake_minimum_required(VERSION 3.22)

function(run_checked)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Command failed (${result}): ${ARGV}\n${output}\n${error}")
  endif()
endfunction()

set(source "${TEST_BINARY_DIR}/source")
set(build "${TEST_BINARY_DIR}/build")
file(MAKE_DIRECTORY "${source}/assets/tracks")
file(COPY "${PROJECT_SOURCE_DIR}/CMakeLists.txt" "${PROJECT_SOURCE_DIR}/main.c"
  "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests" DESTINATION "${source}")
file(WRITE "${source}/assets/tracks/fixture.tcp" "initial track contents\n")

run_checked("${CMAKE_COMMAND}" -S "${source}" -B "${build}"
  -G "${TEST_GENERATOR}" "-DCMAKE_C_COMPILER=${TEST_C_COMPILER}"
  "-DSDL3_DIR=${TEST_SDL3_DIR}" -DCMAKE_BUILD_TYPE=Release)

# Multi-configuration generators put the executable under the configuration.
file(STRINGS "${build}/CMakeCache.txt" configurations
  REGEX "^CMAKE_CONFIGURATION_TYPES:")
if(configurations)
  set(destination "${build}/${TEST_CONFIG}/assets")
else()
  set(destination "${build}/assets")
endif()

run_checked("${CMAKE_COMMAND}" --build "${build}" --config "${TEST_CONFIG}"
  --target ToyCars --parallel 2)
file(READ "${destination}/tracks/fixture.tcp" actual)
if(NOT actual STREQUAL "initial track contents\n")
  message(FATAL_ERROR "The initial build did not copy the track")
endif()

# Change existing content and add a new asset without changing any C source.
file(WRITE "${source}/assets/tracks/fixture.tcp" "updated track contents\n")
file(WRITE "${source}/assets/new-asset.txt" "new asset\n")
run_checked("${CMAKE_COMMAND}" --build "${build}" --config "${TEST_CONFIG}"
  --target ToyCars --parallel 2)
file(READ "${destination}/tracks/fixture.tcp" actual)
if(NOT actual STREQUAL "updated track contents\n")
  message(FATAL_ERROR "An asset-only change left stale data beside the executable")
endif()
file(READ "${destination}/new-asset.txt" actual)
if(NOT actual STREQUAL "new asset\n")
  message(FATAL_ERROR "The build did not copy the newly added asset")
endif()

message(STATUS "Asset-only edits and newly added assets are copied on rebuild")
