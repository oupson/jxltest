cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

include_directories("includes")

add_subdirectory(third_party)

add_definitions( -DMAGICKCORE_QUANTUM_DEPTH=16 )
add_definitions( -DMAGICKCORE_HDRI_ENABLE=1 )
find_package(ImageMagick COMPONENTS Magick++ MagickCore MagickWand)

include_directories(${ImageMagick_INCLUDE_DIRS})

project(jxl-test CXX)

add_executable(jxl-test src/main.cpp)

target_link_libraries(jxl-test jxl jxl_threads skcms-interface ${ImageMagick_LIBRARIES} fmt)
