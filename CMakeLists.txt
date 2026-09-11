# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)

project(dolphin-image-converter
    VERSION 0.2.3
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

# Qt 6.7+ defers automatic source-target collection in qt_add_translations().
# On some Qt/CMake combinations that deferred collection can misidentify words
# from target metadata as target names (for example "Image").  Specify both
# the resource target and the translation source target explicitly and request
# an immediate call.  Keep the Qt 6.5/6.6-compatible signature as a fallback.
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.7.0")
    qt_add_translations(
        TARGETS dolphin-image-converter
        SOURCE_TARGETS dolphin-image-converter
        TS_FILES translations/dolphin-image-converter_ro.ts
        RESOURCE_PREFIX "/i18n"
        IMMEDIATE_CALL
    )
else()
    qt_add_translations(dolphin-image-converter
        TS_FILES translations/dolphin-image-converter_ro.ts
        RESOURCE_PREFIX "/i18n"
    )
endif()

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
