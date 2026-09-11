# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)

project(dolphin-image-converter
    VERSION 0.2.0
    DESCRIPTION "Batch image resize, rotate and conversion integration for KDE Dolphin"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets LinguistTools)

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

qt_add_translations(dolphin-image-converter
    TS_FILES translations/dolphin-image-converter_ro.ts
    RESOURCE_PREFIX "/i18n"
)

include(GNUInstallDirs)

configure_file(
    data/dolphin-image-converter.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/dolphin-image-converter.desktop
    @ONLY
)

install(TARGETS dolphin-image-converter
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/dolphin-image-converter.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/kio/servicemenus
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)
