cmake_minimum_required(VERSION 3.10)

project(GLPS VERSION 1.0 LANGUAGES C)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(GLPS_INCLUDE_VULKAN "Enable Vulkan support in GLPS" OFF)
option(GLPS_FORCE_WAYLAND "Force Wayland backend" OFF)

if(GLPS_INCLUDE_VULKAN)
    find_package(Vulkan REQUIRED)
    add_compile_definitions(GLPS_USE_VULKAN)
endif()

if(WIN32)

    message(STATUS "Building for Windows")

    add_library(${PROJECT_NAME} SHARED
        src/glps_wgl_context.c
        src/glps_win32.c
        src/glps_window_manager.c
        src/utils/logger/pico_logger.c
        src/glps_timer.c
    )

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        GLPS_USE_WIN32
        _CRT_SECURE_NO_WARNINGS
    )

    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        opengl32
        gdi32
        user32
    )

elseif(UNIX AND NOT APPLE)

    message(STATUS "Configuring Linux backend")

    find_package(PkgConfig REQUIRED)

    pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl)
    pkg_check_modules(EGL REQUIRED egl)
    pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)

    find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner)
    if(NOT WAYLAND_SCANNER_EXECUTABLE)
        message(FATAL_ERROR "wayland-scanner not found")
    endif()

    execute_process(
        COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=pkgdatadir wayland-protocols
        OUTPUT_VARIABLE WAYLAND_PROTOCOLS_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT WAYLAND_PROTOCOLS_DIR)
        message(FATAL_ERROR "Could not determine wayland-protocols directory")
    endif()

    message(STATUS "Wayland protocols directory: ${WAYLAND_PROTOCOLS_DIR}")

    set(XDG_PROTOCOL_DIR ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell)
    set(XDG_PROTOCOL_XML ${XDG_PROTOCOL_DIR}/xdg-shell.xml)

    if(NOT EXISTS ${XDG_PROTOCOL_XML})
        message(FATAL_ERROR "xdg-shell.xml not found at ${XDG_PROTOCOL_XML}")
    endif()

    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/xdg)

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/xdg/xdg-shell.h
        COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header ${XDG_PROTOCOL_XML} ${CMAKE_BINARY_DIR}/xdg/xdg-shell.h
        DEPENDS ${XDG_PROTOCOL_XML}
        COMMENT "Generating xdg-shell.h"
    )

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/xdg/xdg-shell-protocol.c
        COMMAND ${WAYLAND_SCANNER_EXECUTABLE} public-code ${XDG_PROTOCOL_XML} ${CMAKE_BINARY_DIR}/xdg/xdg-shell-protocol.c
        DEPENDS ${XDG_PROTOCOL_XML}
        COMMENT "Generating xdg-shell-protocol.c"
    )

    set(XDG_DECORATION_PROTOCOL_DIR ${WAYLAND_PROTOCOLS_DIR}/unstable/xdg-decoration)
    set(XDG_DECORATION_PROTOCOL_XML ${XDG_DECORATION_PROTOCOL_DIR}/xdg-decoration-unstable-v1.xml)

    if(EXISTS ${XDG_DECORATION_PROTOCOL_XML})
        add_custom_command(
            OUTPUT ${CMAKE_BINARY_DIR}/xdg/xdg-decoration-unstable-v1.h
            COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header ${XDG_DECORATION_PROTOCOL_XML} ${CMAKE_BINARY_DIR}/xdg/xdg-decoration-unstable-v1.h
            DEPENDS ${XDG_DECORATION_PROTOCOL_XML}
            COMMENT "Generating xdg-decoration-unstable-v1.h"
        )
        set(XDG_DECORATION_HEADER ${CMAKE_BINARY_DIR}/xdg/xdg-decoration-unstable-v1.h)
    endif()

    set(WLR_DATA_CONTROL_PROTOCOL_DIR ${WAYLAND_PROTOCOLS_DIR}/unstable/wlr-data-control)
    set(WLR_DATA_CONTROL_PROTOCOL_XML ${WLR_DATA_CONTROL_PROTOCOL_DIR}/wlr-data-control-unstable-v1.xml)

    if(EXISTS ${WLR_DATA_CONTROL_PROTOCOL_XML})
        add_custom_command(
            OUTPUT ${CMAKE_BINARY_DIR}/xdg/wlr-data-control-unstable-v1.h
            COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header ${WLR_DATA_CONTROL_PROTOCOL_XML} ${CMAKE_BINARY_DIR}/xdg/wlr-data-control-unstable-v1.h
            DEPENDS ${WLR_DATA_CONTROL_PROTOCOL_XML}
            COMMENT "Generating wlr-data-control-unstable-v1.h"
        )
        set(WLR_DATA_CONTROL_HEADER ${CMAKE_BINARY_DIR}/xdg/wlr-data-control-unstable-v1.h)
    endif()

    set(GENERATED_XDG_HEADERS
        ${CMAKE_BINARY_DIR}/xdg/xdg-shell.h
        ${XDG_DECORATION_HEADER}
        ${WLR_DATA_CONTROL_HEADER}
    )

    set(GENERATED_XDG_SOURCES
        ${CMAKE_BINARY_DIR}/xdg/xdg-shell-protocol.c
    )

    set(CURRENT_SESSION_TYPE "auto")

    if(DEFINED ENV{XDG_SESSION_TYPE})
        set(CURRENT_SESSION_TYPE $ENV{XDG_SESSION_TYPE})
    endif()

    set(USE_WAYLAND OFF)

    if(GLPS_FORCE_WAYLAND OR CURRENT_SESSION_TYPE STREQUAL "wayland")
        set(USE_WAYLAND ON)
    endif()

    if(USE_WAYLAND)

        message(STATUS "Building Wayland backend")

        add_library(${PROJECT_NAME} SHARED
            src/glps_wayland.c
            src/glps_egl_context.c
            src/glps_window_manager.c
            src/utils/logger/pico_logger.c
            src/glps_thread.c
            src/glps_audio_stream.c
            src/glps_timer.c
            ${GENERATED_XDG_SOURCES}
        )

        target_compile_definitions(${PROJECT_NAME} PRIVATE
            GLPS_USE_WAYLAND
        )

        target_compile_options(${PROJECT_NAME} PRIVATE
            -Wall
            -Wextra
            -Wno-unused-variable
            -Wno-unused-parameter
        )

        target_include_directories(${PROJECT_NAME} PRIVATE
            ${WAYLAND_INCLUDE_DIRS}
            ${EGL_INCLUDE_DIRS}
            ${CMAKE_BINARY_DIR}
        )

        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${WAYLAND_LIBRARIES}
            ${EGL_LIBRARIES}
            GLESv2
            xkbcommon
            pthread
            asound
            rt
            m
        )

        add_dependencies(${PROJECT_NAME} xdg-shell-header)

        add_custom_target(xdg-shell-header DEPENDS ${GENERATED_XDG_HEADERS})

    else()

        message(STATUS "Building X11 backend")

        find_package(X11 REQUIRED)
        pkg_check_modules(EGL REQUIRED egl)

        add_library(${PROJECT_NAME} SHARED
            src/glps_x11.c
            src/glps_egl_context.c
            src/glps_window_manager.c
            src/utils/logger/pico_logger.c
            src/glps_thread.c
            src/glps_audio_stream.c
            src/glps_timer.c
        )

        target_compile_definitions(${PROJECT_NAME} PRIVATE
            GLPS_USE_X11
        )

        target_compile_options(${PROJECT_NAME} PRIVATE
            -Wall
            -Wextra
            -Wno-unused-variable
            -Wno-unused-parameter
        )

        target_include_directories(${PROJECT_NAME} PRIVATE
            ${X11_INCLUDE_DIRS}
            ${EGL_INCLUDE_DIRS}
        )

        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${X11_LIBRARIES}
            ${EGL_LIBRARIES}
            GLESv2
            pthread
            asound
            rt
            m
        )

    endif()

    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
        /usr/include/glib-2.0
        /usr/lib/glib-2.0/include
        /usr/include/pixman-1
        /usr/include/libdrm
        /usr/include/alsa
    )

else()
    message(FATAL_ERROR "Unsupported platform")
endif()

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/GLPS>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/internal
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION include/GLPS)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/internal/ DESTINATION include/GLPS)

install(TARGETS ${PROJECT_NAME}
    EXPORT GLPSConfig
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(EXPORT GLPSConfig
    FILE GLPSConfig.cmake
    NAMESPACE GLPS::
    DESTINATION lib/cmake/GLPS
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

enable_testing()