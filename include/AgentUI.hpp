#pragma once

#include "imgui.h"
#include "TextEditor.h"
#include "portable-file-dialogs.h"
#include "OllamaClient.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include "json.hpp"
#include "Orchestrator.hpp"
#include "NativeTools.hpp"

namespace agent::ui {

struct ChatMessage {
    std::string role;
    std::string text;
};

class AgentUI {
public:
    void render();
    void setOllama(agent::network::OllamaClient* client);

    std::vector<ChatMessage> history;
    std::mutex msgMutex;
    char inputBuf[1024] = "";
    bool scrollToBottom = false;
    bool exitRequested = false;
    TextEditor codeEditor;

    std::string currentProjectRoot = ".";
    std::string currentModel = "qwen2.5:14b";
    std::vector<std::string> availableModels;
    
    // Context & Thinking State
    std::string selectedFile = "";
    std::string detectedTech = "Desconhecida";
    std::string detectedDeps = "";
    std::string thoughtStream = "Aguardando processamento...";
    std::string projectMap = "";
    bool autonomousMode = true;

    void detectProjectTech();
    void parseDependencies();
    void generateProjectMap();
    void runPythonAgent(const std::string& goal, const std::string& mode = "AGENT");

    // Layout State (Fase 14)
    float splitterPosLeft = 250.0f;
    float splitterPosRight = 300.0f;

    // Telemetry State (Fase 22)
    float gpuLoad = 0.0f;
    float vramUsed = 0.0f;
    float vramTotal = 16.0f;
    std::string activeGpuName = "card0";
    int totalPromptTokens = 0;
    int totalCompletionTokens = 0;
    float tokensPerSec = 0.0f;
    std::mutex telemetryMutex;
    std::atomic<bool> stopTelemetry{false};

    void startTelemetry();
    void stopTelemetryLoop();

    // Sessão (Fase 33)
    void saveSession();
    void loadSession();
    void newDialogue();

private:
    void telemetryLoop();
    void drawMainMenu();
    void drawFileExplorer();
    void renderDirectory(const std::string& path);
    void drawChatWindow();
    void drawThoughtPanel();
    void drawStatsPanel();
    void drawCodeBlock(const std::string& code, const std::string& lang);

    agent::network::OllamaClient* ollama = nullptr;
    agent::core::Orchestrator* orchestrator = nullptr;
};

} // namespace agent::ui
