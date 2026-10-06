# Blendity - optional CMake build (the build.bat / build.sh scripts need no CMake).
#   cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
cmake_minimum_required(VERSION 3.16)
project(Blendity VERSION 0.10.0 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Single self-contained executables on Windows (static CRT).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

file(GLOB BLENDITY_SOURCES CONFIGURE_DEPENDS
  src/core/*.cpp src/image/*.cpp src/render/*.cpp src/scene/*.cpp src/editor/*.cpp src/research/*.cpp)
# Blender's own bundled dependencies (extern/README.md)
list(APPEND BLENDITY_SOURCES extern/ufbx/ufbx.c extern/sky/source/sky_hosek.cpp)

if(WIN32)
  list(APPEND BLENDITY_SOURCES src/platform/platform_win32.cpp)
elseif(APPLE)
  list(APPEND BLENDITY_SOURCES src/platform/platform_cocoa.mm)
  set_source_files_properties(src/platform/platform_cocoa.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
  list(APPEND BLENDITY_SOURCES src/platform/platform_x11.cpp)
endif()

add_library(blendity_engine STATIC ${BLENDITY_SOURCES})
find_package(Threads REQUIRED)
target_link_libraries(blendity_engine PUBLIC Threads::Threads)
target_include_directories(blendity_engine PUBLIC extern/sky/include)
if(MSVC)
  target_compile_options(blendity_engine PUBLIC /W3 /utf-8 /permissive- /Zc:__cplusplus)
  target_compile_definitions(blendity_engine PUBLIC NOMINMAX _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(blendity_engine PUBLIC user32 gdi32 shell32)
elseif(APPLE)
  target_compile_options(blendity_engine PUBLIC -Wall -Wextra -Wno-unused-parameter)
  target_link_libraries(blendity_engine PUBLIC "-framework Cocoa" "-framework QuartzCore")
else()
  target_compile_options(blendity_engine PUBLIC -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  find_package(X11 REQUIRED)
  target_include_directories(blendity_engine PUBLIC ${X11_INCLUDE_DIR})
  target_link_libraries(blendity_engine PUBLIC ${X11_LIBRARIES})
endif()

add_executable(Blendity WIN32 src/app/main.cpp)
target_link_libraries(Blendity PRIVATE blendity_engine)
if(MSVC)
  # Keep a normal main() even with the WIN32 (no console) subsystem.
  target_link_options(Blendity PRIVATE /ENTRY:mainCRTStartup)
endif()

add_executable(blendity_stress stress/stress_main.cpp)
target_link_libraries(blendity_stress PRIVATE blendity_engine)

add_executable(blendity_tests tests/test_main.cpp)
target_link_libraries(blendity_tests PRIVATE blendity_engine)

enable_testing()
add_test(NAME unit_tests COMMAND blendity_tests)
add_test(NAME stress_quick COMMAND blendity_stress --quick --out ${CMAKE_BINARY_DIR}/stress_results
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
