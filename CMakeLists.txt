# OctoPrintControl - An OctoPrint Discord Bot
# Copyright (c) 2024 Taylor Talkington
# License: MIT (see LICENSE)
cmake_minimum_required(VERSION 3.16)

project(OctoPrintControl C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

set(BUILD_CURL_EXE OFF)
set(BUILD_LIBCURL_DOCS OFF)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)
set(ENABLE_UNIX_SOCKETS OFF)
set(ENABLE_WEBSOCKETS ON)
set(CURL_DISABLE_KERBEROS_AUTH ON)
set(CURL_DISABLE_ALTSVC ON)
set(CURL_DISABLE_FTP ON)
set(CURL_DISABLE_FTPS ON)
set(CURL_DISABLE_DICT ON)
set(CURL_DISABLE_FILE ON)
set(CURL_DISABLE_GOPHER ON)
set(CURL_DISABLE_GOPHERS ON)
set(CURL_DISABLE_IMAP ON)
set(CURL_DISABLE_IMAPS ON)
set(CURL_DISABLE_LDAP ON)
set(CURL_DISABLE_LDAPS ON)
set(CURL_DISABLE_MQTT ON)
set(CURL_DISABLE_POP3 ON)
set(CURL_DISABLE_POP3S ON)
set(CURL_DISABLE_RTSP ON)
set(CURL_DISABLE_SMB ON)
set(CURL_DISABLE_SMBS ON)
set(CURL_DISABLE_SMTP ON)
set(CURL_DISABLE_SMTPS ON)
set(CURL_DISABLE_TELNET ON)
set(CURL_DISABLE_TFTP ON)
if(WIN32)
    set(CURL_USE_SCHANNEL ON)
else()
    set(CURL_USE_OPENSSL ON)
endif()
set(ENABLE_THREADED_RESOLVER ON)
add_subdirectory(contrib/curl)

add_subdirectory(contrib/nlohmann_json)

add_subdirectory(contrib/fmt)


set(SPDLOG_ENABLE_PCH ON)
set(SPDLOG_FMT_EXTERNAL ON)
add_subdirectory(contrib/spdlog)

if(WIN32)
    set(IMAGEMAGICK_DIR "C:/Program Files/ImageMagick-7.1.1-Q16-HDRI")
    set(MAGICK++_INCLUDE_DIRS "${IMAGEMAGICK_DIR}/include")
    set(MAGICK++_LIBRARY_DIRS "${IMAGEMAGICK_DIR}/lib")
    set(MAGICK++_LIBRARIES CORE_RL_Magick++_)
    set(MAGICK++_CFLAGS "-DNeedFunctionPrototypes" "-D_DLL" "-D_MAGICKMOD_" "-D_VISUALC_")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MAGICK++ REQUIRED Magick++)
endif()

add_executable(OctoPrintControl
    src/main.cpp
    
    src/app.cpp
    src/app.h
    
    src/utils.cpp
    src/utils.h

    src/http.cpp
    src/http.h
    src/websocket.cpp
    src/websocket.h

    src/printer.cpp
    src/printer.h

    src/discord.h
    src/discord.cpp

    src/octoprint.cpp
    src/octoprint.h
)
# Magick++ specific defines
#target_compile_definitions(OctoPrintControl PRIVATE "NeedFunctionPrototypes" "_DLL" "_MAGICKMOD_" "_VISUALC_")
target_compile_options(OctoPrintControl PRIVATE ${MAGICK++_CFLAGS})

target_include_directories(OctoPrintControl PRIVATE ${MAGICK++_INCLUDE_DIRS})
target_link_directories(OctoPrintControl PRIVATE ${MAGICK++_LIBRARY_DIRS})
target_link_libraries(OctoPrintControl PRIVATE CURL::libcurl nlohmann_json::nlohmann_json fmt::fmt spdlog::spdlog ${MAGICK++_LIBRARIES})

if(WIN32)
    file(GLOB IMAGEMAGICK_DLLS "${IMAGEMAGICK_DIR}/*.dll")
    add_custom_command(
        TARGET OctoPrintControl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IMAGEMAGICK_DLLS}
        $<TARGET_FILE_DIR:OctoPrintControl>
    )
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC") 
    target_compile_options(OctoPrintControl PRIVATE "/W3")
else()
    target_compile_options(OctoPrintControl PRIVATE "-Wall")
endif()