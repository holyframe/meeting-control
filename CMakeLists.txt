cmake_minimum_required(VERSION 3.24)
project(MeetingController VERSION 0.4.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Meeting Controller currently targets Windows 10/11 only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

add_executable(MeetingController WIN32
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/UiaWorker.h
    src/UiaWorker.cpp
    src/ChromeWindowEnumerator.h
    src/ChromeWindowEnumerator.cpp
    src/UiaTreeInspector.h
    src/UiaTreeInspector.cpp
)

target_include_directories(MeetingController PRIVATE src)

target_link_libraries(MeetingController PRIVATE
    Qt6::Core
    Qt6::Widgets
    ole32
    oleaut32
    user32
    uiautomationcore
)

target_compile_definitions(MeetingController PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(MeetingController PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()
