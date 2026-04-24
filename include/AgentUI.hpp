#pragma once

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "TextEditor.h"
#include "NativeTools.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <deque>

namespace agent::network {
    class OllamaClient;
    enum class ModelProvider;
    struct Message;
    struct OllamaStreamStats;
    struct OllamaOptions;
}

namespace agent::core {
    class Orchestrator;
    struct AgentSpec;
}

namespace agent::ui {

struct ChatMessage {
    std::string role;
    std::string text;
};

enum class MessagePartType {
    Text,
    Reasoning,
    ToolCall,
    ToolResult,
};

struct MessagePart {
    MessagePartType type = MessagePartType::Text;
    std::string text;
    std::string name;
    std::string callId;
    bool isError = false;
};

struct StructuredChatMessage {
    std::string role;
    std::vector<MessagePart> parts;
};

enum class ChangeProposalSource {
    AssistantText,
    NativeTool
};

enum class ApprovalState {
    Idle,
    Pending,
    Approved,
    Rejected
};

struct ChangeProposal {
    std::string kind; // replace_file, create_file, append_to_file, insert_at_cursor, apply_patch, write_file
    std::string targetPath;
    std::string content;
    std::string summary;
    std::string confidence = "high"; // low, medium, high
    bool directlyApplicable = false;
    ChangeProposalSource source = ChangeProposalSource::AssistantText;
    std::string nativeToolName;
    std::string nativeToolArgsJson;
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
    std::string currentProvider = "Ollama";
    int currentProviderIndex = 0;
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
    std::string lastChangeTargetPath = "";
    std::deque<std::string> recentFiles;
    bool newEntryModeDirectory = false;
    bool newEntryFormVisible = false;
    char newEntryPathBuf[512] = "";

    // UI State - Chat
    std::vector<ChatMessage> history;
    std::vector<StructuredChatMessage> structuredHistory;
    std::vector<agent::network::Message> llmHistory;
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
    std::string providerEndpoint = "http://localhost:11434";
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
    char pendingChangeTargetBuf[1024] = "";

    // P3.2: Quick File Open (Ctrl+P)
    bool quickOpenVisible = false;
    char quickOpenBuf[256] = "";
    std::vector<std::string> quickOpenMatches;
    std::string pinnedActiveFile; // Arquivo pinado via Quick Open

    std::string thoughtStream = "Pronto.";
    bool changeProposalVisible = false;
    ChangeProposal pendingChangeProposal;
    std::string pendingChangeDiff;

    std::mutex changeApprovalMutex;
    std::condition_variable changeApprovalCv;
    ApprovalState changeApprovalState{ApprovalState::Idle};

    // P0.4: Estado do modal de confirmação para delete_path
    std::atomic<int> deleteApprovalState{0}; // 0=idle, 1=pending, 2=approved, 3=rejected
    std::string deleteApprovalPath;
    std::string deleteApprovalIsRecursive;
    std::mutex deleteApprovalMutex;

    // Render Sub-methods
    void drawMainMenu();
    void syncNativeToolsRuntime();
    void drawFileExplorer();
    void renderDirectory(const std::string& path);
    void drawFileEditor();
    void loadFileIntoEditor(const std::string& path);
    bool saveEditorFile();
    bool createWorkspaceEntry(const std::string& relativePath, bool directory);
    bool applyTextToActiveFile(const std::string& text, bool saveAfter);
    bool applyPartialChangeToEditor(const ChangeProposal& proposal, bool saveAfter);
    bool ensureEditorTarget(const std::string& targetPath);
    void noteFileTouched(const std::string& path);
    void generateProjectMap();
    
    void drawChatWindow();
    StructuredChatMessage buildStructuredMessage(const ChatMessage& message) const;
    std::string flattenStructuredMessageText(const StructuredChatMessage& message) const;
    void ensureStructuredHistoryLocked();
    void appendHistoryMessageLocked(const ChatMessage& message);
    StructuredChatMessage& ensureAssistantStructuredMessageLocked();
    void renderMarkdown(const std::string& text);
    void runPythonAgent(const std::string& goal, const std::string& mode = "AGENT");
    std::string buildActiveContextBlock() const;
    std::string buildChatSystemPrompt() const;
    agent::core::AgentSpec currentAgentSpec() const;
    std::vector<std::string> currentAgentToolNames() const;
    std::string inferTaskMode(const std::string& goal) const;
    std::string inferActiveFileForGoal(const std::string& goal) const;
    std::string inferActiveFileAmbiguityNote(const std::string& goal) const;
    bool buildChangeProposalFromAssistantText(const std::string& text, ChangeProposal& proposal) const;
    std::string loadWorkspaceFileText(const std::string& path) const;
    
    void drawThoughtPanel();
    void drawStatsPanel();
    void telemetryLoop();

    void drawOpenFolderPickerDialog();
    void drawGovernedProjectDialog();
    void drawContextPolicyDialog();
    void drawChangeProposalDialog();
    void drawDeleteApprovalModal();
    void drawQuickOpenModal();
    void renderModelManagerModal();
    std::string buildSimpleDiffPreview(const std::string& oldText, const std::string& newText) const;

    // Session Helpers
    void saveSession();
    void loadSession();
    std::filesystem::path sessionsDir() const;
    std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> listRecentSessions(std::size_t maxCount = 12) const;
    bool loadSessionFromFile(const std::filesystem::path& path);
};

} // namespace agent::ui
