cmake_minimum_required(VERSION 3.10)

project(logger C)

set(CMAKE_C_STANDARD 11)

find_package(Curses REQUIRED)
find_package(Threads REQUIRED)

include_directories(src)

add_executable(logger
    logger.c
    ui.c
    qso.c
    config.c
    cty.c
    dxcluster.c
    export.c
    stats.c
    maidenhead.c
)

target_link_libraries(logger
    ncurses
    pthread
    m
)
