cmake_minimum_required(VERSION 3.10)
project(Pation C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -flto")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -flto")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O3 -flto")

# Include directories
if(EMSCRIPTEN)
    include_directories(
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/third-party
        ${CMAKE_SOURCE_DIR}/third-party/libjpeg-turbo/src
        ${CMAKE_SOURCE_DIR}/third-party/libjpeg-turbo/build_wasm
    )
    set(TURBOJPEG_LIB ${CMAKE_SOURCE_DIR}/third-party/libjpeg-turbo/build_wasm/libturbojpeg.a)
else()
    include_directories(
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/third-party
        ${CMAKE_SOURCE_DIR}/third-party/libjpeg-turbo/src
        ${CMAKE_SOURCE_DIR}/third-party/libjpeg-turbo/build_ucrt
    )
    set(TURBOJPEG_LIB ${CMAKE_SOURCE_DIR}/third-party/libjpeg-turbo/build_ucrt/libturbojpeg.a)
endif()

# Core source files
file(GLOB_RECURSE PATION_SRC 
    "src/*.c" 
    "src/*.cpp"
)
# Exclude main.cpp from library sources
list(FILTER PATION_SRC EXCLUDE REGEX "main\\.cpp$")

# OpenJPEG configuration
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_CODEC OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_JPIP OFF CACHE BOOL "" FORCE)
set(BUILD_VIEWER OFF CACHE BOOL "" FORCE)
set(BUILD_JAVA OFF CACHE BOOL "" FORCE)
add_subdirectory(third-party/openjpeg-2.5.4 EXCLUDE_FROM_ALL)

# Reset output directories so OpenJPEG does not override project output paths
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}" CACHE PATH "" FORCE)
set(LIBRARY_OUTPUT_PATH "${CMAKE_BINARY_DIR}" CACHE PATH "" FORCE)

include_directories(
    ${CMAKE_SOURCE_DIR}/third-party/openjpeg-2.5.4/src/lib/openjp2
    ${CMAKE_BINARY_DIR}/third-party/openjpeg-2.5.4/src/lib/openjp2
)

# Third-party source files
set(THIRD_PARTY_SRC
    third-party/miniz/miniz.c
    third-party/jpge/jpge.cpp
    third-party/ccitt/ccitt.c
)

# Library
add_library(pation_lib STATIC ${PATION_SRC} ${THIRD_PARTY_SRC})
target_compile_definitions(pation_lib PUBLIC OPJ_STATIC)

# Prebuilt libjpeg-turbo link directories
if(NOT EMSCRIPTEN)
    # Main executable
    add_executable(pation src/main.cpp)
    set_target_properties(pation PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_link_libraries(pation pation_lib openjp2 ${TURBOJPEG_LIB})

    # Python wrapper
    option(BUILD_PYTHON_WRAPPER "Build Python Extension" OFF)
    if(BUILD_PYTHON_WRAPPER)
        find_package(Python3 COMPONENTS Development REQUIRED)
        
        Python3_add_library(pation_python MODULE wrapper-python/wrapper-python.cpp)
        set_target_properties(pation_python PROPERTIES OUTPUT_NAME "pation")
        target_link_libraries(pation_python PRIVATE pation_lib openjp2 ${TURBOJPEG_LIB})
    endif()
else()
    # WASM build (Emscripten)
    add_executable(pation_wasm wrapper-wasm/wrapper_wasm.cpp)
    set_target_properties(pation_wasm PROPERTIES 
        OUTPUT_NAME "pation"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        LINK_FLAGS "-s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s MAXIMUM_MEMORY=4GB --bind -s EXPORTED_RUNTIME_METHODS=['FS'] -s MODULARIZE=1 -s EXPORT_NAME=createPationModule"
    )
    
    target_link_libraries(pation_wasm pation_lib openjp2 ${TURBOJPEG_LIB})
endif()
