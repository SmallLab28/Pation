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

# Third-party source files
set(THIRD_PARTY_SRC
    third-party/miniz/miniz.c
    third-party/jpge/jpge.cpp
    third-party/ccitt/ccitt.c
)

# Library
add_library(pation_lib STATIC ${PATION_SRC} ${THIRD_PARTY_SRC})

# Prebuilt libjpeg-turbo link directories
if(NOT EMSCRIPTEN)
    # Main executable
    add_executable(pation src/main.cpp)
    target_link_libraries(pation pation_lib ${TURBOJPEG_LIB})

    # Python wrapper
    option(BUILD_PYTHON_WRAPPER "Build Python Extension" OFF)
    if(BUILD_PYTHON_WRAPPER)
        find_package(Python3 COMPONENTS Development REQUIRED)
        
        Python3_add_library(pation_python MODULE wrapper-python/wrapper-python.cpp)
        set_target_properties(pation_python PROPERTIES OUTPUT_NAME "pation")
        target_link_libraries(pation_python PRIVATE pation_lib ${TURBOJPEG_LIB})
    endif()
else()
    # WASM build (Emscripten)
    add_executable(pation_wasm wrapper-wasm/wrapper_wasm.cpp)
    set_target_properties(pation_wasm PROPERTIES OUTPUT_NAME "pation")
    
    # Emscripten specific flags
    set_target_properties(pation_wasm PROPERTIES LINK_FLAGS "-s WASM=1 -s ALLOW_MEMORY_GROWTH=1 --bind -s EXPORTED_RUNTIME_METHODS=['FS'] -s MODULARIZE=1 -s EXPORT_NAME=createPationModule")
    
    target_link_libraries(pation_wasm pation_lib ${TURBOJPEG_LIB})
endif()
