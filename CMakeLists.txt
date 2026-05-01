cmake_minimum_required(VERSION 3.22)

project(bsort VERSION 0.1.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(CTest)
include(GNUInstallDirs)
include(CheckIPOSupported)

option(BSORT_BUILD_EXECUTABLE "Build the bsort command-line executable" ON)
option(BSORT_NATIVE "Build with -march=native when supported" OFF)
option(BSORT_LTO "Enable link-time optimization/interprocedural optimization" OFF)
option(BSORT_USE_STD_SORT_CLEANUP "Use std::sort for small cleanup buckets instead of shell sort" ON)
set(BSORT_PGO "off" CACHE STRING "PGO mode: off, generate, or use")
set_property(CACHE BSORT_PGO PROPERTY STRINGS off generate use)
set(BSORT_PGO_DIR "${CMAKE_BINARY_DIR}/pgo" CACHE PATH "Directory for GCC PGO profile data")
set(BSORT_STD_SORT_CLEANUP_MIN 96 CACHE STRING "Default runtime -t value")

if(BSORT_LTO)
  check_ipo_supported(RESULT bsort_lto_supported OUTPUT bsort_lto_error)
  if(NOT bsort_lto_supported)
    message(FATAL_ERROR "BSORT_LTO requested but IPO/LTO is not supported: ${bsort_lto_error}")
  endif()
endif()

if(NOT BSORT_PGO STREQUAL "off")
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    message(FATAL_ERROR "BSORT_PGO currently supports GNU-compatible gcov PGO builds only")
  endif()

  file(MAKE_DIRECTORY "${BSORT_PGO_DIR}")
  if(NOT BSORT_PGO STREQUAL "generate" AND NOT BSORT_PGO STREQUAL "use")
    message(FATAL_ERROR "BSORT_PGO must be one of: off, generate, use")
  endif()
endif()

function(configure_bsort_target target)
  target_compile_features(${target} PUBLIC cxx_std_23)
  target_include_directories(${target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  set_target_properties(${target} PROPERTIES
    CXX_EXTENSIONS OFF
  )

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
    )

    if(BSORT_NATIVE)
      target_compile_options(${target} PRIVATE -march=native)
    endif()
  endif()

  if(BSORT_LTO)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()

  if(BSORT_PGO STREQUAL "generate")
    target_compile_options(${target} PRIVATE -fprofile-generate=${BSORT_PGO_DIR})
    target_link_options(${target} PRIVATE -fprofile-generate=${BSORT_PGO_DIR})
  elseif(BSORT_PGO STREQUAL "use")
    target_compile_options(${target} PRIVATE
      -fprofile-use=${BSORT_PGO_DIR}
      -fprofile-correction
    )
    target_link_options(${target} PRIVATE -fprofile-use=${BSORT_PGO_DIR})
  endif()

  if(BSORT_USE_STD_SORT_CLEANUP)
    target_compile_definitions(${target} PRIVATE BSORT_USE_STD_SORT_CLEANUP=1)
  else()
    target_compile_definitions(${target} PRIVATE BSORT_USE_STD_SORT_CLEANUP=0)
  endif()

  target_compile_definitions(${target} PRIVATE
    BSORT_STD_SORT_CLEANUP_MIN=${BSORT_STD_SORT_CLEANUP_MIN}
  )
endfunction()

add_library(bsort_lib src/bsort.cpp)
add_library(bsort::bsort ALIAS bsort_lib)
target_compile_definitions(bsort_lib PRIVATE BSORT_LIBRARY_ONLY=1)
set_target_properties(bsort_lib PROPERTIES
  EXPORT_NAME bsort
  OUTPUT_NAME bsort
)
configure_bsort_target(bsort_lib)

if(BSORT_BUILD_EXECUTABLE)
  add_executable(bsort src/bsort.cpp)
  configure_bsort_target(bsort)
endif()

set(bsort_config_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/bsort")

configure_package_config_file(
  cmake/bsortConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/bsortConfig.cmake"
  INSTALL_DESTINATION "${bsort_config_install_dir}"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/bsortConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(TARGETS bsort_lib
  EXPORT bsortTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(BSORT_BUILD_EXECUTABLE)
  install(TARGETS bsort
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()

install(EXPORT bsortTargets
  NAMESPACE bsort::
  DESTINATION "${bsort_config_install_dir}"
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/bsortConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/bsortConfigVersion.cmake"
  DESTINATION "${bsort_config_install_dir}"
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING)
  if(BSORT_BUILD_EXECUTABLE)
    add_test(
      NAME bsort_smoke
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/bsort_smoke.sh $<TARGET_FILE:bsort>
    )
  endif()

  add_executable(bsort_library_smoke tests/bsort_library_smoke.cpp)
  target_link_libraries(bsort_library_smoke PRIVATE bsort_lib)

  add_test(
    NAME bsort_library_smoke
    COMMAND bsort_library_smoke
  )

  add_executable(bsort_library_golden tests/bsort_library_golden.cpp)
  target_link_libraries(bsort_library_golden PRIVATE bsort_lib)

  add_test(
    NAME bsort_library_golden
    COMMAND bsort_library_golden
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/input
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/output
  )

  add_executable(bsort_package_smoke tests/bsort_library_smoke.cpp)
  target_link_libraries(bsort_package_smoke PRIVATE bsort::bsort)

  add_test(
    NAME bsort_package_smoke
    COMMAND bsort_package_smoke
  )
endif()
