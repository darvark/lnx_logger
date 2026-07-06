cmake_minimum_required(VERSION 3.10)

project(logger C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

find_package(Threads REQUIRED)
find_library(SQLITE3_LIBRARY NAMES sqlite3 sqlite3.so.0 PATHS /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu REQUIRED)

include_directories(src)

add_executable(logger
    qt_frontend.cpp
    app_controller.c
    db.c
    suggestion.c
    qso.c
    config.c
    cty.c
    dxcluster.c
    export.c
    stats.c
    maidenhead.c
)

target_include_directories(logger
    PRIVATE
    ${CMAKE_SOURCE_DIR}
)

set(_qt_package "")

find_package(Qt6 COMPONENTS Widgets QUIET)
if (Qt6_FOUND)
    set(_qt_package Qt6)
else()
    find_package(Qt5 COMPONENTS Widgets QUIET)
    if (Qt5_FOUND)
        set(_qt_package Qt5)
    endif()
endif()

if (NOT _qt_package)
    message(FATAL_ERROR "Qt Widgets not found. Logger now requires Qt.")
endif()

target_link_libraries(logger
    ${_qt_package}::Widgets
    pthread
    m
    ${SQLITE3_LIBRARY}
)

add_executable(regression_tests
    tests/regression/regression_tests.c
    suggestion.c
    config.c
    cty.c
    qso.c
    db.c
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
    ${SQLITE3_LIBRARY}
)

target_include_directories(regression_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}
)

add_test(NAME regression_tests COMMAND regression_tests)

add_executable(unit_tests
    tests/unit/unit_tests.c
    app_controller.c
    db.c
    suggestion.c
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
    ${SQLITE3_LIBRARY}
)

target_include_directories(unit_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}
)

add_test(NAME unit_tests COMMAND unit_tests)
