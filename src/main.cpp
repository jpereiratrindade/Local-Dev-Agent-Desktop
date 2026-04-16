#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <iostream>
#include <filesystem>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include "OllamaClient.hpp"
#include "AgentUI.hpp"

namespace {
std::string findFontPath(const std::vector<const char*>& candidates) {
    for (const char* path : candidates) {
        if (std::filesystem::exists(path)) return path;
    }
    return {};
}

bool loadFonts(ImGuiIO& io) {
    const float baseFontSize = 16.0f;
    bool emojiLoaded = false;

#if defined(_WIN32)
    const std::vector<const char*> baseCandidates = {
        "assets/fonts/NotoEmoji-Regular.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
    };
    const std::vector<const char*> emojiCandidates = {
        "assets/fonts/NotoEmoji-Regular.ttf",
        "C:\\Windows\\Fonts\\seguiemj.ttf",
    };
#elif defined(__APPLE__)
    const std::vector<const char*> baseCandidates = {
        "assets/fonts/NotoEmoji-Regular.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    };
    const std::vector<const char*> emojiCandidates = {
        "assets/fonts/NotoEmoji-Regular.ttf",
        "/System/Library/Fonts/Apple Color Emoji.ttc",
    };
#else
    const std::vector<const char*> baseCandidates = {
        "assets/fonts/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    const std::vector<const char*> emojiCandidates = {
        "assets/fonts/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/google-noto-emoji-fonts/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/TTF/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/google-noto-color-emoji-fonts/Noto-COLRv1.ttf",
        "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
    };
#endif

    std::string basePath = findFontPath(baseCandidates);
    ImFont* baseFont = nullptr;
    if (!basePath.empty()) baseFont = io.Fonts->AddFontFromFileTTF(basePath.c_str(), baseFontSize);
    if (!baseFont) baseFont = io.Fonts->AddFontDefault();
    io.FontDefault = baseFont;

    std::string emojiPath = findFontPath(emojiCandidates);
    if (!emojiPath.empty()) {
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.PixelSnapH = true;

        static const ImWchar emojiRanges[] = {
            0x00A0, 0x00FF,
            0x2000, 0x3000,
            0x1F300, 0x1FAFF,
            0
        };

        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        builder.AddRanges(emojiRanges);
        static ImVector<ImWchar> ranges;
        ranges.clear();
        builder.BuildRanges(&ranges);
        ImFont* emojiFont = io.Fonts->AddFontFromFileTTF(emojiPath.c_str(), baseFontSize, &cfg, ranges.Data);
        emojiLoaded = (emojiFont != nullptr);
    }
    return emojiLoaded;
}
} // namespace

int main(int argc, char* argv[]) {
    // Detect models on startup
    agent::network::OllamaClient ollama("http://localhost:11434"); 
    auto models = ollama.listModels();

    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        std::cerr << "Error: " << SDL_GetError() << std::endl;
        return -1;
    }

    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Agent GUI", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    bool emojiEnabled = loadFonts(io);

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    agent::ui::AgentUI ui;
    ui.emojiIconsEnabled = emojiEnabled;
    ui.setOllama(&ollama);
    ui.availableModels = models;
    if (!models.empty()) ui.currentModel = models[0];
    ui.startTelemetry(); // Ativar monitoramento AMD v4.1

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        if (ui.exitRequested) done = true;

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Render UI
        ui.render();

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    ui.stopTelemetryLoop();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
