# mkui: game UI runtime (MkUI): .ui reading, layout, text, view model binding -> 2D quads. No engine headers; the host gives
# files, strings, image decoding, atlas upload and view model access (mkui/Host.h) and draws the quads (mkui/Ui.h).
# Built standalone it includes tests and the MkUI Designer (tool/); added by another project with add_subdirectory, only the library.
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
# Values the including side can set (defaults to this repo's third_party/):
#   MKUI_STB_DIR          folder containing stb_truetype.h (the implementation is compiled here)
#   MKUI_MKCONTRACT_DIR   mkcontract source folder, used only if no mkcontract target exists yet ("@format ui N" lines)
cmake_minimum_required(VERSION 3.25)
project(mkui VERSION 0.5.0 LANGUAGES CXX)

if(NOT MKUI_STB_DIR)
    set(MKUI_STB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
endif()
if(NOT TARGET mkcontract)
    if(NOT MKUI_MKCONTRACT_DIR)
        set(MKUI_MKCONTRACT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mkcontract)
    endif()
    set(MKCONTRACT_BUILD_TESTS OFF)
    add_subdirectory(${MKUI_MKCONTRACT_DIR} ${CMAKE_CURRENT_BINARY_DIR}/mkcontract)
endif()
option(MKUI_BUILD_TESTS "Build mkui tests" ${PROJECT_IS_TOP_LEVEL})
option(MKUI_BUILD_TOOL "Build the MkUI Designer (SDL3 + Dear ImGui)" ${PROJECT_IS_TOP_LEVEL})

add_library(mkui STATIC
    include/mkui/Types.h
    include/mkui/Host.h
    include/mkui/Document.h
    include/mkui/Source.h
    include/mkui/Atlas.h
    include/mkui/Text.h
    include/mkui/Ui.h
    include/mkui/Draw.h
    include/mkui/Values.h
    src/Document.cpp
    src/Source.cpp
    src/Atlas.cpp
    src/Text.cpp
    src/Ui.cpp
    src/Draw.cpp
    src/Values.cpp
    src/StbTruetype.cpp)
target_include_directories(mkui PUBLIC include)
target_include_directories(mkui SYSTEM PRIVATE ${MKUI_STB_DIR})
target_link_libraries(mkui PRIVATE mkcontract)
target_compile_features(mkui PUBLIC cxx_std_20)
target_compile_definitions(mkui PUBLIC MKUI_VERSION="${PROJECT_VERSION}")
if(MSVC)
    target_compile_options(mkui PRIVATE /permissive- /utf-8 /Zc:__cplusplus)
    set_source_files_properties(src/Document.cpp src/Source.cpp src/Atlas.cpp src/Text.cpp src/Ui.cpp src/Draw.cpp src/Values.cpp PROPERTIES COMPILE_OPTIONS "/W4")
    set_source_files_properties(src/StbTruetype.cpp PROPERTIES COMPILE_OPTIONS "/W0") # external library implementation
else()
    target_compile_options(mkui PRIVATE -Wall -Wextra)
    set_source_files_properties(src/StbTruetype.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

if(MKUI_BUILD_TESTS)
    enable_testing()
    add_executable(mkui_tests tests/UiTests.cpp)
    target_link_libraries(mkui_tests PRIVATE mkui)
    if(MSVC)
        target_compile_options(mkui_tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_definitions(mkui_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(mkui_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME mkui_tests COMMAND mkui_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

if(MKUI_BUILD_TOOL)
    add_subdirectory(tool)
endif()
