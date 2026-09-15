#SMALLAB28 SOFTWARE


cmake_minimum_required(VERSION 3.10)
project(Pation)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(app 
    src/main.cpp 
    src/pation/document.c
    src/pation/context.c
    src/pdf/xref.cpp
    src/pation/state.c 
    src/pdf/page.cpp
    src/pdf/parser.cpp
)

target_include_directories(
    app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# mark
add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/data
    $<TARGET_FILE_DIR:app>/data
)
