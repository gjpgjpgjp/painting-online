cmake_minimum_required(VERSION 3.16)

project(painting_online VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加 Network 组件
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
)

# 禁用 mainwindow.cpp 的自动 moc 处理（因为手动包含 .moc 文件）
set_source_files_properties(mainwindow.cpp PROPERTIES SKIP_AUTOMOC ON)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(painting_online
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        DrawingCore.h
        DrawingCore.cpp
        mainwindow.ui
        NetworkManager.h
        NetworkManager.cpp
        CanvasDatabase.h
        CanvasDatabase.cpp
        RoomServer.h
        RoomServer.cpp
        CanvasExporter.h
        CanvasExporter.cpp
        BrushPreset.h
        BrushPreset.cpp



    )
else()
    if(ANDROID)
        add_library(painting_online SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(painting_online
            ${PROJECT_SOURCES}
        )
    endif()
endif()

# 链接 Network 模块
target_link_libraries(painting_online PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)

if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.painting_online)
endif()
set_target_properties(painting_online PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS painting_online
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(painting_online)
endif()