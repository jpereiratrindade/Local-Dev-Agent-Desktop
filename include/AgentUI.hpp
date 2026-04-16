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
#include <filesystem>
#include <utility>
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
    int selectedReasoning = 1; // 0=low,1=medium,2=high
    int selectedAccess = 1;    // 0=read-only,1=workspace-write,2=full-access
    std::atomic<bool> llmBusy{false};
    bool emojiIconsEnabled = false;
    bool hasOpenProject = true;
    std::string currentSessionFile = "last_session.json";
    bool openFolderFallbackRequested = false;
    bool openFolderFallbackVisible = false;
    char openFolderPathBuf[1024] = "";
    std::string openFolderFallbackError;
    bool openFolderPickerRequested = false;
    bool openFolderPickerVisible = false;
    std::string folderPickerCurrentDir;
    std::string folderPickerSelectedDir;
    char folderPickerPathBuf[1024] = "";
    char newFolderNameBuf[256] = "";
    std::string folderPickerStatus;
    
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
    void drawOpenFolderFallbackDialog();
    void drawOpenFolderPickerDialog();
    std::filesystem::path sessionsDir() const;
    bool loadSessionFromFile(const std::filesystem::path& sessionFile);
    std::string newSessionFileName() const;
    std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> listRecentSessions(std::size_t maxCount = 12) const;

    agent::network::OllamaClient* ollama = nullptr;
    agent::core::Orchestrator* orchestrator = nullptr;
};

} // namespace agent::ui
