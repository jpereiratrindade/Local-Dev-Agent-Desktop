#pragma once

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "TextEditor.h"
#include "NativeTools.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <filesystem>

namespace agent::network {
    class OllamaClient;
    struct Message;
    struct OllamaStreamStats;
    struct OllamaOptions;
}

namespace agent::core {
    class Orchestrator;
}

namespace agent::ui {

struct ChatMessage {
    std::string role;
    std::string text;
};

class AgentUI {
public:
    AgentUI();
    ~AgentUI();

    void render();
    void setOllama(agent::network::OllamaClient* client);
    void setOrchestrator(agent::core::Orchestrator* orch) { this->orchestrator = orch; }

    // State accessible by main/main loop
    bool exitRequested = false;
    bool emojiIconsEnabled = true;
    std::string currentModel = "qwen2.5:14b";
    std::vector<std::string> availableModels;

    // Public API for mission logic
    void newDialogue();
    void triggerRagSync();
    void startTelemetry();
    void stopTelemetryLoop();

private:
    // Core components
    agent::network::OllamaClient* ollama = nullptr;
    agent::core::Orchestrator* orchestrator = nullptr;

    // UI State - Layout
    float splitterPosLeft = 250.0f;
    float splitterPosRight = 320.0f;

    // UI State - Project
    std::string currentProjectRoot = ".";
    std::string currentSessionFile = "last_session.json";
    bool hasOpenProject = false;
    std::string projectMap = "";
    std::string projectGovernance = "";

    // UI State - Editor
    TextEditor codeEditor;
    std::string selectedFile = "";
    std::string editorFilePath = "";
    std::string editorPlainTextBuffer = "";
    std::string editorSavedText = "";
    bool editorUsesPlainText = false;
    bool editorDirty = false;
    std::string lastResolvedProjectRoot = "";
    bool newEntryModeDirectory = false;
    bool newEntryFormVisible = false;
    char newEntryPathBuf[512] = "";

    // UI State - Chat
    std::vector<ChatMessage> history;
    std::mutex msgMutex;
    char inputBuf[4096] = ""; // Expanded
    bool scrollToBottom = false;
    std::atomic<bool> llmBusy{false};
    
    // UI State - Settings
    int selectedProfile = 0;
    std::string reasoning = "medium";
    std::string access = "workspace-write";
    std::string contextSource = "workspace";
    bool autonomousMode = false;
    bool autonomousFeatureEnabled = true;

    // Telemetry State
    float gpuLoad = 0.0f;
    float vramUsed = 0.0f;
    float vramTotal = 0.0f;
    std::string activeGpuName = "N/A";
    std::atomic<bool> stopTelemetry{false};
    std::mutex telemetryMutex;
    
    int totalPromptTokens = 0;
    int totalCompletionTokens = 0;
    float tokensPerSec = 0.0f;
    float tokenRateMs = 0.0f;

    // RAG State
    agent::core::RagStats ragStats;
    std::atomic<float> ragIndexingProgress{0.0f};
    std::atomic<bool> ragIndexingBusy{false};
    std::string ragIndexingStatusMsg = "";

    // Modals State
    bool modelManagerRequested = false;
    bool modelManagerVisible = false;
    bool pullingModel = false;
    std::string pullStatus = "";
    float pullProgress = 0.0f;
    std::string ollamaVersion = "";
    char modelPullNameBuf[128] = "";

    bool openFolderPickerRequested = false;
    bool openFolderPickerVisible = false;
    std::string folderPickerCurrentDir = ".";
    char folderPickerPathBuf[1024] = "";

    bool governedProjectDialogRequested = false;
    bool governedProjectDialogVisible = false;
    char governedProjectNameBuf[128] = "";

    bool contextPolicyDialogRequested = false;
    bool contextPolicyDialogVisible = false;
    char contextLibraryPathsBuf[4096] = "";
    char contextDomainsBuf[1024] = "";

    std::string thoughtStream = "Pronto.";

    // Render Sub-methods
    void drawMainMenu();
    void drawFileExplorer();
    void renderDirectory(const std::string& path);
    void drawFileEditor();
    void loadFileIntoEditor(const std::string& path);
    bool saveEditorFile();
    bool createWorkspaceEntry(const std::string& relativePath, bool directory);
    bool applyTextToActiveFile(const std::string& text, bool saveAfter);
    void generateProjectMap();
    
    void drawChatWindow();
    void renderMarkdown(const std::string& text);
    void runPythonAgent(const std::string& goal, const std::string& mode = "AGENT");
    std::string buildActiveContextBlock() const;
    std::string buildChatSystemPrompt() const;
    std::string inferTaskMode(const std::string& goal) const;
    std::string extractWritableAssistantText(const std::string& text) const;
    
    void drawThoughtPanel();
    void drawStatsPanel();
    void telemetryLoop();

    void drawOpenFolderPickerDialog();
    void drawGovernedProjectDialog();
    void drawContextPolicyDialog();
    void renderModelManagerModal();

    // Session Helpers
    void saveSession();
    void loadSession();
    std::filesystem::path sessionsDir() const;
    std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> listRecentSessions(std::size_t maxCount = 12) const;
    bool loadSessionFromFile(const std::filesystem::path& path);
};

} // namespace agent::ui
