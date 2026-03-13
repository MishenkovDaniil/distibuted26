cmake_minimum_required(VERSION 3.16)
project(integral-distributed C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(integral_parser STATIC
    src/parser.c
)

add_executable(master
    src/master/master.c
    src/master/master.h
)

add_executable(worker
    src/worker/worker.c
    src/worker/worker.h
)

target_link_libraries(master
    PRIVATE
        m
        pthread
        integral_parser
)

target_link_libraries(worker
    PRIVATE
        m
        integral_parser
)

install(TARGETS master worker
        RUNTIME DESTINATION bin)