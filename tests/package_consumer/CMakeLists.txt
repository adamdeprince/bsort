cmake_minimum_required(VERSION 3.22)

project(bsort_package_consumer LANGUAGES CXX)

find_package(bsort CONFIG REQUIRED)

add_executable(package_consumer ../bsort_library_smoke.cpp)
target_link_libraries(package_consumer PRIVATE bsort::bsort)
