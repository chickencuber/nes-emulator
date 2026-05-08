cmake_minimum_required(VERSION 3.16)
project(nes_emulator C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL3 REQUIRED)

add_executable(nes
    src/main.c
    src/util.c
    src/cpu.c
)

target_compile_definitions(nes PRIVATE
    $<$<CONFIG:Debug>:DEBUG_BUILD=1>
)
target_include_directories(nes PRIVATE 
    ${SDL3_INCLUDE_DIRS}
    include
)
target_link_libraries(nes PRIVATE ${SDL3_LIBRARIES})
