cmake_minimum_required(VERSION 3.10)

project(GLPS VERSION 1.0 LANGUAGES C)

# -------------------------------------------------
# Output directories
# -------------------------------------------------
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# -------------------------------------------------
# Options
# -------------------------------------------------
option(GLPS_INCLUDE_VULKAN "Enable Vulkan support in GLPS" OFF)
option(GLPS_FORCE_WAYLAND "Force Wayland backend" OFF)

# -------------------------------------------------
# Vulkan (optional)
# -------------------------------------------------
if(GLPS_INCLUDE_VULKAN)
    find_package(Vulkan REQUIRED)
    add_compile_definitions(GLPS_USE_VULKAN)
endif()

# =================================================
# Windows
# =================================================
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

# =================================================
# Linux (Wayland / X11)
# =================================================
elseif(UNIX AND NOT APPLE)

    message(STATUS "Configuring Linux backend")

    find_package(PkgConfig REQUIRED)
    find_package(EGL REQUIRED)

    # -------------------------------
    # Wayland detection
    # -------------------------------
    pkg_check_modules(WAYLAND wayland-client wayland-egl)

    set(GLPS_USE_WAYLAND_BACKEND OFF)
    if(GLPS_FORCE_WAYLAND OR (WAYLAND_FOUND AND EGL_FOUND))
        set(GLPS_USE_WAYLAND_BACKEND ON)
    endif()

    # -------------------------------
    # Wayland backend
    # -------------------------------
    if(GLPS_USE_WAYLAND_BACKEND)

        message(STATUS "Building for Linux Wayland")

        file(GLOB XDG_GLPS_SOURCES "src/xdg/*.c")
        file(GLOB XDG_GLPS_HEADERS "internal/xdg/*.h")

        add_library(${PROJECT_NAME} SHARED
            src/glps_wayland.c
            src/glps_egl_context.c
            src/glps_window_manager.c
            src/utils/logger/pico_logger.c
            src/glps_thread.c
            src/glps_audio_stream.c
            src/glps_timer.c
            ${XDG_GLPS_SOURCES}
            ${XDG_GLPS_HEADERS}
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
        )

        target_link_libraries(${PROJECT_NAME} PRIVATE
            EGL::EGL
            ${WAYLAND_LIBRARIES}
            xkbcommon
            pthread
            asound
            rt
            m
        )

    # -------------------------------
    # X11 fallback
    # -------------------------------
    else()

        message(STATUS "Building for X11")

        find_package(X11 REQUIRED)

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

        target_link_libraries(${PROJECT_NAME} PRIVATE
            X11::X11
            EGL::EGL
            pthread
            asound
            rt
        )
    endif()

    # -------------------------------
    # Common Linux includes
    # -------------------------------
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
        /usr/include/glib-2.0
        /usr/lib/glib-2.0/include
        /usr/include/pixman-1
        /usr/include/libdrm
        /usr/include
        /usr/include/alsa
    )

else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# =================================================
# Common include paths
# =================================================
target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/GLPS>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/internal
)

# =================================================
# Install
# =================================================
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

# =================================================
# Library properties
# =================================================
set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

enable_testing()
