cmake_minimum_required(VERSION 3.10)

project(logger C)

set(CMAKE_C_STANDARD 11)

enable_testing()

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

add_executable(regression_tests
    tests/regression/regression_tests.c
    config.c
    cty.c
    qso.c
    export.c
    stats.c
    maidenhead.c
)

target_include_directories(regression_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(regression_tests
    m
)

add_test(NAME regression_tests COMMAND regression_tests)

add_executable(unit_tests
    tests/unit/unit_tests.c
    config.c
    cty.c
    qso.c
    export.c
    stats.c
    maidenhead.c
    dxcluster.c
)

target_include_directories(unit_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(unit_tests
    pthread
    m
)

add_test(NAME unit_tests COMMAND unit_tests)
