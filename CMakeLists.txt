cmake_minimum_required(VERSION 3.16...3.28)

project(easy-irl-stream VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# --- Plugin sources ---
add_library(easy-irl-stream MODULE
    src/plugin-main.c
    src/irl-source.c
    src/ingest-thread.c
    src/media-decoder.c
    src/event-handler.c
    src/webhook.c
    src/srtla-server.c
    src/remote-settings.c
    src/obfuscation.cpp
    src/help-dialog.cpp
    src/stats-dialog.cpp
)

if(WIN32)
    # ================================================================
    #  Windows: explicit dependency paths (set by build.ps1 or CI)
    # ================================================================
    set(OBS_SOURCE_DIR "" CACHE PATH "Path to obs-studio source tree (for headers)")
    set(OBS_LIB_DIR    "" CACHE PATH "Path to OBS import libraries (.lib)")
    set(FFMPEG_DIR     "" CACHE PATH "Path to FFmpeg dev package (include + lib)")
    set(QT6_DIR        "" CACHE PATH "Path to Qt6 dev package (include + lib)")

    add_library(obs_lib SHARED IMPORTED)
    set_target_properties(obs_lib PROPERTIES
        IMPORTED_IMPLIB "${OBS_LIB_DIR}/obs.lib"
    )

    add_library(obs_frontend SHARED IMPORTED)
    set_target_properties(obs_frontend PROPERTIES
        IMPORTED_IMPLIB "${OBS_LIB_DIR}/obs-frontend-api.lib"
    )

    add_library(w32_pthreads SHARED IMPORTED)
    set_target_properties(w32_pthreads PROPERTIES
        IMPORTED_IMPLIB "${OBS_LIB_DIR}/w32-pthreads.lib"
    )

    foreach(_qt_mod Core Gui Widgets)
        add_library(Qt6::${_qt_mod} SHARED IMPORTED)
        set_target_properties(Qt6::${_qt_mod} PROPERTIES
            IMPORTED_IMPLIB "${QT6_DIR}/lib/Qt6${_qt_mod}.lib"
        )
    endforeach()

    target_include_directories(easy-irl-stream PRIVATE
        "${OBS_SOURCE_DIR}/libobs"
        "${OBS_SOURCE_DIR}/frontend/api"
        "${OBS_SOURCE_DIR}/deps/w32-pthreads"
        "${FFMPEG_DIR}/include"
        "${QT6_DIR}/include"
        "${QT6_DIR}/include/QtCore"
        "${QT6_DIR}/include/QtGui"
        "${QT6_DIR}/include/QtWidgets"
    )

    target_link_libraries(easy-irl-stream
        obs_lib
        obs_frontend
        w32_pthreads
        ws2_32
        iphlpapi
        shell32
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        "${FFMPEG_DIR}/lib/avformat.lib"
        "${FFMPEG_DIR}/lib/avcodec.lib"
        "${FFMPEG_DIR}/lib/avutil.lib"
        "${FFMPEG_DIR}/lib/swscale.lib"
        "${FFMPEG_DIR}/lib/swresample.lib"
        "${FFMPEG_DIR}/lib/libcurl_imp.lib"
    )

    target_compile_definitions(easy-irl-stream PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(easy-irl-stream PRIVATE /Zc:__cplusplus /permissive- /utf-8)

else()
    # ================================================================
    #  Linux / macOS: find system packages
    # ================================================================
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(LIBOBS REQUIRED IMPORTED_TARGET libobs)
    pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
    pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
    pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
    pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)
    pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)

    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
    find_package(CURL REQUIRED)

    # obs-frontend-api: try pkg-config first, fall back to find_library
    pkg_check_modules(OBS_FRONTEND IMPORTED_TARGET obs-frontend-api)
    if(OBS_FRONTEND_FOUND)
        set(_OBS_FRONTEND_TARGET PkgConfig::OBS_FRONTEND)
    else()
        find_library(OBS_FRONTEND_LIB obs-frontend-api)
        if(OBS_FRONTEND_LIB)
            add_library(obs_frontend_imported SHARED IMPORTED)
            set_target_properties(obs_frontend_imported PROPERTIES
                IMPORTED_LOCATION "${OBS_FRONTEND_LIB}"
            )
            set(_OBS_FRONTEND_TARGET obs_frontend_imported)
        else()
            message(WARNING "obs-frontend-api not found, linking by name")
            set(_OBS_FRONTEND_TARGET obs-frontend-api)
        endif()
    endif()

    # Optional: extra include dir for obs-frontend-api headers
    set(OBS_FRONTEND_INCLUDE_DIR "" CACHE PATH
        "Path to obs-frontend-api headers (frontend/api)")
    if(OBS_FRONTEND_INCLUDE_DIR)
        target_include_directories(easy-irl-stream PRIVATE
            "${OBS_FRONTEND_INCLUDE_DIR}")
    endif()

    target_link_libraries(easy-irl-stream
        PkgConfig::LIBOBS
        ${_OBS_FRONTEND_TARGET}
        PkgConfig::AVFORMAT
        PkgConfig::AVCODEC
        PkgConfig::AVUTIL
        PkgConfig::SWSCALE
        PkgConfig::SWRESAMPLE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        CURL::libcurl
        pthread
    )

    # Install targets
    include(GNUInstallDirs)
    install(TARGETS easy-irl-stream
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins")
endif()

if(DEFINED PLUGIN_VERSION_OVERRIDE)
    set(_PLUGIN_VER "${PLUGIN_VERSION_OVERRIDE}")
else()
    set(_PLUGIN_VER "${PROJECT_VERSION}")
endif()

target_compile_definitions(easy-irl-stream PRIVATE
    PLUGIN_VERSION="${_PLUGIN_VER}"
)

set_target_properties(easy-irl-stream PROPERTIES PREFIX "")
