// MkUI Designer executable: SDL3 window + SDL_Renderer + Dear ImGui (docking). The actual screen is App.
//   MkUI [--project <project folder>] [--file ui/menu.ui] [--select <node>] [--zoom 1.5] [--show Values] [--follow] [--resolution N] [--frames N --capture <png>]
#include "App.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {

// A system font for the tool itself, with Korean merged in (comments in .ui files); ImGui's default font if none
bool loadFont(float size) {
    ImGuiIO& io = ImGui::GetIO();
#if defined(_WIN32)
    const char* main[] = {"C:\\Windows\\Fonts\\segoeui.ttf"};
    const char* merge[] = {"C:\\Windows\\Fonts\\malgun.ttf"};
#elif defined(__APPLE__)
    const char* main[] = {"/System/Library/Fonts/Supplemental/Arial.ttf"};
    const char* merge[] = {"/System/Library/Fonts/AppleSDGothicNeo.ttc"};
#else
    const char* main[] = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
    const char* merge[] = {"/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc"};
#endif
    bool loaded = false;
    for (const char* path : main)
        if (SDL_GetPathInfo(path, nullptr) && io.Fonts->AddFontFromFileTTF(path, size)) {
            loaded = true;
            break;
        }
    if (!loaded) {
        ImFontConfig config;
        config.SizePixels = size;
        io.Fonts->AddFontDefaultVector(&config);
    }
    ImFontConfig mergeConfig;
    mergeConfig.MergeMode = true;
    for (const char* path : merge)
        if (SDL_GetPathInfo(path, nullptr) && io.Fonts->AddFontFromFileTTF(path, size, &mergeConfig)) break;
#if defined(_WIN32)
    // Icon font (engine docs/TOOL_DESIGN.md section 2): Segoe Fluent Icons on Windows 11, Segoe MDL2 Assets on 10. Returns whether it loaded
    for (const char* icons : {"C:\\Windows\\Fonts\\SegoeIcons.ttf", "C:\\Windows\\Fonts\\segmdl2.ttf"}) {
        ImFontConfig config;
        config.MergeMode = true;
        config.GlyphOffset = ImVec2(0.0f, 3.0f);
        if (SDL_GetPathInfo(icons, nullptr) && io.Fonts->AddFontFromFileTTF(icons, size, &config)) return true;
    }
#endif
    return false;
}

// The MealKit editor's look (module-debug src/mealkit/debug/ImGuiLayer.cpp applyStyle), copied since MkUI doesn't link the engine
// (engine docs/TOOL_DESIGN.md section 3). Keep it the same as the original.
void applyEditorStyle(float scale) {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    // Panels are separated by space and a darker gutter, not by lines
    s.WindowRounding = 6.0f;
    s.ChildRounding = 4.0f;
    s.PopupRounding = 6.0f;
    s.FrameRounding = 4.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 4.0f;
    s.ScrollbarRounding = 6.0f;
    s.ScrollbarSize = 10.0f;
    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.TabBarBorderSize = 0.0f;
    s.TabBarOverlineSize = 0.0f;
    s.DockingSeparatorSize = 4.0f;
    s.DockingNodeHasCloseButton = false; // each tab has its own close button
    s.TabCloseButtonMinWidthSelected = 0.0f; // tabs show it only while hovered
    s.TabCloseButtonMinWidthUnselected = 0.0f;
    s.WindowPadding = ImVec2(10.0f, 10.0f);
    s.FramePadding = ImVec2(7.0f, 4.0f);
    s.ItemSpacing = ImVec2(8.0f, 6.0f);
    s.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    s.CellPadding = ImVec2(6.0f, 3.0f);
    s.IndentSpacing = 16.0f;
    s.WindowMenuButtonPosition = ImGuiDir_None; // no triangle on every dock tab bar: tabs are all visible and can be dragged
    const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
    const ImVec4 gutter(0.075f, 0.078f, 0.086f, 1.00f); // behind the panels: menu bar, status bar, gaps, tab strips
    const ImVec4 panel(0.118f, 0.122f, 0.133f, 1.00f);
    const auto white = [](float alpha) { return ImVec4(1.0f, 1.0f, 1.0f, alpha); };
    const auto accent = [](float alpha) { return ImVec4(0.36f, 0.56f, 0.86f, alpha); }; // only for selection and active states
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = ImVec4(0.88f, 0.89f, 0.91f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.52f, 0.56f, 1.00f);
    c[ImGuiCol_TextSelectedBg] = accent(0.35f);
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = clear;
    c[ImGuiCol_PopupBg] = ImVec4(0.135f, 0.14f, 0.153f, 0.99f);
    c[ImGuiCol_Border] = white(0.06f);
    c[ImGuiCol_BorderShadow] = clear;
    c[ImGuiCol_MenuBarBg] = gutter;
    c[ImGuiCol_TitleBg] = gutter;
    c[ImGuiCol_TitleBgActive] = gutter;
    c[ImGuiCol_TitleBgCollapsed] = gutter;
    c[ImGuiCol_FrameBg] = ImVec4(0.165f, 0.17f, 0.185f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.207f, 0.225f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.235f, 0.243f, 0.265f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.20f, 0.31f, 0.47f, 1.00f);
    c[ImGuiCol_HeaderHovered] = white(0.06f);
    c[ImGuiCol_HeaderActive] = accent(0.55f);
    c[ImGuiCol_Button] = ImVec4(0.165f, 0.17f, 0.185f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.235f, 0.243f, 0.265f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.29f, 0.30f, 0.325f, 1.00f);
    c[ImGuiCol_CheckMark] = ImVec4(0.62f, 0.76f, 0.98f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.42f, 0.44f, 0.48f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = accent(1.0f);
    c[ImGuiCol_Separator] = white(0.07f);
    c[ImGuiCol_SeparatorHovered] = accent(0.55f);
    c[ImGuiCol_SeparatorActive] = accent(0.90f);
    c[ImGuiCol_ResizeGrip] = clear;
    c[ImGuiCol_ResizeGripHovered] = accent(0.40f);
    c[ImGuiCol_ResizeGripActive] = accent(0.80f);
    c[ImGuiCol_ScrollbarBg] = clear;
    c[ImGuiCol_ScrollbarGrab] = white(0.13f);
    c[ImGuiCol_ScrollbarGrabHovered] = white(0.22f);
    c[ImGuiCol_ScrollbarGrabActive] = white(0.30f);
    // The selected tab joins its panel; the others sit on the strip
    c[ImGuiCol_Tab] = clear;
    c[ImGuiCol_TabHovered] = white(0.07f);
    c[ImGuiCol_TabSelected] = panel;
    c[ImGuiCol_TabSelectedOverline] = clear;
    c[ImGuiCol_TabDimmed] = clear;
    c[ImGuiCol_TabDimmedSelected] = panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = clear;
    c[ImGuiCol_TableHeaderBg] = white(0.04f);
    c[ImGuiCol_TableBorderStrong] = white(0.07f);
    c[ImGuiCol_TableBorderLight] = white(0.05f);
    c[ImGuiCol_TableRowBgAlt] = white(0.02f);
    c[ImGuiCol_DockingPreview] = accent(0.50f);
    c[ImGuiCol_DockingEmptyBg] = clear;
    c[ImGuiCol_NavCursor] = accent(0.90f);
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

// Dark title bar in the gutter color (#131416, editor kEditorCaptionColor). Ignored where Windows doesn't support it
void applyEditorCaption(SDL_Window* window) {
#if defined(_WIN32)
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) return;
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    const COLORREF caption = RGB(0x13, 0x14, 0x16);
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
#else
    (void)window;
#endif
}

} // namespace

