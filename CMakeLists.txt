cmake_minimum_required(VERSION 3.24)
project(VideoConverter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

qt_standard_project_setup()

qt_add_executable(${PROJECT_NAME}
    main.cpp
    VideoWidget.cpp
    VideoWidget.h
    RangeSlider.cpp
    RangeSlider.h
    Label.cpp
    Label.h
    ComfyBgRemover.cpp
    ComfyBgRemover.h
    VideoExporter.cpp
    VideoExporter.h
    FrameExtractor.cpp
    FrameExtractor.h
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

if(WIN32)
    set(MYBIN_DIR "C:/Program Files/mybin")
else()
    set(MYBIN_DIR "$ENV{HOME}/bin/mybin")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    DEBUG_POSTFIX "_dbg"
    RUNTIME_OUTPUT_DIRECTORY                "${MYBIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${MYBIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${MYBIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${MYBIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${MYBIN_DIR}"
)
