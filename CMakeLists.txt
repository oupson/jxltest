cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

include_directories("includes")

add_subdirectory(third_party)

project(jxl-test CXX)

add_executable(jxl-test src/main.cpp)

target_link_libraries(jxl-test jxl jxl_threads skcms-interface png fmt)
