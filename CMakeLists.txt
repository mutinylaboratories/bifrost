cmake_minimum_required(VERSION 3.15 FATAL_ERROR)
project(bifrost CXX C)

find_package(Qt6 COMPONENTS Core Gui Widgets Svg REQUIRED)

file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
qt_add_resources(BIFROST_RESOURCES resources.qrc OPTIONS --no-zstd)
add_library(bifrost SHARED ${SOURCES} ${BIFROST_RESOURCES})
set_target_properties(bifrost PROPERTIES AUTOMOC ON)

if(NOT BN_API_BUILD_EXAMPLES AND NOT BN_INTERNAL_BUILD)
    find_path(BN_API_PATH NAMES binaryninjaapi.h
        HINTS
            ../binaryninja-api
            ../../binaryninja-api
            binaryninjaapi
            $ENV{BN_API_PATH}
        REQUIRED)
    add_subdirectory(${BN_API_PATH} api)
endif()

target_link_libraries(bifrost binaryninjaui Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)

set_target_properties(bifrost PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out/bin)

if(BN_INTERNAL_BUILD)
    ui_plugin_rpath(bifrost)
endif()

bn_install_plugin(bifrost)

add_subdirectory(tests)
