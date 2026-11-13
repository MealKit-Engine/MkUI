// MkUI Designer executable: SDL3 window + SDL_Renderer + Dear ImGui (docking). The actual screen is App.
//   MkUI [--project <project folder>] [--file ui/menu.ui] [--select <node>] [--zoom 1.5] [--show Values] [--follow] [--resolution N] [--lang ko|en] [--frames N --capture <png>]
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
    // Spectrum 2 shape (engine docs/TOOL_DESIGN.md 3.1): 8 px fields, 10 px panels and popups.
    // Panels are separated by space and a darker gutter, not by lines
    s.WindowRounding = 10.0f;
    s.ChildRounding = 10.0f;
    s.PopupRounding = 10.0f;
    s.FrameRounding = 8.0f;
    s.GrabRounding = 4.0f;
    s.TabRounding = 8.0f;
    s.ScrollbarRounding = 6.0f;
    s.ScrollbarSize = 10.0f;
    s.WindowBorderSize = 0.0f; // layers are told apart by color, not outlines
    s.ChildBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
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
    // Tokens of engine docs/TOOL_DESIGN.md 3.1: Spectrum 2 dark gray-25..gray-900 (gray 1..12) and blue. Pick a step by its job, never a color by eye
    const auto rgb = [](unsigned v, float a = 1.0f) {
        return ImVec4(((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, a);
    };
    const auto gray = [&](int step) {
        static constexpr unsigned kGray[13] = {0, 0x111111, 0x1B1B1B, 0x222222, 0x2C2C2C, 0x323232, 0x393939,
                                          0x444444, 0x6D6D6D, 0x8A8A8A, 0x909090, 0xAFAFAF, 0xEBEBEB};
        return rgb(kGray[step]);
    };
    const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
    const ImVec4 focus = rgb(0x5D89FF);  // keyboard focus, hovered splitters
    const ImVec4 accent = rgb(0x3B63FB); // selection, check marks, active grabs
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = gray(12);
    c[ImGuiCol_TextDisabled] = gray(11); // secondary text: paths, dates, hints
    c[ImGuiCol_TextSelectedBg] = rgb(0x3B63FB, 0.45f);
    c[ImGuiCol_WindowBg] = gray(2);
    c[ImGuiCol_ChildBg] = clear;
    c[ImGuiCol_PopupBg] = gray(3);
    c[ImGuiCol_Border] = gray(6);
    c[ImGuiCol_BorderShadow] = clear;
    c[ImGuiCol_MenuBarBg] = gray(1);
    c[ImGuiCol_TitleBg] = gray(1);
    c[ImGuiCol_TitleBgActive] = gray(1);
    c[ImGuiCol_TitleBgCollapsed] = gray(1);
    // Hover is one neutral step up, pressed and selected one more; the accent never fills a hovered control
    c[ImGuiCol_FrameBg] = gray(4); // fields and buttons share a step, so a combo and its arrow read as one control
    c[ImGuiCol_FrameBgHovered] = gray(5);
    c[ImGuiCol_FrameBgActive] = gray(6);
    c[ImGuiCol_Header] = gray(5);
    c[ImGuiCol_HeaderHovered] = gray(4);
    c[ImGuiCol_HeaderActive] = gray(6);
    c[ImGuiCol_Button] = gray(4);
    c[ImGuiCol_ButtonHovered] = gray(6);
    c[ImGuiCol_ButtonActive] = gray(7);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = gray(9);
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Separator] = gray(5);
    c[ImGuiCol_SeparatorHovered] = focus;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = clear;
    c[ImGuiCol_ResizeGripHovered] = focus;
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_ScrollbarBg] = clear;
    c[ImGuiCol_ScrollbarGrab] = gray(6);
    c[ImGuiCol_ScrollbarGrabHovered] = gray(7);
    c[ImGuiCol_ScrollbarGrabActive] = gray(8);
    // The selected tab joins its panel; the others sit on the strip
    c[ImGuiCol_Tab] = clear;
    c[ImGuiCol_TabHovered] = gray(4);
    c[ImGuiCol_TabSelected] = gray(2);
    c[ImGuiCol_TabSelectedOverline] = clear;
    c[ImGuiCol_TabDimmed] = clear;
    c[ImGuiCol_TabDimmedSelected] = gray(2);
    c[ImGuiCol_TabDimmedSelectedOverline] = clear;
    c[ImGuiCol_TableHeaderBg] = clear; // no header band: quiet header text over a line
    c[ImGuiCol_TableBorderStrong] = gray(6);
    c[ImGuiCol_TableBorderLight] = gray(5);
    c[ImGuiCol_TableRowBgAlt] = clear; // no zebra rows
    c[ImGuiCol_DockingPreview] = rgb(0x3B63FB, 0.5f);
    c[ImGuiCol_DockingEmptyBg] = clear;
    c[ImGuiCol_NavCursor] = focus;
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

// Dark title bar in gray 1 #111111 (editor kEditorCaptionColor, docs/TOOL_DESIGN.md 3.1). Ignored where Windows doesn't support it
void applyEditorCaption(SDL_Window* window) {
#if defined(_WIN32)
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) return;
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    const COLORREF caption = RGB(0x11, 0x11, 0x11);
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
#else
    (void)window;
#endif
}

} // namespace

int main(int argc, char** argv) {
    std::string project, file, capture, select, show, language;
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
        else if (std::strcmp(argv[i], "--lang") == 0 && i + 1 < argc) language = argv[++i]; // this run only (captures)
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
    app.languageOverride = language;
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
        SDL_SetRenderDrawColorFloat(renderer, 0x11 / 255.0f, 0x11 / 255.0f, 0x11 / 255.0f, 1.0f); // gray 1
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
