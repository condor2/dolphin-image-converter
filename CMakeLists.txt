cmake_minimum_required(VERSION 3.20)

project(dolphin-image-converter
    VERSION 0.1.1
    DESCRIPTION "Batch image resize, rotate and conversion integration for KDE Dolphin"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

add_executable(dolphin-image-converter
    src/main.cpp
    src/imageconverterdialog.cpp
    src/imageconverterdialog.h
    src/imagemagickrunner.cpp
    src/imagemagickrunner.h
)

target_link_libraries(dolphin-image-converter PRIVATE Qt6::Widgets)

target_compile_definitions(dolphin-image-converter PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

include(GNUInstallDirs)

install(TARGETS dolphin-image-converter
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# KDE requires locally installed service menu files to be executable/authorized.
install(PROGRAMS data/dolphin-image-converter.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/kio/servicemenus
)
