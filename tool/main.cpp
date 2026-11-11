// MkUI Designer executable: SDL3 window + SDL_Renderer + Dear ImGui (docking). The actual screen is App.
//   MkUI [--project <project folder>] [--file ui/menu.ui] [--select <node>] [--zoom 1.5] [--resolution N] [--frames N --capture <png>]
#include "App.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// A system font for the tool itself, with Korean merged in (comments in .ui files); ImGui's default font if none
void loadFont(float size) {
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
}

} // namespace

int main(int argc, char** argv) {
    std::string project, file, capture, select;
    float zoom = 1.0f;
    long frames = 0;
    int resolution = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc) project = argv[++i];
        else if (std::strcmp(argv[i], "--file") == 0 && i + 1 < argc) file = argv[++i];
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = std::strtol(argv[++i], nullptr, 10);
        else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) capture = argv[++i];
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
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    style.FrameRounding = 3.0f;
    loadFont(16.0f);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    mkui::tool::App app;
    app.window = window;
    app.init(renderer, project, file, resolution);
    if (!select.empty() && !app.selectByName(select)) SDL_Log("No node named %s", select.c_str());
    if (zoom > 0.0f) app.setZoom(zoom);

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
