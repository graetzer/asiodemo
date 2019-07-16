cmake_minimum_required(VERSION 3.4)

project(asiodemo CXX C)

set(CMAKE_CXX_STANDARD 14)

# llhttp parser library

SET(LLHTTP_SOURCES
  ${PROJECT_SOURCE_DIR}/3rdParty/llhttp/src/api.c
  ${PROJECT_SOURCE_DIR}/3rdParty/llhttp/src/http.c
  ${PROJECT_SOURCE_DIR}/3rdParty/llhttp/src/llhttp.c
)

add_library(llhttp STATIC 
  ${LLHTTP_SOURCES}
)

target_include_directories(llhttp PUBLIC
  "${PROJECT_SOURCE_DIR}/3rdParty/llhttp/include"
)

# OpenSSL

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

# Enable threads

find_package(Threads REQUIRED)

# Sources

set(SOURCES
  src/rest/Acceptor.cpp
  src/rest/Server.cpp
  src/rest/Request.cpp
  src/rest/Response.cpp
)

add_library(asiodemolib STATIC 
  ${SOURCES}
)

target_link_libraries(asiodemolib 
  llhttp
  ${OPENSSL_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
)

add_definitions("-DASIO_STANDALONE=1")

# 3rdParty includes

target_include_directories(asiodemolib PUBLIC
  "${PROJECT_SOURCE_DIR}/3rdParty/asio/include"
)

# Main executable

add_executable(asiodemo 
	src/main.cpp
)

target_link_libraries(asiodemo 
  asiodemolib
)

