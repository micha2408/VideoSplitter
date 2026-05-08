cmake_minimum_required(VERSION 3.24)
project(VlcFrameGrabber LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

qt_standard_project_setup()

# VLC SDK
set(VLC_SDK_PATH "D:/Qt/vlcLibrary/sdk" CACHE PATH "VLC SDK path")

if(NOT EXISTS "${VLC_SDK_PATH}/include/vlc/libvlc.h")
    message(FATAL_ERROR "VLC SDK nicht gefunden! Überprüfe VLC_SDK_PATH: ${VLC_SDK_PATH}")
endif()

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
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${VLC_SDK_PATH}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    "${VLC_SDK_PATH}/lib/libvlc.lib"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    VLC_SDK_PATH="${VLC_SDK_PATH}"
)

# VLC Bibliotheken und Plugins ins Build-Verzeichnis kopieren
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VLC_SDK_PATH}/lib/libvlc.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libvlc.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VLC_SDK_PATH}/lib/libvlccore.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libvlccore.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${VLC_SDK_PATH}/lib/plugins"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins"
    COMMENT "Kopiere VLC Bibliotheken und Plugins"
)

message(STATUS "VLC SDK: ${VLC_SDK_PATH}")
