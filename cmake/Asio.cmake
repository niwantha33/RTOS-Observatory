# SPDX-License-Identifier: Apache-2.0
# Standalone Asio is committed as an exact gitlink under third_party/asio.
# Never substitute a globally installed Asio or download sources at configure time.
set(OBSERVATORY_ASIO_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/third_party/asio/asio/include")
if(NOT EXISTS "${OBSERVATORY_ASIO_INCLUDE_DIR}/asio.hpp")
  message(FATAL_ERROR
    "Pinned standalone Asio submodule missing. Run: git submodule update --init --recursive")
endif()
add_library(observatory_asio_headers INTERFACE)
target_include_directories(observatory_asio_headers SYSTEM INTERFACE
  "${OBSERVATORY_ASIO_INCLUDE_DIR}")
target_compile_definitions(observatory_asio_headers INTERFACE
  ASIO_STANDALONE ASIO_NO_DEPRECATED)
find_package(Threads REQUIRED)
target_link_libraries(observatory_asio_headers INTERFACE Threads::Threads)
if(WIN32)
  target_compile_definitions(observatory_asio_headers INTERFACE _WIN32_WINNT=0x0601)
  target_link_libraries(observatory_asio_headers INTERFACE ws2_32 mswsock)
endif()