int main(int argc, char** argv) {
    std::string project, file, capture, select, show;
    bool follow = false;
    float zoom = 1.0f;
    long frames = 0;
    int resolution = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc) project = argv[++i];
        else if (std::strcmp(argv[i], "--file") == 0 && i + 1 < argc) file = argv[++i];
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = std::strtol(argv[++i], nullptr, 10);
        else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) capture = argv[++i];
        else if (std::strcmp(argv[i], "--show") == 0 && i + 1 < argc) show = argv[++i];
        else if (std::strcmp(argv[i], "--follow") == 0) follow = true;
        else if (std::strcmp(argv[i], "--select") == 0 && i + 1 < argc) select = argv[++i];
        else if (std::strcmp(argv[i], "--zoom") == 0 && i + 1 < argc) zoom = std::strtof(argv[++i], nullptr);
        else if (std::strcmp(argv[i], "--resolution") == 0 && i + 1 < argc) resolution = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--version") == 0) {
            SDL_Log("MkUI %s", MKUI_VERSION);
            return 0;
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_Window* window = SDL_CreateWindow("MkUI", static_cast<int>(1500 * scale), static_cast<int>(900 * scale),
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        SDL_Log("Cannot create the window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    applyEditorCaption(window);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    static std::string ini; // window layout in the user folder
    if (char* pref = SDL_GetPrefPath("MealKit", "MkUI")) {
        ini = std::string(pref) + "imgui.ini";
        SDL_free(pref);
        io.IniFilename = frames > 0 ? nullptr : ini.c_str(); // captures use the default layout
    }
    applyEditorStyle(scale);
    const bool iconFont = loadFont(16.0f);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    mkui::tool::App app;
    app.window = window;
    app.iconFont = iconFont;
    app.init(renderer, project, file, resolution);
    if (!select.empty() && !app.selectByName(select)) SDL_Log("No node named %s", select.c_str());
    if (zoom > 0.0f) app.setZoom(zoom);
    if (!show.empty()) app.showWindow(show);
    if (follow) app.followFirstGame();

    Uint64 last = SDL_GetPerformanceCounter();
    std::string shownTitle;
    long frame = 0;
    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) app.requestQuit();
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) app.requestQuit();
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        const double dt = static_cast<double>(now - last) / static_cast<double>(SDL_GetPerformanceFrequency());
        last = now;

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        app.frame(dt); // renders the preview into its own target before the window is drawn
        ImGui::Render();
        if (app.canQuit()) running = false;

        const std::string title = app.title();
        if (title != shownTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            shownTitle = title;
        }
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColorFloat(renderer, 0.08f, 0.08f, 0.09f, 1.0f);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (frames > 0 && ++frame >= frames) {
            if (!capture.empty()) {
                SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
                if (!shot || !SDL_SavePNG(shot, capture.c_str())) SDL_Log("Cannot save %s: %s", capture.c_str(), SDL_GetError());
                SDL_DestroySurface(shot);
            }
            running = false;
        }
        SDL_RenderPresent(renderer);
    }

    app.shutdown();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
