cmake_minimum_required(VERSION 3.16)
project(imgui_organic LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------- options
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ORGANIC_TOP_LEVEL ON)
else()
    set(ORGANIC_TOP_LEVEL OFF)
endif()

option(ORGANIC_BUILD_DEMO "Build the organic_demo application (needs GLFW + OpenGL)" ${ORGANIC_TOP_LEVEL})

# Dear ImGui is NOT vendored: point this to your own checkout of the *docking*
# branch (https://github.com/ocornut/imgui/tree/docking). If left empty, the
# following locations are tried: ./imgui, ./imgui_docking, ../imgui, ../imgui_docking
set(ORGANIC_IMGUI_DIR "" CACHE PATH "Path to a Dear ImGui checkout (docking branch)")

if(NOT CMAKE_BUILD_TYPE AND ORGANIC_TOP_LEVEL)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ---------------------------------------------------------------- Dear ImGui
set(_organic_created_imgui OFF)
set(_imgui_dir "${ORGANIC_IMGUI_DIR}")

# is a candidate directory a Dear ImGui checkout of the docking branch?
function(organic_check_imgui_docking dir out_ok)
    set(${out_ok} OFF PARENT_SCOPE)
    if(EXISTS "${dir}/imgui.h")
        file(STRINGS "${dir}/imgui.h" _dock REGEX "ImGuiConfigFlags_DockingEnable")
        if(NOT _dock STREQUAL "")
            set(${out_ok} ON PARENT_SCOPE)
        endif()
    endif()
endfunction()

if(_imgui_dir STREQUAL "")
    # auto-detect: pick the first candidate that is a docking-branch checkout
    foreach(_cand imgui imgui_docking ../imgui ../imgui_docking)
        get_filename_component(_abs "${CMAKE_CURRENT_SOURCE_DIR}/${_cand}" ABSOLUTE)
        organic_check_imgui_docking("${_abs}" _ok)
        if(_ok)
            set(_imgui_dir "${_abs}")
            break()
        endif()
    endforeach()
endif()

if(NOT TARGET imgui)
    if(_imgui_dir STREQUAL "" OR NOT EXISTS "${_imgui_dir}/imgui.h")
        message(FATAL_ERROR
            "imgui_organic: Dear ImGui (docking branch) not found.\n"
            "Clone it next to this project:\n"
            "    git clone --branch docking https://github.com/ocornut/imgui.git\n"
            "or pass -DORGANIC_IMGUI_DIR=/path/to/imgui")
    endif()

    # make sure this is the docking branch (hard error for an explicit path)
    organic_check_imgui_docking("${_imgui_dir}" _ok)
    if(NOT _ok)
        message(FATAL_ERROR
            "imgui_organic: '${_imgui_dir}' is not the *docking* branch of Dear ImGui "
            "(ImGuiConfigFlags_DockingEnable not found in imgui.h).\n"
            "Clone it with: git clone --branch docking https://github.com/ocornut/imgui.git")
    endif()

    add_library(imgui STATIC
        ${_imgui_dir}/imgui.cpp
        ${_imgui_dir}/imgui_draw.cpp
        ${_imgui_dir}/imgui_tables.cpp
        ${_imgui_dir}/imgui_widgets.cpp
        ${_imgui_dir}/imgui_demo.cpp
        ${_imgui_dir}/misc/cpp/imgui_stdlib.cpp
    )
    target_include_directories(imgui PUBLIC
        ${_imgui_dir}
        ${_imgui_dir}/misc/cpp
    )
    set(_organic_created_imgui ON)
    message(STATUS "imgui_organic: building Dear ImGui from ${_imgui_dir}")
else()
    message(STATUS "imgui_organic: reusing existing 'imgui' target")
endif()

# ---------------------------------------------------------------- ImPlot (submodule)
if(NOT TARGET implot)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/implot/implot.h")
        message(FATAL_ERROR
            "imgui_organic: implot submodule missing.\n"
            "Run: git submodule update --init --recursive")
    endif()
    add_library(implot STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/implot/implot.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/implot/implot_items.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/implot/implot_demo.cpp
    )
    target_include_directories(implot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/implot)
    target_link_libraries(implot PUBLIC imgui)
else()
    message(STATUS "imgui_organic: reusing existing 'implot' target")
endif()

# ---------------------------------------------------------------- nlohmann/json (submodule)
set(ORGANIC_JSON_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/single_include")
if(NOT EXISTS "${ORGANIC_JSON_INCLUDE}/nlohmann/json.hpp")
    message(FATAL_ERROR
        "imgui_organic: nlohmann/json submodule missing.\n"
        "Run: git submodule update --init --recursive")
endif()

# ---------------------------------------------------------------- organic library
set(ORGANIC_SOURCES
    organic/OrganicCore.cpp
    organic/OrganicEasing.cpp
    organic/OrganicAudio.cpp
    organic/OrganicTimeline.cpp
    organic/OrganicTimelineUI.cpp
    organic/OrganicPanels.cpp
    organic/OrganicDock.cpp
)

# If the parent project provides its own 'imgui' target that does NOT compile
# misc/cpp/imgui_stdlib.cpp, we compile it into organic (needs ORGANIC_IMGUI_DIR).
if(NOT _organic_created_imgui AND NOT _imgui_dir STREQUAL ""
   AND EXISTS "${_imgui_dir}/misc/cpp/imgui_stdlib.cpp")
    list(APPEND ORGANIC_SOURCES "${_imgui_dir}/misc/cpp/imgui_stdlib.cpp")
endif()

add_library(organic STATIC ${ORGANIC_SOURCES})
add_library(organic::organic ALIAS organic)
target_include_directories(organic PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/organic
    ${ORGANIC_JSON_INCLUDE}            # <nlohmann/json.hpp>
    ${ORGANIC_JSON_INCLUDE}/nlohmann   # "json.hpp" (used internally)
)
if(NOT _organic_created_imgui AND NOT _imgui_dir STREQUAL "")
    target_include_directories(organic PRIVATE ${_imgui_dir}/misc/cpp)
endif()
target_link_libraries(organic PUBLIC imgui implot)

# ---------------------------------------------------------------- demo application
if(ORGANIC_BUILD_DEMO)
    if(_imgui_dir STREQUAL "")
        message(FATAL_ERROR
            "imgui_organic: the demo needs the ImGui sources for the GLFW/OpenGL3 "
            "backends; set -DORGANIC_IMGUI_DIR=/path/to/imgui")
    endif()

    find_package(OpenGL REQUIRED)
    find_package(glfw3 3.3 QUIET)
    if(NOT glfw3_FOUND AND NOT TARGET glfw)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
        add_library(glfw ALIAS PkgConfig::GLFW)
    endif()

    add_executable(organic_demo
        app/main.cpp
        ${_imgui_dir}/backends/imgui_impl_glfw.cpp
        ${_imgui_dir}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(organic_demo PRIVATE ${_imgui_dir}/backends)
    target_link_libraries(organic_demo PRIVATE organic glfw OpenGL::GL)
    target_compile_definitions(organic_demo PRIVATE
        ORGANIC_FONT_DIR="${_imgui_dir}/misc/fonts"
    )
endif()
