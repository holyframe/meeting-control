cmake_minimum_required(VERSION 3.24)
project(MeetingController VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Meeting Controller currently targets Windows only.")
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
    src/CdpPipe.h
    src/CdpPipe.cpp
    src/CdpClient.h
    src/CdpClient.cpp
    src/ZoomInspector.h
    src/ZoomInspector.cpp
)

target_include_directories(MeetingController PRIVATE src)

target_link_libraries(MeetingController PRIVATE
    Qt6::Core
    Qt6::Widgets
)

target_compile_definitions(MeetingController PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(MeetingController PRIVATE /W4 /permissive- /EHsc)
endif()
