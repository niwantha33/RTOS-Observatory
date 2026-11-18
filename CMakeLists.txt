cmake_minimum_required(VERSION 3.20)
project(rtos_observatory VERSION 0.2.0 LANGUAGES C CXX)
option(OBSERVATORY_BUILD_TESTS "Build deterministic C++ tests" ON)
add_library(observatory_target_ring firmware/obs_trace_ring.c)
target_include_directories(observatory_target_ring PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/firmware)
target_compile_features(observatory_target_ring PUBLIC c_std_11)
if(MSVC)
  # MSVC gates its C11 <stdatomic.h> implementation behind this option.
  target_compile_options(observatory_target_ring PUBLIC /experimental:c11atomics)
endif()
add_library(observatory_core src/engine.cpp src/session.cpp src/timeline.cpp)
target_include_directories(observatory_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(observatory_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(observatory_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(observatory_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
add_executable(rtos-observatory src/main.cpp)
target_link_libraries(rtos-observatory PRIVATE observatory_core)
if(OBSERVATORY_BUILD_TESTS)
  enable_testing()
  add_executable(observatory_ring_tests tests/test_ring.c)
  target_link_libraries(observatory_ring_tests PRIVATE observatory_target_ring)
  add_test(NAME observatory_ring_tests COMMAND observatory_ring_tests)
  add_executable(observatory_tests tests/test_engine.cpp)
  target_link_libraries(observatory_tests PRIVATE observatory_core)
  add_executable(observatory_timeline_tests tests/test_timeline.cpp)
  target_link_libraries(observatory_timeline_tests PRIVATE observatory_core)
  add_test(NAME observatory_timeline_accounting COMMAND observatory_timeline_tests)
  add_test(NAME observatory_core_tests COMMAND observatory_tests)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME observatory_zew_adapter COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_zew_adapter.py)
  endif()
  add_test(NAME observatory_cli_demo COMMAND rtos-observatory --input ${CMAKE_CURRENT_SOURCE_DIR}/examples/inversion.trace --output ${CMAKE_CURRENT_BINARY_DIR}/inversion-report.json)
endif()
install(TARGETS rtos-observatory RUNTIME DESTINATION bin)

if(OBSERVATORY_BUILD_TESTS)
  add_executable(observatory_session_tests tests/test_session.cpp)
  target_link_libraries(observatory_session_tests PRIVATE observatory_core)
  add_test(NAME observatory_session_tests COMMAND observatory_session_tests)
endif()

if(OBSERVATORY_BUILD_TESTS)
  foreach(test_target observatory_tests observatory_timeline_tests observatory_ring_tests)
    if(MSVC)
      target_compile_options(${test_target} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${test_target} PRIVATE -UNDEBUG)
    endif()
  endforeach()
endif()

if(OBSERVATORY_BUILD_TESTS AND Python3_Interpreter_FOUND)
  add_test(NAME observatory_cli_session_roundtrip COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_session.py $<TARGET_FILE:rtos-observatory>
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/foundation.trace)
endif()

option(OBSERVATORY_BUILD_LIVE "Build optional standalone Asio TCP capture" OFF)
if(OBSERVATORY_BUILD_LIVE)
  include(cmake/Asio.cmake)
  add_library(observatory_live src/live.cpp)
  target_link_libraries(observatory_live PUBLIC observatory_core observatory_asio_headers)
  add_executable(rtos-observatory-capture src/capture_main.cpp)
  target_link_libraries(rtos-observatory-capture PRIVATE observatory_live)
  install(TARGETS rtos-observatory-capture RUNTIME DESTINATION bin)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio/asio/LICENSE_1_0.txt
    DESTINATION share/doc/rtos-observatory/licenses RENAME Asio-Boost-1.0.txt)
  if(OBSERVATORY_BUILD_TESTS)
    add_executable(observatory_live_tests tests/test_live.cpp)
    target_link_libraries(observatory_live_tests PRIVATE observatory_live)
    add_test(NAME observatory_live_lifecycle COMMAND observatory_live_tests)
  endif()
  if(OBSERVATORY_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME observatory_tcp_capture COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tcp_capture.py $<TARGET_FILE:rtos-observatory-capture>
      $<TARGET_FILE:rtos-observatory> ${CMAKE_CURRENT_SOURCE_DIR}/examples/foundation.trace)
    set_tests_properties(observatory_tcp_capture PROPERTIES TIMEOUT 30)
  endif()
endif()

if(OBSERVATORY_BUILD_TESTS)
  add_executable(observatory_stream_tests tests/test_stream.cpp)
  target_link_libraries(observatory_stream_tests PRIVATE observatory_core)
  add_test(NAME observatory_incremental_stream COMMAND observatory_stream_tests)
endif()
if(OBSERVATORY_BUILD_LIVE AND OBSERVATORY_BUILD_TESTS AND Python3_Interpreter_FOUND)
  add_test(NAME observatory_live_progress COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_live_progress.py $<TARGET_FILE:rtos-observatory-capture>
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/foundation.trace)
  set_tests_properties(observatory_live_progress PROPERTIES TIMEOUT 25)
endif()

# W05/W06: Qt-free standalone Dear ImGui + GLFW + OpenGL3 desktop.
# CLI, offline analysis, and firmware builds do not require UI submodule contents.
option(OBSERVATORY_BUILD_DESKTOP "Build Dear ImGui native offline desktop" OFF)
if(OBSERVATORY_BUILD_DESKTOP)
  # Dependencies are committed gitlinks, never fetched at configure time.
  set(imgui_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
  set(glfw_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw")
  if(NOT EXISTS "${imgui_SOURCE_DIR}/imgui.h" OR
     NOT EXISTS "${glfw_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Desktop dependencies missing. Run: git submodule update --init --recursive")
  endif()
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  if(UNIX AND NOT APPLE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
  endif()
  add_subdirectory("${glfw_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/glfw")
  find_package(OpenGL REQUIRED)

  add_library(observatory_desktop_model desktop/session_controller.cpp)
  target_include_directories(observatory_desktop_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(observatory_desktop_model PUBLIC observatory_core)
  target_compile_features(observatory_desktop_model PUBLIC cxx_std_20)

  add_library(observatory_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(observatory_imgui PUBLIC
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(observatory_imgui PUBLIC glfw OpenGL::GL)
  target_compile_features(observatory_imgui PUBLIC cxx_std_20)

  add_executable(rtos-observatory-desktop
    desktop/imgui/main.cpp
    desktop/imgui/workspace.cpp
    desktop/imgui/workspace.hpp)
  target_link_libraries(rtos-observatory-desktop PRIVATE
    observatory_desktop_model observatory_imgui)
  target_compile_features(rtos-observatory-desktop PRIVATE cxx_std_20)
  install(TARGETS rtos-observatory-desktop RUNTIME DESTINATION bin)
  # Explicit redistributable third-party notices for source/binary packaging.
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
    DESTINATION share/doc/rtos-observatory/licenses RENAME Observatory-Apache-2.0.txt)
  install(FILES ${imgui_SOURCE_DIR}/LICENSE.txt
    DESTINATION share/doc/rtos-observatory/licenses RENAME Dear-ImGui-MIT.txt)
  install(FILES ${glfw_SOURCE_DIR}/LICENSE.md
    DESTINATION share/doc/rtos-observatory/licenses RENAME GLFW-zlib.txt)

  if(OBSERVATORY_BUILD_TESTS)
    add_executable(observatory_desktop_controller_tests desktop/tests/test_session_controller.cpp)
    target_link_libraries(observatory_desktop_controller_tests PRIVATE observatory_desktop_model)
    target_compile_features(observatory_desktop_controller_tests PRIVATE cxx_std_20)
    if(MSVC)
      target_compile_options(observatory_desktop_controller_tests PRIVATE /UNDEBUG)
    else()
      target_compile_options(observatory_desktop_controller_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME observatory_desktop_controller COMMAND observatory_desktop_controller_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/examples/foundation.trace)
    add_test(NAME observatory_desktop_smoke COMMAND rtos-observatory-desktop --smoke
      ${CMAKE_CURRENT_SOURCE_DIR}/examples/foundation.trace)
    add_test(NAME observatory_desktop_timeline_smoke COMMAND rtos-observatory-desktop --smoke
      ${CMAKE_CURRENT_SOURCE_DIR}/examples/timeline.trace)
    set_tests_properties(observatory_desktop_smoke observatory_desktop_timeline_smoke
      PROPERTIES TIMEOUT 30)
    # Linux CI xvfb-run provides a genuine hidden GLFW/OpenGL render smoke.
    if(UNIX AND NOT APPLE)
      find_program(OBSERVATORY_XVFB_RUN xvfb-run)
      if(OBSERVATORY_XVFB_RUN)
        add_test(NAME observatory_imgui_ui_smoke COMMAND ${OBSERVATORY_XVFB_RUN} -a
          $<TARGET_FILE:rtos-observatory-desktop> --smoke-ui
          ${CMAKE_CURRENT_SOURCE_DIR}/examples/timeline.trace)
        set_tests_properties(observatory_imgui_ui_smoke PROPERTIES TIMEOUT 30)
      endif()
    endif()
  endif()
endif()
