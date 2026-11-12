# MkUI Designer (exe MkUI): SDL3 window + Dear ImGui (docking) + the mkui runtime, drawn with mkui::tessellate on SDL_Renderer.
# Build output goes to <build folder>/bin/ with the executable and the manifest mealkit-tool.txt together → put that folder in
# %LOCALAPPDATA%\MealKit\tools\ui\, or open the MealKit editor with --tool-path <build folder>/bin and it appears in the [Tools] menu.

# SDL3 (static, without tests/examples)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL ${CMAKE_CURRENT_BINARY_DIR}/SDL EXCLUDE_FROM_ALL)

set(imgui_dir ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/imgui)
add_library(mkui_imgui STATIC
    ${imgui_dir}/imgui.cpp ${imgui_dir}/imgui_draw.cpp ${imgui_dir}/imgui_tables.cpp ${imgui_dir}/imgui_widgets.cpp
    ${imgui_dir}/backends/imgui_impl_sdl3.cpp ${imgui_dir}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(mkui_imgui PUBLIC ${imgui_dir} ${imgui_dir}/backends)
target_link_libraries(mkui_imgui PUBLIC SDL3::SDL3-static)

add_executable(MkUI WIN32 main.cpp App.h App.cpp PenpotServer.h PenpotServer.cpp)
target_link_libraries(MkUI PRIVATE mkui mkui_imgui mkcontract_client SDL3::SDL3-static) # mkcontract_client: live connection to running games
if(WIN32)
    target_link_libraries(MkUI PRIVATE ws2_32) # Penpot plugin server sockets
    target_link_libraries(MkUI PRIVATE dwmapi) # editor-colored title bar
endif()
# Penpot plugin files (penpot-plugin/dist, committed) next to the exe as penpot/: the Designer serves them
add_custom_command(TARGET MkUI POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/../penpot-plugin/dist ${CMAKE_BINARY_DIR}/bin/penpot)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../penpot-plugin/dist/ DESTINATION penpot)
if(MSVC)
    target_compile_options(MkUI PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MkUI PRIVATE -Wall -Wextra)
endif()
set_target_properties(MkUI PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} upper)
    set_target_properties(MkUI PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${upper} ${CMAKE_BINARY_DIR}/bin)
endforeach()

# Manifest (version from project(mkui VERSION ...)). exe is written without an extension, .exe is appended on Windows
set(MKUI_VERSION ${PROJECT_VERSION})
configure_file(mealkit-tool.txt ${CMAKE_BINARY_DIR}/bin/mealkit-tool.txt @ONLY)
if(WIN32)
    file(READ ${CMAKE_BINARY_DIR}/bin/mealkit-tool.txt manifest)
    string(REPLACE "exe MkUI\n" "exe MkUI.exe\n" manifest "${manifest}")
    file(WRITE ${CMAKE_BINARY_DIR}/bin/mealkit-tool.txt "${manifest}")
endif()

install(TARGETS MkUI RUNTIME DESTINATION .)
install(FILES ${CMAKE_BINARY_DIR}/bin/mealkit-tool.txt DESTINATION .)

if(WIN32)
    target_sources(MkUI PRIVATE res/MkUI.rc) # app icon (engine docs/TOOL_DESIGN.md section 5)
endif()
