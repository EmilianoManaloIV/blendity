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
# Single self-contained executables on Windows (static CRT; the DLL runtime when
# Blender's libraries are used, below).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- Blender's prebuilt libraries (optional) ----------------------------------
# Same detection as build.bat / build.sh: when ../blender/lib/<platform> exists
# (projects.blender.org/blender/lib-<platform>), each library found enables its
# BL_WITH_* backend; otherwise the dependency-free code is used.
#   -DBLENDITY_NO_LIBS=ON   force the dependency-free build
#   -DBLENDITY_LIBDIR=path  use libraries from elsewhere
option(BLENDITY_NO_LIBS "Build without Blender's prebuilt libraries" OFF)
if(WIN32)
  set(_bl_plat windows_x64)
elseif(APPLE)
  set(_bl_plat macos_arm64)
else()
  set(_bl_plat linux_x64)
endif()
set(BLENDITY_LIBDIR "${CMAKE_SOURCE_DIR}/../blender/lib/${_bl_plat}" CACHE PATH "Blender library folder")
set(BLENDITY_USE_LIBS OFF)
if(NOT BLENDITY_NO_LIBS AND EXISTS "${BLENDITY_LIBDIR}/tbb/include")
  set(BLENDITY_USE_LIBS ON)
endif()
set(BL_LIB_DEFS "")
set(BL_LIB_INCS "")
set(BL_LIB_LINK "")
set(BL_LIB_DIRS "")
# bl_lib(<folder> <define> INC <dirs...> WIN <libs...> UNIX <libs...>)  paths relative to the lib folder
function(bl_lib folder define)
  cmake_parse_arguments(A "" "" "INC;WIN;UNIX" ${ARGN})
  if(NOT EXISTS "${BLENDITY_LIBDIR}/${folder}")
    return()
  endif()
  set(_defs ${BL_LIB_DEFS} ${define})
  set(_incs ${BL_LIB_INCS})
  foreach(i ${A_INC})
    list(APPEND _incs "${BLENDITY_LIBDIR}/${i}")
  endforeach()
  set(_link ${BL_LIB_LINK})
  if(WIN32)
    set(_libs ${A_WIN})
  else()
    set(_libs ${A_UNIX})
  endif()
  foreach(l ${_libs})
    string(REPLACE "@SO@" "${CMAKE_SHARED_LIBRARY_SUFFIX}" l "${l}")
    list(APPEND _link "${BLENDITY_LIBDIR}/${l}")
  endforeach()
  set(BL_LIB_DEFS ${_defs} PARENT_SCOPE)
  set(BL_LIB_INCS ${_incs} PARENT_SCOPE)
  set(BL_LIB_LINK ${_link} PARENT_SCOPE)
  set(BL_LIB_DIRS ${BL_LIB_DIRS} ${folder} PARENT_SCOPE)
