cmake_minimum_required(VERSION 3.10)

project(logger C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LOGGER_BUILD_QT "Build Qt frontend" ON)

enable_testing()

find_package(Curses REQUIRED)
find_package(Threads REQUIRED)

include_directories(src)

add_executable(logger
    logger.c
    app_controller.c
    ui.c
    suggestion.c
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

if (LOGGER_BUILD_QT)
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

    if (_qt_package)
        add_executable(logger_qt
            qt_frontend.cpp
            app_controller.c
            suggestion.c
            qso.c
            config.c
            cty.c
            dxcluster.c
            export.c
            stats.c
            maidenhead.c
        )

        target_include_directories(logger_qt
            PRIVATE
            ${CMAKE_SOURCE_DIR}
        )

        target_link_libraries(logger_qt
            ${_qt_package}::Widgets
            pthread
            m
        )
    else()
        message(STATUS "Qt Widgets not found. Skipping logger_qt target.")
    endif()
endif()

add_executable(regression_tests
    tests/regression/regression_tests.c
    suggestion.c
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
    app_controller.c
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
)

add_test(NAME unit_tests COMMAND unit_tests)
