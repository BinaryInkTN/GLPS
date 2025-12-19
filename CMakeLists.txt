cmake_minimum_required(VERSION 3.10)

project(GLPS VERSION 1.0 LANGUAGES C)

# Set output directories
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(GLPS_INCLUDE_VULKAN "Enable Vulkan support in GLPS" OFF)

if(GLPS_INCLUDE_VULKAN)
    add_definitions(-DGLPS_USE_VULKAN)
    find_package(Vulkan REQUIRED)
endif()

# Platform detection and configuration
if(WIN32)
    message(STATUS "Building for Windows")

    # Windows-specific settings
    set(GLPS_SOURCES
        src/glps_wgl_context.c
        src/glps_win32.c
        src/glps_window_manager.c
        src/utils/logger/pico_logger.c
        # src/glps_thread.c
        src/glps_timer.c
    )

    set(GLPS_HEADERS
        internal/glps_wgl_context.h
        include/glps_window_manager.h
       # include/glps_thread.h
        internal/glps_win32.h
        internal/glps_common.h
        internal/utils/logger/pico_logger.h
        include/glps_timer.h
    )

    add_library(${PROJECT_NAME} SHARED ${GLPS_SOURCES} ${GLPS_HEADERS})

    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            GLPS_USE_WIN32
            _CRT_SECURE_NO_WARNINGS
    )

    target_compile_options(${PROJECT_NAME}
        PRIVATE
            -mwindows
            -Wall
            -Wextra
    )

    target_link_libraries(${PROJECT_NAME}
        PRIVATE
            opengl32
            gdi32
            user32
    )

elseif(UNIX AND NOT APPLE)
    # Linux-specific settings
    find_package(PkgConfig REQUIRED)
    execute_process(
        COMMAND bash -c "echo $XDG_SESSION_TYPE"
        OUTPUT_VARIABLE CURRENT_SESSION_TYPE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(CURRENT_SESSION_TYPE STREQUAL "wayland")
        # Wayland
        pkg_check_modules(WAYLAND wayland-client wayland-egl)

        # EGL (use CMake, not pkg-config)
        find_package(EGL)

        if(WAYLAND_FOUND AND EGL_FOUND)
            message(STATUS "Building for Linux Wayland")

        set(GLPS_SOURCES
            src/glps_wayland.c
            src/glps_window_manager.c
            src/utils/logger/pico_logger.c
            src/glps_egl_context.c
            src/glps_thread.c
            src/glps_audio_stream.c
            src/glps_timer.c
        )

        file(GLOB XDG_GLPS_SOURCES "src/xdg/*.c")
        list(APPEND GLPS_SOURCES ${XDG_GLPS_SOURCES})

        set(GLPS_HEADERS
            internal/glps_wayland.h
            include/glps_window_manager.h
            internal/glps_egl_context.h
            internal/glps_common.h
            internal/utils/logger/pico_logger.h
            include/glps_thread.h
            include/glps_audio_stream.h
            internal/utils/audio/dr_mp3.h
            include/glps_timer.h
            internal/utils/uthash/uthash.h
        )

        file(GLOB XDG_GLPS_HEADERS "internal/xdg/*.h")
        list(APPEND GLPS_HEADERS ${XDG_GLPS_HEADERS})

        add_library(${PROJECT_NAME} SHARED ${GLPS_SOURCES} ${GLPS_HEADERS})

        target_compile_definitions(${PROJECT_NAME}
            PRIVATE
                GLPS_USE_WAYLAND
        )

        target_compile_options(${PROJECT_NAME}
            PRIVATE
                -Wall
                -Wextra
                -Wno-unused-variable
                -Wno-unused-parameter
        )

        target_link_libraries(${PROJECT_NAME}
            PRIVATE
                m
                EGL
                ${WAYLAND_LIBRARIES}
                xkbcommon
                pthread
                asound
                rt
        )

        # Install Wayland protocols if needed
        if(NOT EXISTS "/usr/share/wayland-protocols")
            message(STATUS "Installing Wayland protocols...")
            execute_process(
                COMMAND ${PROJECT_SOURCE_DIR}/scripts/waylandProtolcsInst.sh
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            )
        endif()
    endif()
    else()
        # Fall back to X11
        message(STATUS "Building for X11")

        find_package(X11 REQUIRED)
        find_library(EGL_LIBRARY EGL REQUIRED)

        set(GLPS_SOURCES
            src/glps_egl_context.c
            src/glps_x11.c
            src/glps_window_manager.c
            src/utils/logger/pico_logger.c
            src/glps_thread.c
            src/glps_audio_stream.c
            src/glps_timer.c
        )

        set(GLPS_HEADERS
            internal/glps_egl_context.h
            internal/glps_x11.h
            internal/glps_common.h
            internal/utils/logger/pico_logger.h
            include/glps_window_manager.h
            include/glps_thread.h
            include/glps_audio_stream.h
            internal/utils/audio/dr_mp3.h
            include/glps_timer.h
            # include/glps_mqtt_client.h
            # src/glps_mqtt_client.c
        )

        add_library(${PROJECT_NAME} SHARED ${GLPS_SOURCES} ${GLPS_HEADERS})

        target_compile_definitions(${PROJECT_NAME}
            PRIVATE
                GLPS_USE_X11
        )

        target_compile_options(${PROJECT_NAME}
            PRIVATE
                -Wall
                -Wextra
                -Wno-unused-variable
                -Wno-unused-parameter
        )

        target_link_libraries(${PROJECT_NAME}
            PRIVATE
                ${X11_LIBRARIES}
                ${EGL_LIBRARY}
                pthread
                asound
                rt
        )

        # Install ALSA if needed
        execute_process(
            COMMAND ${PROJECT_SOURCE_DIR}/scripts/alsa.sh
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        )
    endif()

    # Common Linux includes
    target_include_directories(${PROJECT_NAME} SYSTEM
        PRIVATE
            /usr/include/glib-2.0
            /usr/lib/glib-2.0/include
            /usr/include/pixman-1
            /usr/include/libdrm
            /usr/include/libpng12
            /usr/include
            /usr/include/alsa
    )
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Common include directories
target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/GLPS>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/internal
)

# Installation
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

# Set library properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER "${GLPS_HEADERS}"
)

enable_testing()
