cmake_minimum_required(VERSION 3.21)
project(QtSqliteDemo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Sql)

add_library(record_store src/recordstore.cpp src/recordstore.h)
target_include_directories(record_store PUBLIC src)
target_link_libraries(record_store PUBLIC Qt6::Core Qt6::Sql)

add_library(record_window src/mainwindow.cpp src/mainwindow.h)
target_link_libraries(record_window PUBLIC record_store Qt6::Widgets)

qt_add_executable(qt_sqlite_demo src/main.cpp)
target_link_libraries(qt_sqlite_demo PRIVATE record_window)

add_executable(environment_probe tools/environment_probe.cpp)
target_link_libraries(environment_probe PRIVATE Qt6::Core Qt6::Sql)

if(MINGW)
    foreach(target record_store record_window qt_sqlite_demo environment_probe)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

option(BUILD_TESTING "Build local verification tools" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(demo_tests tests/demo_tests.cpp)
    target_link_libraries(demo_tests PRIVATE record_window Qt6::Test)
    add_test(NAME environment COMMAND environment_probe)
    add_test(NAME records_and_window COMMAND demo_tests -platform offscreen)
    add_test(NAME application_startup COMMAND qt_sqlite_demo -platform offscreen
        --smoke-test "${CMAKE_BINARY_DIR}/startup.png")
    set_tests_properties(environment records_and_window application_startup PROPERTIES TIMEOUT 30)
endif()
