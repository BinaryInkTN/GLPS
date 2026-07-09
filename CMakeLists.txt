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


#
# Windows
#
if(WIN32)

    message(STATUS "Building Windows backend")

    add_library(${PROJECT_NAME}
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

    target_link_libraries(${PROJECT_NAME} PRIVATE
        opengl32
        gdi32
        user32
    )


#
# Linux
#
elseif(UNIX AND NOT APPLE)

    message(STATUS "Configuring Linux backend")


    find_package(PkgConfig REQUIRED)


    pkg_check_modules(WAYLAND REQUIRED
        wayland-client
        wayland-egl
    )


    pkg_check_modules(EGL REQUIRED egl)

    pkg_check_modules(ALSA REQUIRED alsa)


    #
    # Wayland scanner from Yocto or system
    #
    if(NOT WAYLAND_SCANNER)

        find_program(
            WAYLAND_SCANNER
            wayland-scanner
            REQUIRED
        )

    endif()


    #
    # Wayland protocols path
    #
    if(NOT WAYLAND_PROTOCOLS_DIR)

        execute_process(
            COMMAND
                ${PKG_CONFIG_EXECUTABLE}
                --variable=pkgdatadir
                wayland-protocols

            OUTPUT_VARIABLE WAYLAND_PROTOCOLS_DIR

            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

    endif()


    message(STATUS
        "Wayland scanner: ${WAYLAND_SCANNER}"
    )

    message(STATUS
        "Wayland protocols: ${WAYLAND_PROTOCOLS_DIR}"
    )


    set(XDG_PROTOCOL_XML
        ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml
    )


    if(NOT EXISTS ${XDG_PROTOCOL_XML})

        message(FATAL_ERROR
            "Missing xdg-shell.xml: ${XDG_PROTOCOL_XML}"
        )

    endif()


    #
    # Generate protocol files
    #
    # Result:
    #
    # build/generated/xdg/
    #       xdg-shell.h
    #       xdg-shell-protocol.c
    #
    #

    set(GENERATED_XDG_DIR
        ${CMAKE_BINARY_DIR}/generated/xdg
    )

    # Clean old generated protocols at configure time
    file(REMOVE_RECURSE ${GENERATED_XDG_DIR})
    
    # Create the directory after cleaning
    file(MAKE_DIRECTORY ${GENERATED_XDG_DIR})


    set(GENERATED_XDG_HEADER
        ${GENERATED_XDG_DIR}/xdg-shell.h
    )


    set(GENERATED_XDG_SOURCE
        ${GENERATED_XDG_DIR}/xdg-shell-protocol.c
    )


    add_custom_command(
        OUTPUT
            ${GENERATED_XDG_HEADER}

        COMMAND
            ${CMAKE_COMMAND} -E make_directory ${GENERATED_XDG_DIR}
        COMMAND
            ${WAYLAND_SCANNER}
            client-header
            ${XDG_PROTOCOL_XML}
            ${GENERATED_XDG_HEADER}

        DEPENDS
            ${XDG_PROTOCOL_XML}
        
        COMMENT "Generating Wayland client header"
    )


    add_custom_command(
        OUTPUT
            ${GENERATED_XDG_SOURCE}

        COMMAND
            ${CMAKE_COMMAND} -E make_directory ${GENERATED_XDG_DIR}
        COMMAND
            ${WAYLAND_SCANNER}
            public-code
            ${XDG_PROTOCOL_XML}
            ${GENERATED_XDG_SOURCE}

        DEPENDS
            ${XDG_PROTOCOL_XML}
        
        COMMENT "Generating Wayland protocol source"
    )


    add_custom_target(
        generate_wayland_protocols

        DEPENDS
            ${GENERATED_XDG_HEADER}
            ${GENERATED_XDG_SOURCE}
    )

    # Make generate depend on clean
    add_dependencies(generate_wayland_protocols clean_wayland_protocols)


    #
    # Backend selection
    #

    if(GLPS_FORCE_WAYLAND)

        message(STATUS "Building Wayland backend")


        add_library(${PROJECT_NAME}

            src/glps_wayland.c
            src/glps_egl_context.c
            src/glps_window_manager.c

            src/utils/logger/pico_logger.c

            src/glps_thread.c
            src/glps_audio_stream.c
            src/glps_timer.c

            ${GENERATED_XDG_SOURCE}
        )


        add_dependencies(
            ${PROJECT_NAME}
            generate_wayland_protocols
        )


        target_compile_definitions(${PROJECT_NAME}
            PRIVATE
                GLPS_USE_WAYLAND
        )


        target_include_directories(${PROJECT_NAME}
            PRIVATE

                ${WAYLAND_INCLUDE_DIRS}
                ${EGL_INCLUDE_DIRS}
                ${ALSA_INCLUDE_DIRS}

                # Allows:
                # #include "xdg/xdg-shell.h"
                ${CMAKE_BINARY_DIR}/generated
        )


        target_link_libraries(${PROJECT_NAME}
            PRIVATE

                ${WAYLAND_LIBRARIES}
                ${EGL_LIBRARIES}
                ${ALSA_LIBRARIES}

                GLESv2
                xkbcommon
                pthread
                rt
                m
        )


    else()

        message(STATUS "Building X11 backend")


        pkg_check_modules(X11 REQUIRED x11)


        add_library(${PROJECT_NAME}

            src/glps_x11.c
            src/glps_egl_context.c
            src/glps_window_manager.c

            src/utils/logger/pico_logger.c

            src/glps_thread.c
            src/glps_audio_stream.c
            src/glps_timer.c
        )


        target_compile_definitions(${PROJECT_NAME}
            PRIVATE
                GLPS_USE_X11
        )


        target_include_directories(${PROJECT_NAME}
            PRIVATE

                ${X11_INCLUDE_DIRS}
                ${EGL_INCLUDE_DIRS}
                ${ALSA_INCLUDE_DIRS}
        )


        target_link_libraries(${PROJECT_NAME}
            PRIVATE

                ${X11_LIBRARIES}
                ${EGL_LIBRARIES}
                ${ALSA_LIBRARIES}

                GLESv2
                pthread
                rt
                m
        )


    endif()


else()

    message(FATAL_ERROR
        "Unsupported platform"
    )

endif()



#
# Common includes
#

target_include_directories(${PROJECT_NAME}

    PUBLIC

        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>

        $<INSTALL_INTERFACE:include/GLPS>


    PRIVATE

        ${PROJECT_SOURCE_DIR}/internal
)



#
# Install
#

install(
    DIRECTORY
        include/

    DESTINATION
        include/GLPS
)


install(
    TARGETS ${PROJECT_NAME}

    EXPORT GLPSConfig

    LIBRARY DESTINATION lib

    ARCHIVE DESTINATION lib

    RUNTIME DESTINATION bin
)


install(
    EXPORT GLPSConfig

    FILE GLPSConfig.cmake

    NAMESPACE GLPS::

    DESTINATION lib/cmake/GLPS
)


set_target_properties(${PROJECT_NAME}

    PROPERTIES

        VERSION ${PROJECT_VERSION}

        SOVERSION 1
)


enable_testing()