endfunction()
if(BLENDITY_USE_LIBS)
  bl_lib(tbb BL_WITH_TBB INC tbb/include WIN tbb/lib/tbb12.lib UNIX tbb/lib/libtbb@SO@)
  bl_lib(embree BL_WITH_EMBREE INC embree/include WIN embree/lib/embree4.lib UNIX embree/lib/libembree4@SO@)
  bl_lib(openimagedenoise BL_WITH_OIDN INC openimagedenoise/include
         WIN openimagedenoise/lib/OpenImageDenoise.lib UNIX openimagedenoise/lib/libOpenImageDenoise@SO@)
  bl_lib(eigen BL_WITH_EIGEN INC eigen/include/eigen3)
  bl_lib(opensubdiv BL_WITH_OPENSUBDIV INC opensubdiv/include WIN opensubdiv/lib/osdCPU.lib UNIX opensubdiv/lib/libosdCPU@SO@)
  bl_lib(openexr BL_WITH_OPENEXR INC openexr/include openexr/include/OpenEXR imath/include imath/include/Imath
         WIN openexr/lib/OpenEXR.lib openexr/lib/OpenEXRCore.lib openexr/lib/Iex.lib openexr/lib/IlmThread.lib imath/lib/Imath.lib
         UNIX openexr/lib/libOpenEXR@SO@ openexr/lib/libOpenEXRCore@SO@ openexr/lib/libIex@SO@ openexr/lib/libIlmThread@SO@
              imath/lib/libImath@SO@)
  bl_lib(opencolorio BL_WITH_OCIO INC opencolorio/include WIN opencolorio/lib/OpenColorIO.lib UNIX opencolorio/lib/libOpenColorIO@SO@)
  bl_lib(manifold BL_WITH_MANIFOLD INC manifold/include WIN manifold/lib/manifold.lib UNIX manifold/lib/libmanifold.a)
  bl_lib(meshoptimizer BL_WITH_MESHOPT INC meshoptimizer/include
         WIN meshoptimizer/lib/meshoptimizer.lib UNIX meshoptimizer/lib/libmeshoptimizer@SO@)
  bl_lib(openpgl BL_WITH_OPENPGL INC openpgl/include WIN openpgl/lib/openpgl.lib UNIX openpgl/lib/libopenpgl.a)
  bl_lib(jolt BL_WITH_JOLT INC jolt/include WIN jolt/lib/Jolt.lib UNIX jolt/lib/libJolt@SO@)
  bl_lib(jpeg BL_WITH_LIBJPEG INC jpeg/include WIN jpeg/lib/libjpeg.lib UNIX jpeg/lib/libjpeg.a)
  bl_lib(png BL_WITH_LIBPNG INC png/include zlib/include WIN png/lib/libpng.lib zlib/lib/libz_st.lib
         UNIX png/lib/libpng.a zlib/lib/libz.a)
  bl_lib(zstd BL_WITH_ZSTD INC zstd/include WIN zstd/lib/zstd_static.lib UNIX zstd/lib/libzstd.a)
  if(EXISTS "${BLENDITY_LIBDIR}/vulkan/include")  # GPU rendering; the loader comes with the driver
    bl_lib(shaderc BL_WITH_VULKAN INC shaderc/include vulkan/include WIN shaderc/lib/shaderc_shared.lib UNIX shaderc/lib/libshaderc_combined.a)
  endif()
  # Static libraries (manifold, openpgl) use TBB: list it again after them.
  if(NOT WIN32)
    list(APPEND BL_LIB_LINK "${BLENDITY_LIBDIR}/tbb/lib/libtbb${CMAKE_SHARED_LIBRARY_SUFFIX}")
  endif()
  # ABI settings the libraries were built with (blender/build_files/build_environment/cmake).
  # Jolt enables floating-point exceptions only in its MSVC builds; a mismatch
  # makes its version check abort at start-up.
  list(APPEND BL_LIB_DEFS MANIFOLD_PAR=1 JPH_SHARED_LIBRARY JPH_DOUBLE_PRECISION JPH_CROSS_PLATFORM_DETERMINISTIC
       JPH_USE_CPU_COMPUTE JPH_OBJECT_STREAM)
  if(WIN32)
    list(APPEND BL_LIB_DEFS IMATH_DLL OPENEXR_DLL JPH_FLOATING_POINT_EXCEPTIONS_ENABLED JPH_USE_SSE4_1 JPH_USE_SSE4_2)
  elseif(NOT APPLE)
    list(APPEND BL_LIB_DEFS JPH_USE_SSE4_1 JPH_USE_SSE4_2)
  endif()
  # The libraries use the DLL / shared C++ runtime.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  message(STATUS "Blendity: using Blender libraries from ${BLENDITY_LIBDIR}: ${BL_LIB_DIRS}")
else()
  message(STATUS "Blendity: dependency-free build (BLENDITY_NO_LIBS, or no ${BLENDITY_LIBDIR})")
endif()

file(GLOB BLENDITY_SOURCES CONFIGURE_DEPENDS
  src/core/*.cpp src/image/*.cpp src/render/*.cpp src/scene/*.cpp src/editor/*.cpp src/research/*.cpp src/deps/*.cpp)
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
if(BLENDITY_USE_LIBS)
  target_compile_definitions(blendity_engine PUBLIC ${BL_LIB_DEFS})
  target_include_directories(blendity_engine SYSTEM PUBLIC ${BL_LIB_INCS})
  target_link_libraries(blendity_engine PUBLIC ${BL_LIB_LINK})
  if(NOT WIN32 AND NOT APPLE)
    target_compile_options(blendity_engine PUBLIC -msse4.2 -mpopcnt)
    # Transitive runtime libraries (OpenEXR -> OpenJPH, Embree -> SYCL runtime),
    # and the deployed copies in ./lib next to the executables.
    target_link_options(blendity_engine PUBLIC
      "-Wl,-rpath-link,${BLENDITY_LIBDIR}/openjph/lib:${BLENDITY_LIBDIR}/dpcpp/lib:${BLENDITY_LIBDIR}/tbb/lib:${BLENDITY_LIBDIR}/imath/lib"
      "-Wl,-rpath,\$ORIGIN/lib")
  endif()
  # Run from the build folder: the libraries' runtime files sit next to the executables.
  if(WIN32)
    file(GLOB _bl_dlls "${BLENDITY_LIBDIR}/*/bin/*.dll")
    foreach(d ${_bl_dlls})
      get_filename_component(n "${d}" NAME)
      get_filename_component(dir "${d}" DIRECTORY)
      get_filename_component(dir "${dir}" DIRECTORY)
      get_filename_component(pkg "${dir}" NAME)
      list(FIND BL_LIB_DIRS "${pkg}" _used)
      if((_used GREATER -1 OR pkg MATCHES "^(imath|openjph|dpcpp)$") AND NOT n MATCHES "(_d\.|_d_|debug|_cuda|_hip|_sycl|eigen_|malloc_proxy)")
        file(COPY "${d}" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
      endif()
    endforeach()
  endif()
endif()
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
