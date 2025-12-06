# SPDX-License-Identifier: Apache-2.0
# WSPR-ease Zephyr Application

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wspr-ease)

# Include directories
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Source files
target_sources(app PRIVATE
  src/main.cpp
  src/wifi_manager.cpp
  src/webserver.cpp
  src/captive_dns.cpp
  src/gnss.cpp
  src/fpga.cpp
)
