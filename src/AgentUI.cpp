#include "AgentUI.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "OllamaClient.hpp"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <cstring>

namespace fs = std::filesystem;

namespace agent::ui {
namespace {
static std::string normalizeRootPath(const std::string& raw) {
    try {
        if (raw.empty() || raw == ".") return fs::weakly_canonical(fs::current_path()).string();
        return fs::weakly_canonical(fs::path(raw)).string();
    } catch (...) {
        return raw.empty() ? "." : raw;
    }
}

static bool hasProjectMarkers(const fs::path& root) {
    return fs::exists(root / ".agent") ||
           fs::exists(root / "AGENT.md") ||
           fs::exists(root / "PROJECT_CONTEXT.md") ||
           fs::exists(root / "CMakeLists.txt") ||
           fs::exists(root / "package.json") ||
           fs::exists(root / "pyproject.toml") ||
           fs::exists(root / "requirements.txt") ||
           fs::exists(root / "README.md") ||
           fs::exists(root / "ai-governance");
}

static int projectRootScore(const fs::path& root) {
    int score = 0;
    if (fs::exists(root / ".agent" / "sessions" / "last_session.json")) score += 10;
    if (fs::exists(root / ".agent" / "rag")) score += 8;
    if (fs::exists(root / ".agent" / "context_policy.json")) score += 5;
    if (fs::exists(root / "ai-governance")) score += 5;
    if (fs::exists(root / "AGENT.md")) score += 4;
    if (fs::exists(root / "PROJECT_CONTEXT.md")) score += 3;
    if (fs::exists(root / "CMakeLists.txt")) score += 3;
    if (fs::exists(root / "pyproject.toml")) score += 3;
    if (fs::exists(root / "package.json")) score += 3;
    if (fs::exists(root / "README.md")) score += 1;
    return score;
}

static std::string resolveProjectRoot(const std::string& rawRoot) {
    fs::path base;
    try {
        base = fs::weakly_canonical(fs::path(rawRoot.empty() ? "." : rawRoot));
    } catch (...) {
        return normalizeRootPath(rawRoot);
    }

    fs::path best = base;
    int bestScore = projectRootScore(base);

    try {
        for (const auto& entry : fs::directory_iterator(base)) {
            if (!entry.is_directory()) continue;
            const fs::path child = entry.path();
            if (!hasProjectMarkers(child)) continue;
            int score = projectRootScore(child);
            if (score > bestScore) {
                best = child;
                bestScore = score;
            }
        }
    } catch (...) {
    }

    return normalizeRootPath(best.string());
}

static std::string projectLabelFromRoot(const std::string& root) {
    try {
        fs::path p(root);
        std::string name = p.filename().string();
        if (name.empty()) name = p.string();
        return name.empty() ? "(sem pasta)" : name;
    } catch (...) {
        return root.empty() ? "(sem pasta)" : root;
    }
}

static std::string modelHintFor(const std::string& model) {
    std::string s = model;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (s.find("code") != std::string::npos || s.find("coder") != std::string::npos || s.find("deepseek") != std::string::npos) return "Vocação: programação/refatoração";
    if (s.find("vision") != std::string::npos || s.find("vl") != std::string::npos || s.find("llava") != std::string::npos) return "Vocação: imagem + texto";
    if (s.find("instruct") != std::string::npos || s.find("chat") != std::string::npos) return "Vocação: chat geral/instruções";
    if (s.find("qwen") != std::string::npos || s.find("llama") != std::string::npos || s.find("mistral") != std::string::npos || s.find("gemma") != std::string::npos) return "Vocação: uso geral equilibrado";
    return "Vocação: uso geral";
}

static const char* reasoningLabel(int idx) {
    static const char* labels[] = {"low", "medium", "high"};
    if (idx < 0 || idx > 2) return "medium";
    return labels[idx];
}

static agent::core::AccessLevel accessFromIdx(int idx) {
    if (idx <= 0) return agent::core::AccessLevel::ReadOnly;
    if (idx >= 2) return agent::core::AccessLevel::FullAccess;
    return agent::core::AccessLevel::WorkspaceWrite;
}

static const char* accessLabel(int idx) {
    static const char* labels[] = {"Read-only", "Workspace-write", "Full-access"};
    if (idx < 0 || idx > 2) return "Workspace-write";
    return labels[idx];
}

static const char* profileLabel(int idx) {
    static const char* labels[] = {
        "general", "coding", "analysis", "review",
        "writing-outline", "writing-argument", "writing-chapter", "writing-review",
        "research", "research-project"
    };
    if (idx < 0 || idx > 9) return "general";
    return labels[idx];
}

static const char* profileUiLabel(int idx) {
    static const char* labels[] = {
        "Uso geral",
        "Codar",
        "Analise",
        "Review",
        "Writing Outline",
        "Writing Argumento",
        "Writing Capitulo",
        "Writing Review",
        "Pesquisa",
        "Projeto Pesquisa"
    };
    if (idx < 0 || idx > 9) return "Uso geral";
    return labels[idx];
}

static std::string profileHintFor(const std::string& profile) {
    if (profile == "coding") return "Implementar, editar, validar, corrigir build e testar";
    if (profile == "analysis") return "Sintetizar com evidencias";
    if (profile == "review") return "Apontar bugs e riscos";
    if (profile == "writing-outline") return "Estruturar tese e secoes";
    if (profile == "writing-argument") return "Articular conceitos e tensoes";
    if (profile == "writing-chapter") return "Redigir prosa de capitulo";
    if (profile == "writing-review") return "Revisar tese, coesao e rigor";
    if (profile == "research") return "Mapear referencias e hipoteses";
    if (profile == "research-project") return "Desenhar problema, objetivos, metodo, corpus e entregaveis";
    return "Equilibrio geral";
}

static bool profilePrefersAutonomous(const std::string& profile) {
    return profile == "coding" || profile == "review" || profile == "analysis";
}

static bool profileDiscouragesAutonomous(const std::string& profile) {
    return profile == "writing-outline" ||
           profile == "writing-argument" ||
           profile == "writing-chapter" ||
           profile == "writing-review" ||
           profile == "research" ||
           profile == "research-project";
}

static const char* contextSourceLabel(int idx) {
    static const char* labels[] = {
        "workspace",
        "workspace+library",
        "workspace+library+web"
    };
    if (idx < 0 || idx > 2) return "workspace";
    return labels[idx];
}

static agent::core::ContextSourceMode contextSourceModeFromIdx(int idx) {
    if (idx <= 0) return agent::core::ContextSourceMode::WorkspaceOnly;
    if (idx == 1) return agent::core::ContextSourceMode::WorkspaceAndLibrary;
    return agent::core::ContextSourceMode::WorkspaceLibraryAndWeb;
}

static std::string contextSourceHintFor(int idx) {
    if (idx == 1) return "Workspace + biblioteca local aprovada";
    if (idx == 2) return "Workspace + biblioteca local + web aprovada";
    return "Somente arquivos do projeto";
}

static std::string trimLoose(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    if (value.size() >= 2) {
        if ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')) {
            value = value.substr(1, value.size() - 2);
        }
    }
    return value;
}

static std::vector<std::string> splitCsvList(const std::string& raw) {
    std::vector<std::string> out;
    std::stringstream ss(raw);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trimLoose(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

static std::string joinLines(const std::vector<std::string>& values) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) out += "\n";
        out += values[i];
    }
    return out;
}

static std::string joinCommaList(const std::vector<std::string>& values) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) out += ", ";
        out += values[i];
    }
    return out;
}

static std::vector<std::string> splitLineList(const std::string& raw) {
    std::vector<std::string> out;
    std::stringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        line = trimLoose(line);
        if (!line.empty()) out.push_back(normalizeRootPath(line));
    }
    return out;
}

static std::string iconLabel(bool emojiEnabled, const std::string& withEmoji, const std::string& plain) {
    return emojiEnabled ? withEmoji : plain;
}

static std::string pickFolderDialog(const std::string& preferredPath) {
    pfd::settings::rescan();
    if (!pfd::settings::available()) return "";

    std::vector<std::string> candidates;
    if (!preferredPath.empty()) candidates.push_back(preferredPath);
    candidates.push_back(normalizeRootPath("."));
    const char* home = std::getenv("HOME");
    if (home && *home) candidates.push_back(std::string(home));

    for (const auto& candidate : candidates) {
        try {
            std::string normalized = normalizeRootPath(candidate);
            if (!normalized.empty() && fs::exists(normalized)) {
                auto dir = pfd::select_folder("Selecionar Projeto Agent", normalized, pfd::opt::force_path).result();
                if (!dir.empty()) return dir;
            }
        } catch (...) {}
    }
    auto dir = pfd::select_folder("Selecionar Projeto Agent").result();
    return dir;
}

static std::string trimPathInput(std::string value) {
    // Trim whitespace/newlines copied from terminal and surrounding quotes.
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    if (value.size() >= 2) {
        if ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')) {
            value = value.substr(1, value.size() - 2);
        }
    }
    return value;
}

static TextEditor::LanguageDefinition languageForPath(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".hpp" || ext == ".h" || ext == ".c") {
        return TextEditor::LanguageDefinition::CPlusPlus();
    }
    if (ext == ".sql") return TextEditor::LanguageDefinition::SQL();
    if (ext == ".lua") return TextEditor::LanguageDefinition::Lua();
    return TextEditor::LanguageDefinition::C();
}

static bool shouldUsePlainTextEditor(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".md" || ext == ".txt" || ext == ".rst";
}

struct ActionStep {
    std::string title;
    std::string content;
};

struct AgentMessageSections {
    std::string answer;
    std::string actions;
    std::string logs;
    std::vector<ActionStep> actionSteps;
};

static AgentMessageSections splitAgentMessage(const std::string& text) {
    AgentMessageSections sections;
    sections.answer = text;

    std::regex jsonBlockRegex("```json\\s*([\\s\\S]*?)\\s*```");
    for (std::sregex_iterator it(text.begin(), text.end(), jsonBlockRegex), end; it != end; ++it) {
        if (!sections.actions.empty()) sections.actions += "\n\n";
        const std::string block = (*it)[1].str();
        sections.actions += block;

        ActionStep step;
        step.title = "Ação " + std::to_string(sections.actionSteps.size() + 1);
        step.content = block;
        sections.actionSteps.push_back(step);
    }

    sections.answer = std::regex_replace(sections.answer, jsonBlockRegex, "");
    sections.answer = std::regex_replace(sections.answer, std::regex("\n\\s*---\\s*\n"), "\n");

    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("OBSERVAÇÃO") != std::string::npos ||
            line.find("AÇÃO:") != std::string::npos ||
            line.find("PASSO") != std::string::npos ||
            line.find("Missão") != std::string::npos ||
            line.find("MISSION") != std::string::npos) {
            sections.logs += line + "\n";
        }
    }

    // Tenta enriquecer os títulos com "PASSO N / M" quando houver no texto/log.
    std::regex stepRegex("PASSO\\s+([0-9]+)\\s*/\\s*([0-9]+)");
    std::vector<std::string> detectedStepTitles;
    for (std::sregex_iterator it(text.begin(), text.end(), stepRegex), end; it != end; ++it) {
        detectedStepTitles.push_back("PASSO " + (*it)[1].str() + "/" + (*it)[2].str());
    }
    for (size_t i = 0; i < sections.actionSteps.size() && i < detectedStepTitles.size(); ++i) {
        sections.actionSteps[i].title = detectedStepTitles[i];
    }

    return sections;
}
} // namespace

void AgentUI::setOllama(agent::network::OllamaClient* client) {
    ollama = client;
    if (!currentProjectRoot.empty()) currentProjectRoot = resolveProjectRoot(currentProjectRoot);
    if (ollama) {
        const std::string effectiveRoot = currentProjectRoot.empty() ? normalizeRootPath(".") : currentProjectRoot;
        agent::core::registerNativeTools(effectiveRoot);
        agent::core::setNativeToolAccessLevel(accessFromIdx(selectedAccess));
        loadContextPolicy();
        applyContextPolicy();
        if (orchestrator) delete orchestrator;
        orchestrator = new agent::core::Orchestrator(ollama, effectiveRoot);
        if (!currentProjectRoot.empty()) {
            hasOpenProject = true;
            detectProjectTech();
        } else {
            hasOpenProject = false;
            thoughtStream = "Nenhuma pasta aberta.";
        }
    }
}

void AgentUI::render() {
    // 1. Menu e Atalhos Globais
    drawMainMenu();
    
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
         std::string startDir = (hasOpenProject && !currentProjectRoot.empty()) ? currentProjectRoot : normalizeRootPath(".");
         folderPickerCurrentDir = startDir;
         folderPickerSelectedDir.clear();
         std::snprintf(folderPickerPathBuf, sizeof(folderPickerPathBuf), "%s", startDir.c_str());
         openFolderPickerRequested = true;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N)) {
         newDialogue();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && !editorFilePath.empty()) {
         saveEditorFile();
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float statsHeight = 85.0f;
    float menuBarHeight = ImGui::GetFrameHeight();
    float mainAreaHeight = viewport->Size.y - menuBarHeight - statsHeight - 5.0f;

    // 2. Layout Estável via Tabela (Fase 28)
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, mainAreaHeight));
    
    // Usamos uma Window invisível para abrigar a tabela principal
    ImGui::Begin("MainLayoutHost", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    
    if (ImGui::BeginTable("MainWorkspaceTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("ExplorerCol", ImGuiTableColumnFlags_WidthFixed, splitterPosLeft);
        ImGui::TableSetupColumn("ChatCol", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("InspectorCol", ImGuiTableColumnFlags_WidthFixed, splitterPosRight);

        ImGui::TableNextRow(ImGuiTableRowFlags_None, mainAreaHeight);
        
        ImGui::TableNextColumn();
        drawFileExplorer();

        ImGui::TableNextColumn();
        drawChatWindow();

        ImGui::TableNextColumn();
        drawThoughtPanel();

        ImGui::EndTable();
    }
    ImGui::End();

    // 3. Rodapé
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - statsHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, statsHeight));
    drawStatsPanel();
    drawOpenFolderPickerDialog();
    drawGovernedProjectDialog();
    drawOpenFolderFallbackDialog();
    drawContextPolicyDialog();
}

void AgentUI::drawMainMenu() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Arquivo")) {
            if (ImGui::MenuItem("Novo Diálogo", "Ctrl+N")) { newDialogue(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Abrir Pasta...", "Ctrl+O")) {
                std::string startDir = (hasOpenProject && !currentProjectRoot.empty()) ? currentProjectRoot : normalizeRootPath(".");
                folderPickerCurrentDir = startDir;
                folderPickerSelectedDir.clear();
                std::snprintf(folderPickerPathBuf, sizeof(folderPickerPathBuf), "%s", startDir.c_str());
                openFolderPickerRequested = true;
            }
            if (ImGui::MenuItem("Novo Projeto Governado...")) {
                std::string base = (hasOpenProject && !currentProjectRoot.empty()) ? currentProjectRoot : normalizeRootPath(".");
                std::snprintf(governedProjectBasePathBuf, sizeof(governedProjectBasePathBuf), "%s", base.c_str());
                governedProjectNameBuf[0] = '\0';
                governedProjectStatus.clear();
                governedProjectType = 0;
                governedProjectDialogRequested = true;
            }
            if (ImGui::MenuItem("Abrir Diálogo...")) {
                fs::path sessions = sessionsDir();
                auto files = pfd::open_file("Abrir diálogo", sessions.string(), {"Sessões JSON", "*.json"}, pfd::opt::none).result();
                if (!files.empty()) {
                    if (loadSessionFromFile(files[0])) {
                        thoughtStream = "Diálogo restaurado: " + fs::path(files[0]).filename().string();
                    } else {
                        thoughtStream = "Falha ao carregar diálogo selecionado.";
                    }
                }
            }
            if (ImGui::BeginMenu("Diálogos Recentes")) {
                auto recent = listRecentSessions(15);
                if (recent.empty()) {
                    ImGui::TextDisabled("Sem sessões salvas.");
                } else {
                    for (const auto& [path, _ts] : recent) {
                        const std::string fileName = path.filename().string();
                        const bool isCurrent = (fileName == currentSessionFile);
                        if (ImGui::MenuItem(fileName.c_str(), nullptr, isCurrent, true)) {
                            if (loadSessionFromFile(path)) {
                                thoughtStream = "Diálogo restaurado: " + fileName;
                            } else {
                                thoughtStream = "Falha ao carregar diálogo: " + fileName;
                            }
                        }
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Fechar Pasta")) {
                currentProjectRoot.clear();
                selectedFile.clear();
                projectMap = "";
                hasOpenProject = false;
                history.clear();
                thoughtStream = "Pasta fechada.";
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Sair", "Alt+F4")) { exitRequested = true; }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Preferências")) {
            if (ImGui::BeginMenu("Modelo LLM Local")) {
                for (const auto& model : availableModels) {
                    bool isSelected = (currentModel == model);
                    std::string menuLabel = model + "  [" + modelHintFor(model) + "]";
                    if (ImGui::MenuItem(menuLabel.c_str(), nullptr, isSelected))
                        currentModel = model;
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Permitir modo autônomo", nullptr, autonomousFeatureEnabled)) {
                autonomousFeatureEnabled = !autonomousFeatureEnabled;
                if (!autonomousFeatureEnabled) autonomousMode = false;
            }
            if (ImGui::MenuItem("Fontes de Contexto...")) {
                std::snprintf(contextLibraryPathsBuf, sizeof(contextLibraryPathsBuf), "%s", joinLines(approvedLibraryPaths).c_str());
                std::snprintf(contextDomainsBuf, sizeof(contextDomainsBuf), "%s", approvedDomainsCsv.c_str());
                contextPolicyStatus.clear();
                contextPolicyDialogRequested = true;
            }
            ImGui::EndMenu();
        }
        
        std::string projectLabel = hasOpenProject ? projectLabelFromRoot(currentProjectRoot) : "(sem pasta)";
        std::string projectStatus = "Projeto: " + projectLabel;
        ImGui::SameLine(ImGui::GetWindowWidth() - 420);
        ImGui::TextDisabled("%s", projectStatus.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", currentProjectRoot.c_str());
        }
        
        ImGui::EndMainMenuBar();
    }
}

void AgentUI::drawFileExplorer() {
    ImGui::BeginChild("ExplorerArea", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "DIALOGOS");
    ImGui::Separator();

    if (hasOpenProject && !currentProjectRoot.empty()) {
        auto recent = listRecentSessions(20);
        if (recent.empty()) {
            ImGui::TextDisabled("Sem dialogos salvos.");
        } else {
            ImGui::BeginChild("RecentDialogsList", ImVec2(0, 140), true);
            for (const auto& [path, _ts] : recent) {
                const std::string fileName = path.filename().string();
                const bool isCurrent = (fileName == currentSessionFile);
                std::string label = (isCurrent ? "> " : "  ") + fileName;
                if (ImGui::Selectable(label.c_str(), isCurrent)) {
                    if (loadSessionFromFile(path)) {
                        thoughtStream = "Diálogo restaurado: " + fileName;
                    } else {
                        thoughtStream = "Falha ao carregar diálogo: " + fileName;
                    }
                }
            }
            ImGui::EndChild();
        }
    } else {
        ImGui::TextDisabled("Abra uma pasta para ver dialogos.");
    }
    
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "EXPLORER");
    ImGui::Separator();
    
    if (hasOpenProject && !currentProjectRoot.empty() && fs::exists(currentProjectRoot)) {
        float editorHeight = editorFilePath.empty() ? 110.0f : 340.0f;
        ImGui::BeginChild("ExplorerTreeArea", ImVec2(0, -editorHeight), true);
        renderDirectory(currentProjectRoot);
        ImGui::EndChild();
    } else {
        ImGui::TextDisabled("Nenhuma pasta aberta.");
    }

    ImGui::Spacing();
    drawFileEditor();
    
    ImGui::EndChild();
}

void AgentUI::renderDirectory(const std::string& path) {
    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            const auto& entryPath = entry.path();
            std::string filename = entryPath.filename().string();
            
            if (entry.is_directory()) {
                std::string folderLabel = iconLabel(emojiIconsEnabled, "📁 " + filename, "[DIR] " + filename);
                if (ImGui::TreeNode(folderLabel.c_str())) {
                    renderDirectory(entryPath.string());
                    ImGui::TreePop();
                }
            } else {
                std::string fileLabel = iconLabel(emojiIconsEnabled, "📄 " + filename, "[FILE] " + filename);
                bool isSelected = (selectedFile == entryPath.string());
                if (ImGui::Selectable(fileLabel.c_str(), isSelected)) {
                    selectedFile = entryPath.string();
                    loadFileIntoEditor(selectedFile);
                }
            }
        }
    } catch (...) {}
}

void AgentUI::loadFileIntoEditor(const std::string& path) {
    if (path.empty()) return;
    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            thoughtStream = "Falha ao abrir arquivo no editor: " + path;
            return;
        }
        std::stringstream buffer;
        buffer << ifs.rdbuf();
        editorUsesPlainText = shouldUsePlainTextEditor(fs::path(path));
        editorPlainTextBuffer = buffer.str();
        editorSavedText = editorPlainTextBuffer;
        codeEditor.SetLanguageDefinition(languageForPath(fs::path(path)));
        codeEditor.SetPalette(TextEditor::GetDarkPalette());
        codeEditor.SetText(editorPlainTextBuffer);
        codeEditor.SetReadOnly(false);
        editorFilePath = path;
        editorDirty = false;
        thoughtStream = "Arquivo carregado no editor: " + fs::path(path).filename().string();
    } catch (...) {
        thoughtStream = "Erro ao carregar arquivo no editor.";
    }
}

bool AgentUI::saveEditorFile() {
    if (editorFilePath.empty()) return false;
    try {
        std::ofstream ofs(editorFilePath);
        if (!ofs.is_open()) {
            thoughtStream = "Falha ao salvar arquivo: " + editorFilePath;
            return false;
        }
        const std::string content = editorUsesPlainText ? editorPlainTextBuffer : codeEditor.GetText();
        ofs << content;
        editorSavedText = content;
        editorDirty = false;
        thoughtStream = "Arquivo salvo: " + fs::path(editorFilePath).filename().string();
        return true;
    } catch (...) {
        thoughtStream = "Erro ao salvar arquivo em edição.";
        return false;
    }
}

void AgentUI::drawFileEditor() {
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "EDITOR");
    ImGui::Separator();

    if (editorFilePath.empty()) {
        ImGui::TextDisabled("Selecione um arquivo no explorer para abrir no editor.");
        return;
    }

    editorDirty = editorUsesPlainText ? (editorPlainTextBuffer != editorSavedText) : (codeEditor.GetText() != editorSavedText);
    std::string fileName = fs::path(editorFilePath).filename().string();
    if (editorDirty) fileName += " *";
    ImGui::TextWrapped("%s", fileName.c_str());

    if (ImGui::SmallButton("Salvar")) {
        saveEditorFile();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Usar como Contexto")) {
        selectedFile = editorFilePath;
        thoughtStream = "Arquivo ativo para contexto: " + fs::path(editorFilePath).filename().string();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Recarregar")) {
        loadFileIntoEditor(editorFilePath);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", editorDirty ? "Nao salvo" : "Salvo");

    ImGui::BeginChild("InlineFileEditor", ImVec2(0, 260), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (editorUsesPlainText) {
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
        bool changed = ImGui::InputTextMultiline("##PlainTextEditor", &editorPlainTextBuffer, ImVec2(-1, -1), flags);
        if (changed) editorDirty = (editorPlainTextBuffer != editorSavedText);
    } else {
        codeEditor.Render("ProjectFileEditor");
        editorDirty = (codeEditor.GetText() != editorSavedText);
    }
    ImGui::EndChild();
}

void AgentUI::drawChatWindow() {
    ImGui::BeginChild("ChatWindowChild", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "AGENT CHAT");
    ImGui::Separator();

    // 1. Altura do Rodapé
    float footerHeight = ImGui::GetFrameHeightWithSpacing() + 45.0f;
    if (!selectedFile.empty()) footerHeight += 22.0f;

    // 2. Histórico
    ImGui::BeginChild("ChatHistory", ImVec2(0, -footerHeight), false);
    {
        std::lock_guard<std::mutex> lock(msgMutex);
        for (size_t i = 0; i < history.size(); ++i) {
            const auto& msg = history[i];
            if (msg.role == "user") {
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "VOCÊ:");
                ImGui::TextWrapped("%s", msg.text.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "AGENT:");
                AgentMessageSections sections = splitAgentMessage(msg.text);

                ImGui::PushID(static_cast<int>(i));
                if (ImGui::SmallButton("Copiar")) {
                    ImGui::SetClipboardText(msg.text.c_str());
                    thoughtStream = "Resposta copiada para a área de transferência.";
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Copiar Bloco")) {
                    std::string wrapped = "```text\n" + msg.text + "\n```";
                    ImGui::SetClipboardText(wrapped.c_str());
                    thoughtStream = "Resposta copiada como bloco Markdown.";
                }
                ImGui::PopID();

                float availWidth = ImGui::GetContentRegionAvail().x;
                float wrappedHeight = ImGui::CalcTextSize(msg.text.c_str(), nullptr, false, availWidth - 16.0f).y;
                float blockHeight = wrappedHeight + 16.0f;
                if (blockHeight < 48.0f) blockHeight = 48.0f;
                float maxByViewport = ImGui::GetWindowHeight() * 0.72f;
                if (blockHeight > maxByViewport) blockHeight = maxByViewport;

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.07f, 0.1f, 0.9f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
                ImGui::BeginChild(("AgentMsgBlock_" + std::to_string(i)).c_str(), ImVec2(0, blockHeight), true, ImGuiWindowFlags_AlwaysUseWindowPadding);
                if (ImGui::BeginTabBar(("AgentTabs_" + std::to_string(i)).c_str(), ImGuiTabBarFlags_FittingPolicyResizeDown)) {
                    if (ImGui::BeginTabItem("Resposta")) {
                        ImGui::PushID(("copy_answer_" + std::to_string(i)).c_str());
                        if (ImGui::SmallButton("Copiar Resposta")) {
                            ImGui::SetClipboardText(sections.answer.c_str());
                            thoughtStream = "Resposta final copiada.";
                        }
                        ImGui::PopID();
                        ImGui::Separator();
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextUnformatted(sections.answer.c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Ações")) {
                        ImGui::PushID(("copy_actions_" + std::to_string(i)).c_str());
                        if (ImGui::SmallButton("Copiar Ações")) {
                            ImGui::SetClipboardText(sections.actions.c_str());
                            thoughtStream = "Ações copiadas.";
                        }
                        ImGui::PopID();
                        ImGui::Separator();
                        if (sections.actions.empty()) ImGui::TextDisabled("Sem ações detectadas.");
                        else {
                            for (size_t stepIdx = 0; stepIdx < sections.actionSteps.size(); ++stepIdx) {
                                const auto& step = sections.actionSteps[stepIdx];
                                std::string header = step.title + "##" + std::to_string(i) + "_" + std::to_string(stepIdx);
                                if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                                    ImGui::PushTextWrapPos(0.0f);
                                    ImGui::TextUnformatted(step.content.c_str());
                                    ImGui::PopTextWrapPos();
                                }
                            }
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Logs")) {
                        ImGui::PushID(("copy_logs_" + std::to_string(i)).c_str());
                        if (ImGui::SmallButton("Copiar Logs")) {
                            ImGui::SetClipboardText(sections.logs.c_str());
                            thoughtStream = "Logs copiados.";
                        }
                        ImGui::PopID();
                        ImGui::Separator();
                        if (sections.logs.empty()) ImGui::TextDisabled("Sem logs detectados.");
                        else ImGui::TextUnformatted(sections.logs.c_str());
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
            ImGui::Separator();
        }
    }
    if (scrollToBottom) { ImGui::SetScrollHereY(1.0f); scrollToBottom = false; }
    ImGui::EndChild();

    ImGui::Separator();

    // 3. Input Fixo
    {
        std::lock_guard<std::mutex> lock(msgMutex);
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (it->role == "assistant") {
                if (ImGui::SmallButton("Copiar Última Resposta")) {
                    ImGui::SetClipboardText(it->text.c_str());
                    thoughtStream = "Última resposta copiada.";
                }
                break;
            }
        }
    }
    ImGui::SameLine();
    if (!selectedFile.empty()) {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "Ativo: %s", fs::path(selectedFile).filename().string().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Desativar Contexto")) selectedFile = "";
    }
    ImGui::SameLine();
    const std::string activeProfile = profileLabel(selectedProfile);
    if (!autonomousFeatureEnabled) autonomousMode = false;
    if (profileDiscouragesAutonomous(activeProfile)) autonomousMode = false;
    if (!autonomousFeatureEnabled) ImGui::BeginDisabled();
    ImGui::Checkbox("Modo Autônomo", &autonomousMode);
    if (!autonomousFeatureEnabled) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Modo autônomo desativado em Preferências.");
        }
    } else if (profileDiscouragesAutonomous(activeProfile)) {
        if (autonomousMode) autonomousMode = false;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Perfis de escrita e pesquisa usam Chat Assistido por padrão para preservar continuidade e reduzir exploração mecânica.");
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", autonomousMode ? "Autônomo" : "Chat Assistido");
    ImGui::SameLine();
    bool busyNow = llmBusy.load() || (ollama && ollama->isStreaming());
    if (busyNow) {
        if (ImGui::SmallButton("Interromper")) {
            if (orchestrator) orchestrator->stopMission();
            if (ollama) ollama->requestStop();
            llmBusy = false;
            thoughtStream = "Interrupção solicitada.";
        }
    }

    ImGui::Separator();

    ImGui::TextDisabled("Modelo");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##ModelInline", currentModel.c_str())) {
        for (const auto& model : availableModels) {
            bool selected = (model == currentModel);
            std::string label = model + " [" + modelHintFor(model) + "]";
            if (ImGui::Selectable(label.c_str(), selected)) currentModel = model;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Reasoning");
    ImGui::SameLine();
    const char* reasoningItems[] = {"low", "medium", "high"};
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("##ReasoningInline", &selectedReasoning, reasoningItems, IM_ARRAYSIZE(reasoningItems));
    ImGui::SameLine();
    ImGui::TextDisabled("Acesso");
    ImGui::SameLine();
    const char* accessItems[] = {"Read-only", "Workspace-write", "Full-access"};
    ImGui::SetNextItemWidth(150);
    if (ImGui::Combo("##AccessInline", &selectedAccess, accessItems, IM_ARRAYSIZE(accessItems))) {
        agent::core::setNativeToolAccessLevel(accessFromIdx(selectedAccess));
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Contexto");
    ImGui::SameLine();
    const char* contextItems[] = {"Workspace", "Workspace + Biblioteca", "Workspace + Biblioteca + Web"};
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("##ContextSourceInline", &selectedContextSource, contextItems, IM_ARRAYSIZE(contextItems))) {
        applyContextPolicy();
        saveContextPolicy();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Perfil");
    ImGui::SameLine();
    const char* profileItems[] = {
        "Uso geral", "Codar", "Analise", "Review",
        "Writing Outline", "Writing Argumento", "Writing Capitulo", "Writing Review",
        "Pesquisa", "Projeto Pesquisa"
    };
    ImGui::SetNextItemWidth(170);
    int previousProfile = selectedProfile;
    if (ImGui::Combo("##ProfileInline", &selectedProfile, profileItems, IM_ARRAYSIZE(profileItems))) {
        const std::string newProfile = profileLabel(selectedProfile);
        if (profileDiscouragesAutonomous(newProfile)) {
            autonomousMode = false;
        } else if (profilePrefersAutonomous(newProfile) && !profileDiscouragesAutonomous(profileLabel(previousProfile))) {
            autonomousMode = true;
        }
    }

    ImGui::PushItemWidth(-70);
    bool submitted = ImGui::InputText("##Input", inputBuf, IM_ARRAYSIZE(inputBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    
    // Atalho Ninja: Ctrl + Enter
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter)) submitted = true;

    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (submitted || ImGui::Button("Send")) {
        if (inputBuf[0] != '\0' && ollama) {
            std::string userMsg = inputBuf;
            const std::string reasoning = reasoningLabel(selectedReasoning);
            const std::string access = accessLabel(selectedAccess);
            const std::string profile = profileLabel(selectedProfile);
            const std::string profileHint = profileHintFor(profile);
            const std::string contextSource = contextSourceLabel(selectedContextSource);
            const std::string librarySummary = approvedLibraryPaths.empty() ? std::string("(nenhuma)") : joinCommaList(approvedLibraryPaths);
            
            // Contexto V5: Injeção de Autoridade via System Prompt
            std::string systemPrompt = "VOCÊ É O AGENT. MODO ANALISTA TÉCNICO DE ELITE.\n"
                                       "CONTEXTO FÍSICO DO WORKSPACE:\n" + projectMap + "\n"
                                       "DIRETRIZES IMUTÁVEIS:\n"
                                       "1. O mapa acima é a sua única fonte de verdade sobre a estrutura do projeto.\n"
                                       "2. Nunca diga que não conhece o projeto ou peça dados de estrutura.\n"
                                       "3. Se um arquivo estiver em <active_file>, trate-o como seu código principal.\n"
                                       "4. Respostas cordiais e extremamente técnicas baseadas no contexto fornecido.\n"
                                       "5. Nível de reasoning atual: " + reasoning + ".\n"
                                       "6. Nível de acesso permitido para ferramentas: " + access + ".\n"
                                       "7. Perfil de tarefa atual: " + profile + ". Objetivo cognitivo: " + profileHint + ".\n"
                                       "8. Política de contexto atual: " + contextSource + ".\n"
                                       "9. Bibliotecas locais aprovadas: " + librarySummary + ".\n"
                                       "10. Domínios aprovados: " + (approvedDomainsCsv.empty() ? std::string("(nenhum)") : approvedDomainsCsv) + ".\n"
                                       "11. Ao usar contexto externo, separe mentalmente base local, base externa e inferência.\n"
                                       "12. Se a política for workspace only, não tente usar conteúdo fora do projeto.\n"
                                       "8. Se você afirmar que verificou/criou/editou/executou algo, inclua evidência objetiva (comando + saída curta ou caminho de arquivo).\n"
                                       "9. Se o perfil for de writing, pesquisa ou análise, não transforme a resposta em inspeção mecânica do repositório; use o mínimo de exploração necessário e sintetize cedo.\n"
                                       "10. Se o perfil for writing-chapter, responda em prosa estruturada. Se for writing-outline, entregue estrutura. Se for writing-review, entregue crítica editorial/conceitual.";

            std::string fullMsg = "";
            if (!selectedFile.empty()) {
                std::string activeContent;
                if (selectedFile == editorFilePath && !editorFilePath.empty()) {
                    activeContent = editorUsesPlainText ? editorPlainTextBuffer : codeEditor.GetText();
                } else {
                    std::ifstream ifs(selectedFile);
                    std::stringstream buffer;
                    buffer << ifs.rdbuf();
                    activeContent = buffer.str();
                }
                fullMsg += "<active_file name=\"" + fs::path(selectedFile).filename().string() + "\">\n" + activeContent + "\n</active_file>\n";
            }
            fullMsg += "<user_query>\n" + userMsg + "\n</user_query>";

            {
                std::lock_guard<std::mutex> lock(msgMutex);
                history.push_back({"user", userMsg});
                history.push_back({"assistant", "..."});
                agent::core::clearNativeToolUsageSummary();
                contextUsageStatus.clear();
                thoughtStream = "Modo Analista de Elite: Consultando Realidade do Workspace...";
                saveSession();
            }
            llmBusy = true;
            inputBuf[0] = '\0';
            scrollToBottom = true;

            if (autonomousMode && profilePrefersAutonomous(profile)) {
                std::string missionGoal = "[reasoning=" + reasoning + "][access=" + access + "][profile=" + profile + "][context=" + contextSource + "] " + userMsg;
                runPythonAgent(missionGoal, "AGENT");
            } else {
                int promptEstimate = static_cast<int>((fullMsg.size() / 4) + 24);
                std::vector<agent::network::Message> threadHistory;
                {
                    std::lock_guard<std::mutex> lock(msgMutex);
                    for(size_t i = 0; i < history.size() - 2; ++i) {
                        std::string role = history[i].role;
                        if (role == "agent") role = "assistant";
                        threadHistory.push_back({role, history[i].text});
                    }
                    threadHistory.push_back({"user", fullMsg});
                }

                ollama->setModel(currentModel);
                ollama->chatStream(threadHistory, [this](const std::string& chunk) {
                    std::lock_guard<std::mutex> lock(msgMutex);
                    if (!history.empty() && history.back().role == "assistant") {
                        if (history.back().text == "...") history.back().text = "";
                        history.back().text += chunk;
                        scrollToBottom = true;
                    }
                }, [this, promptEstimate](bool success, agent::network::OllamaClient::StreamStats stats) {
                    std::lock_guard<std::mutex> lock(msgMutex);
                    int completionTokens = stats.completion_tokens;
                    if (completionTokens <= 0 && !history.empty() && history.back().role == "assistant") {
                        completionTokens = std::max(1, static_cast<int>(history.back().text.size() / 4));
                    }
                    int promptTokens = stats.prompt_tokens > 0 ? stats.prompt_tokens : promptEstimate;
                    totalPromptTokens += promptTokens;
                    totalCompletionTokens += completionTokens;
                    tokensPerSec = (stats.total_duration_ms > 0) ? (completionTokens / (stats.total_duration_ms / 1000.0)) : 0;
                    thoughtStream = success ? ("Transmissão concluída. (+" + std::to_string(completionTokens) + " tkn)") : "Transmissão interrompida.";
                    llmBusy = false;
                    saveSession();
                }, systemPrompt);
            }
        }
    }
    ImGui::EndChild();
}

void AgentUI::drawThoughtPanel() {
    ImGui::BeginChild("InspectorArea", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "INSPECTOR / CoT");
    ImGui::Separator();
    ImGui::Text("Projeto: %s", projectLabelFromRoot(currentProjectRoot).c_str());
    ImGui::TextDisabled("%s", currentProjectRoot.c_str());
    ImGui::TextColored(ImVec4(0.75f, 0.82f, 0.55f, 1.0f), "Contexto: %s", contextSourceHintFor(selectedContextSource).c_str());
    if (!approvedLibraryPaths.empty()) {
        ImGui::TextWrapped("Bibliotecas locais: %s", joinCommaList(approvedLibraryPaths).c_str());
    }
    if (selectedContextSource == 2 && !approvedDomainsCsv.empty()) {
        ImGui::TextWrapped("Domínios aprovados: %s", approvedDomainsCsv.c_str());
    }
    if (!contextPolicyStatus.empty()) {
        ImGui::TextWrapped("%s", contextPolicyStatus.c_str());
    }
    contextUsageStatus = agent::core::getNativeToolUsageSummary();
    if (!contextUsageStatus.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.85f, 0.72f, 0.4f, 1.0f), "Uso efetivo de bases:");
        ImGui::TextWrapped("%s", contextUsageStatus.c_str());
    }
    ImGui::Separator();
    
    // Automação: Exibir dependências detectadas
    if (!detectedDeps.empty()) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Dependências:");
        ImGui::TextWrapped("%s", detectedDeps.c_str());
        ImGui::Separator();
    }

    ImGui::BeginChild("ThoughtScroll");
    ImGui::TextWrapped("%s", thoughtStream.c_str());
    ImGui::EndChild();

    // --- SEÇÃO RAG MONITOR ---
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "RAG MONITOR");
    
    // Atualizar stats periodicamente
    static auto lastRagUpdate = std::chrono::steady_clock::now();
    if (std::chrono::steady_clock::now() - lastRagUpdate > std::chrono::seconds(2)) {
        ragStats = agent::core::getNativeToolRagStats();
        lastRagUpdate = std::chrono::steady_clock::now();
    }

    ImGui::Text("Documentos Indexados: %d", ragStats.docCount);
    float sizeMb = static_cast<float>(ragStats.cacheSizeBytes) / (1024.0f * 1024.0f);
    ImGui::Text("Tamanho do Cache: %.2f MB", sizeMb);

    if (ragIndexingBusy) {
        ImGui::BeginDisabled();
        ImGui::Button("Sincronizando...", ImVec2(-1, 0));
        ImGui::EndDisabled();
        ImGui::ProgressBar(ragIndexingProgress, ImVec2(-1, 0));
        ImGui::TextDisabled("%s", ragIndexingStatusMsg.c_str());
    } else {
        if (ImGui::Button("Sincronizar Bibliotecas Agora", ImVec2(-1, 0))) {
            triggerRagSync();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Inicia a varredura e indexação de todos os arquivos nas pastas de referência.");
        }
    }
    
    ImGui::EndChild();
}

void AgentUI::drawStatsPanel() {
    ImGui::Begin("Dashboard Telemetria", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    
    std::lock_guard<std::mutex> lock(telemetryMutex);
    ImGui::Columns(5);
    ImGui::SetColumnWidth(0, 220);
    ImGui::SetColumnWidth(1, 200);
    ImGui::SetColumnWidth(2, 180);
    ImGui::SetColumnWidth(3, 320);
    ImGui::Text("AMD GPU [%s]: %.1f%%", activeGpuName.c_str(), gpuLoad); ImGui::NextColumn();
    ImGui::Text("VRAM: %.2f / %.1f GB", vramUsed, vramTotal); ImGui::NextColumn();
    ImGui::Text("RAM: %.1f GB", vramTotal); ImGui::NextColumn();
    
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.8f, 1.0f), "Tokens: %d (%d in) | %.1f t/s", totalCompletionTokens, totalPromptTokens, tokensPerSec);
    ImGui::NextColumn();

    std::string modelBadge = "Modelo: " + (currentModel.empty() ? std::string("N/D") : currentModel);
    std::string hint = modelHintFor(currentModel);
    std::string profile = profileLabel(selectedProfile);
    std::string profileBadge = "Perfil: " + profile + " (" + profileHintFor(profile) + ")";
    std::string contextBadge = "Contexto: " + std::string(contextSourceLabel(selectedContextSource));
    std::string shortHint = hint;
    const std::string prefix = "Vocação: ";
    if (shortHint.rfind(prefix, 0) == 0) shortHint = shortHint.substr(prefix.size());
    ImGui::TextColored(ImVec4(0.85f, 0.8f, 0.45f, 1.0f), "%s", modelBadge.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", shortHint.c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.75f, 1.0f), "%s", profileBadge.c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.75f, 0.7f, 0.95f, 1.0f), "%s", contextBadge.c_str());
    
    ImGui::End();
}

void AgentUI::drawOpenFolderFallbackDialog() {
    if (openFolderFallbackRequested) {
        openFolderFallbackVisible = true;
        openFolderFallbackError.clear();
        ImGui::OpenPopup("Abrir Pasta (Fallback)");
        openFolderFallbackRequested = false;
    }

    if (ImGui::BeginPopupModal("Abrir Pasta (Fallback)", &openFolderFallbackVisible, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Nao foi possivel abrir o seletor de pastas do sistema. Informe o caminho manualmente:");
        ImGui::Separator();
        ImGui::SetNextItemWidth(520.0f);
        ImGui::InputText("##FolderPathFallback", openFolderPathBuf, IM_ARRAYSIZE(openFolderPathBuf));
        if (!openFolderFallbackError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", openFolderFallbackError.c_str());
        }
        ImGui::Separator();

        if (ImGui::Button("Abrir", ImVec2(120, 0))) {
            std::string candidate = normalizeRootPath(trimPathInput(openFolderPathBuf));
            if (fs::exists(candidate) && fs::is_directory(candidate)) {
                currentProjectRoot = candidate;
                setOllama(ollama);
                thoughtStream = "Pasta aberta via fallback: " + candidate;
                openFolderFallbackVisible = false;
                openFolderFallbackError.clear();
                ImGui::CloseCurrentPopup();
            } else {
                openFolderFallbackError = "Caminho inválido ou inacessível: " + candidate;
                thoughtStream = openFolderFallbackError;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(120, 0))) {
            openFolderFallbackVisible = false;
            openFolderFallbackError.clear();
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            openFolderFallbackVisible = false;
            openFolderFallbackError.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void AgentUI::drawGovernedProjectDialog() {
    if (governedProjectDialogRequested) {
        governedProjectDialogVisible = true;
        ImGui::OpenPopup("Novo Projeto Governado");
        governedProjectDialogRequested = false;
    }

    if (ImGui::BeginPopupModal("Novo Projeto Governado", &governedProjectDialogVisible, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Crie um projeto com governança padronizada (DDD, DAI, ADR, ai-governance, GSDD).");
        ImGui::Separator();

        const char* types[] = {"C++", "Python", "Research", "Writing"};
        ImGui::SetNextItemWidth(220.0f);
        ImGui::Combo("Tipo", &governedProjectType, types, IM_ARRAYSIZE(types));

        ImGui::SetNextItemWidth(520.0f);
        ImGui::InputText("Nome", governedProjectNameBuf, IM_ARRAYSIZE(governedProjectNameBuf));
        ImGui::SetNextItemWidth(520.0f);
        ImGui::InputText("Pasta base", governedProjectBasePathBuf, IM_ARRAYSIZE(governedProjectBasePathBuf));

        if (!governedProjectStatus.empty()) {
            ImGui::TextWrapped("%s", governedProjectStatus.c_str());
        }
        ImGui::Separator();

        if (ImGui::Button("Criar", ImVec2(120, 0))) {
            std::string outPath;
            std::string err;
            if (createGovernedProject(governedProjectBasePathBuf, governedProjectNameBuf, governedProjectType, outPath, err)) {
                governedProjectStatus = "Projeto criado em: " + outPath;
                currentProjectRoot = outPath;
                setOllama(ollama);
                thoughtStream = "Projeto governado criado: " + outPath;
            } else {
                governedProjectStatus = "Erro: " + err;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            governedProjectDialogVisible = false;
        }
        ImGui::EndPopup();
    }
}

void AgentUI::drawContextPolicyDialog() {
    if (contextPolicyDialogRequested) {
        contextPolicyDialogVisible = true;
        ImGui::OpenPopup("Fontes de Contexto");
        contextPolicyDialogRequested = false;
    }

    if (ImGui::BeginPopupModal("Fontes de Contexto", &contextPolicyDialogVisible, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Configure fontes aprovadas para ampliar o contexto do agente por projeto.");
        ImGui::Separator();

        const char* contextItems[] = {"Workspace", "Workspace + Biblioteca", "Workspace + Biblioteca + Web"};
        ImGui::SetNextItemWidth(320.0f);
        ImGui::Combo("Modo", &selectedContextSource, contextItems, IM_ARRAYSIZE(contextItems));

        ImGui::TextDisabled("Bibliotecas locais aprovadas (uma por linha)");
        ImGui::InputTextMultiline("##BibliotecasLocais", contextLibraryPathsBuf, IM_ARRAYSIZE(contextLibraryPathsBuf), ImVec2(640.0f, 110.0f));
        if (ImGui::Button("Adicionar pasta...", ImVec2(140, 0))) {
            std::string preferred = approvedLibraryPaths.empty() ? currentProjectRoot : approvedLibraryPaths.back();
            std::string picked = pickFolderDialog(preferred);
            if (!picked.empty()) {
                std::string current = contextLibraryPathsBuf;
                if (!current.empty() && current.back() != '\n') current += "\n";
                current += normalizeRootPath(picked);
                std::snprintf(contextLibraryPathsBuf, sizeof(contextLibraryPathsBuf), "%s", current.c_str());
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Limpar lista", ImVec2(120, 0))) {
            contextLibraryPathsBuf[0] = '\0';
        }

        ImGui::SetNextItemWidth(640.0f);
        ImGui::InputText("Domínios aprovados (CSV)", contextDomainsBuf, IM_ARRAYSIZE(contextDomainsBuf));
        ImGui::TextDisabled("Ex.: embrapa.br, scielo.br, gov.br");

        if (!contextPolicyStatus.empty()) {
            ImGui::TextWrapped("%s", contextPolicyStatus.c_str());
        }
        ImGui::Separator();

        if (ImGui::Button("Aplicar", ImVec2(120, 0))) {
            approvedLibraryPaths = splitLineList(contextLibraryPathsBuf);
            approvedDomainsCsv = trimPathInput(contextDomainsBuf);
            applyContextPolicy();
            saveContextPolicy();
            contextPolicyStatus = "Política de contexto aplicada ao projeto.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(120, 0))) {
            contextPolicyDialogVisible = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void AgentUI::drawOpenFolderPickerDialog() {
    if (openFolderPickerRequested) {
        openFolderPickerVisible = true;
        folderPickerStatus.clear();
        newFolderNameBuf[0] = '\0';
        ImGui::OpenPopup("Abrir Pasta");
        openFolderPickerRequested = false;
    }

    if (ImGui::BeginPopupModal("Abrir Pasta", &openFolderPickerVisible, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (folderPickerCurrentDir.empty()) {
            folderPickerCurrentDir = normalizeRootPath(".");
            std::snprintf(folderPickerPathBuf, sizeof(folderPickerPathBuf), "%s", folderPickerCurrentDir.c_str());
        }

        ImGui::TextWrapped("Selecione a pasta do projeto.");
        ImGui::Separator();

        ImGui::SetNextItemWidth(640.0f);
        ImGui::InputText("Caminho", folderPickerPathBuf, IM_ARRAYSIZE(folderPickerPathBuf));

        if (ImGui::Button("Ir para caminho", ImVec2(130, 0))) {
            std::string typed = normalizeRootPath(trimPathInput(folderPickerPathBuf));
            if (fs::exists(typed) && fs::is_directory(typed)) {
                folderPickerCurrentDir = typed;
                folderPickerSelectedDir.clear();
                folderPickerStatus.clear();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Home", ImVec2(90, 0))) {
            const char* home = std::getenv("HOME");
            if (home && *home) folderPickerCurrentDir = normalizeRootPath(home);
            std::snprintf(folderPickerPathBuf, sizeof(folderPickerPathBuf), "%s", folderPickerCurrentDir.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Subir", ImVec2(90, 0))) {
            fs::path p(folderPickerCurrentDir);
            if (p.has_parent_path()) {
                folderPickerCurrentDir = normalizeRootPath(p.parent_path().string());
                std::snprintf(folderPickerPathBuf, sizeof(folderPickerPathBuf), "%s", folderPickerCurrentDir.c_str());
                folderPickerSelectedDir.clear();
                folderPickerStatus.clear();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Seletor do sistema", ImVec2(170, 0))) {
            std::string picked = pickFolderDialog(folderPickerCurrentDir);
            if (!picked.empty()) {
                folderPickerCurrentDir = normalizeRootPath(picked);
                folderPickerSelectedDir.clear();
                std::snprintf(folderPickerPathBuf, sizeof(folderPickerPathBuf), "%s", folderPickerCurrentDir.c_str());
                folderPickerStatus.clear();
            }
        }

        ImGui::Separator();
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputTextWithHint("##NewFolderName", "Nome da nova pasta", newFolderNameBuf, IM_ARRAYSIZE(newFolderNameBuf));
        ImGui::SameLine();
        if (ImGui::Button("Nova Pasta", ImVec2(120, 0))) {
            std::string folderName = trimPathInput(newFolderNameBuf);
            if (folderName.empty()) {
                folderPickerStatus = "Informe um nome para a nova pasta.";
            } else {
                fs::path newDir = fs::path(folderPickerCurrentDir) / folderName;
                try {
                    if (fs::exists(newDir)) {
                        folderPickerStatus = "A pasta ja existe: " + newDir.string();
                    } else if (fs::create_directories(newDir)) {
                        folderPickerStatus = "Pasta criada: " + newDir.string();
                        folderPickerSelectedDir = normalizeRootPath(newDir.string());
                        newFolderNameBuf[0] = '\0';
                    } else {
                        folderPickerStatus = "Nao foi possivel criar a pasta.";
                    }
                } catch (...) {
                    folderPickerStatus = "Erro ao criar pasta.";
                }
            }
        }
        if (!folderPickerStatus.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", folderPickerStatus.c_str());
        }

        ImGui::Separator();
        ImGui::BeginChild("FolderPickerList", ImVec2(640, 280), true);
        try {
            std::vector<std::string> dirs;
            for (const auto& entry : fs::directory_iterator(folderPickerCurrentDir)) {
                if (entry.is_directory()) dirs.push_back(entry.path().filename().string());
            }
            std::sort(dirs.begin(), dirs.end());

            for (const auto& name : dirs) {
                fs::path full = fs::path(folderPickerCurrentDir) / name;
                bool selected = (folderPickerSelectedDir == full.string());
                std::string dirItem = iconLabel(emojiIconsEnabled, "📁 " + name, "[DIR] " + name);
                if (ImGui::Selectable(dirItem.c_str(), selected)) {
                    folderPickerSelectedDir = full.string();
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    folderPickerCurrentDir = normalizeRootPath(full.string());
                    std::snprintf(folderPickerPathBuf, sizeof(folderPickerPathBuf), "%s", folderPickerCurrentDir.c_str());
                    folderPickerSelectedDir.clear();
                    folderPickerStatus.clear();
                }
            }
        } catch (...) {
            ImGui::TextDisabled("Nao foi possivel listar o diretorio atual.");
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Abrir Pasta Atual", ImVec2(160, 0))) {
            if (fs::exists(folderPickerCurrentDir) && fs::is_directory(folderPickerCurrentDir)) {
                currentProjectRoot = normalizeRootPath(folderPickerCurrentDir);
                setOllama(ollama);
                thoughtStream = "Pasta aberta: " + currentProjectRoot;
                openFolderPickerVisible = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Abrir Selecionada", ImVec2(160, 0))) {
            if (!folderPickerSelectedDir.empty() && fs::exists(folderPickerSelectedDir) && fs::is_directory(folderPickerSelectedDir)) {
                currentProjectRoot = normalizeRootPath(folderPickerSelectedDir);
                setOllama(ollama);
                thoughtStream = "Pasta aberta: " + currentProjectRoot;
                openFolderPickerVisible = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            openFolderPickerVisible = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void AgentUI::drawCodeBlock(const std::string& code, const std::string& lang) {
    // Configura o editor apenas se necessário
    static bool editorInit = false;
    if (!editorInit) {
        codeEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
        codeEditor.SetPalette(TextEditor::GetDarkPalette());
        editorInit = true;
    }

    codeEditor.SetText(code);
    codeEditor.SetReadOnly(true);
    
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    ImGui::BeginChild("CodeSnippet", ImVec2(0, 200), true, ImGuiWindowFlags_HorizontalScrollbar);
    codeEditor.Render("Editor");
    ImGui::EndChild();
    ImGui::PopStyleVar();
}


void AgentUI::detectProjectTech() {
    if (currentProjectRoot.empty()) {
        detectedTech = "Desconhecida/Outra";
        detectedDeps.clear();
        return;
    }
    currentProjectRoot = normalizeRootPath(currentProjectRoot);
    hasOpenProject = true;
    detectedTech = "Desconhecida/Outra";
    detectedDeps = "";
    try {
        cleanupEmptySessions();
        generateProjectMap();
        loadContextPolicy();
        applyContextPolicy();
        if (fs::exists(fs::path(currentProjectRoot) / "requirements.txt") || 
            fs::exists(fs::path(currentProjectRoot) / "setup.py") ||
            fs::exists(fs::path(currentProjectRoot) / "main.py")) {
            detectedTech = "Python";
            parseDependencies();
        }
        else if (fs::exists(fs::path(currentProjectRoot) / "CMakeLists.txt") ||
                 fs::exists(fs::path(currentProjectRoot) / "Makefile")) {
            detectedTech = "C++ / Native";
            parseDependencies();
        }
        else if (fs::exists(fs::path(currentProjectRoot) / "package.json")) {
            detectedTech = "JavaScript / Node.js";
        }
        
        loadSession(); // Carregar histórico do projeto
    } catch (...) {}
    
    thoughtStream = "Projeto: " + projectLabelFromRoot(currentProjectRoot) + "\nRaiz: " + currentProjectRoot +
                   "\nProjeto detectado: " + detectedTech + "\nAnalisando estrutura para automação...";
}

void AgentUI::parseDependencies() {
    try {
        if (detectedTech == "Python") {
            std::ifstream reqFile(fs::path(currentProjectRoot) / "requirements.txt");
            if (reqFile.is_open()) {
                std::string line;
                int count = 0;
                while (std::getline(reqFile, line) && count < 10) {
                    if (!line.empty() && line[0] != '#') {
                        detectedDeps += line + ", ";
                        count++;
                    }
                }
                if (!detectedDeps.empty()) detectedDeps = detectedDeps.substr(0, detectedDeps.length() - 2);
            }
        } else if (detectedTech == "C++ / Native") {
            std::ifstream cmakeFile(fs::path(currentProjectRoot) / "CMakeLists.txt");
            if (cmakeFile.is_open()) {
                std::string line;
                while (std::getline(cmakeFile, line)) {
                    if (line.find("find_package") != std::string::npos) {
                        size_t start = line.find("(") + 1;
                        size_t end = line.find(" ", start);
                        if (end == std::string::npos) end = line.find(")", start);
                        detectedDeps += line.substr(start, end - start) + ", ";
                    }
                }
                if (!detectedDeps.empty()) detectedDeps = detectedDeps.substr(0, detectedDeps.length() - 2);
            }
        }
    } catch (...) {}
}

void AgentUI::generateProjectMap() {
    projectMap = "ESTRUTURA DO PROJETO (ESTILO TREE):\n.\n";
    try {
        auto root = fs::path(currentProjectRoot);
        std::vector<std::string> lines;
        
        int count = 0;
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (count++ > 50) break;
            
            auto path = entry.path();
            auto rel = fs::relative(path, root);
            int depth = std::distance(rel.begin(), rel.end());
            
            // Ignorar build/git
            std::string relStr = rel.string();
            if (relStr.find("build") != std::string::npos || relStr.find(".git") != std::string::npos) continue;

            std::string prefix = "";
            for (int i = 0; i < depth - 1; ++i) prefix += "│   ";
            prefix += entry.is_directory()
                ? iconLabel(emojiIconsEnabled, "├── 📁 ", "├── [DIR] ")
                : iconLabel(emojiIconsEnabled, "├── 📄 ", "├── [FILE] ");
            
            projectMap += prefix + path.filename().string() + "\n";
        }
    } catch (...) {}
    thoughtStream = "Project Map reconstruído (Estilo Tree). " + std::to_string(projectMap.length()) + " bytes.";
}

void AgentUI::startTelemetry() {
    stopTelemetry = false;
    std::thread([this]() { this->telemetryLoop(); }).detach();
}

void AgentUI::stopTelemetryLoop() {
    stopTelemetry = true;
}

void AgentUI::telemetryLoop() {
    while (!stopTelemetry) {
        {
            float bestLoad = 0.0f;
            float bestVram = 0.0f;
            std::string bestCard = "N/A";

            // Sniffer Inteligente: Examinar 4 possíveis cartões
            for (int i = 0; i < 4; ++i) {
                std::string base = "/sys/class/drm/card" + std::to_string(i) + "/device/";
                std::ifstream v(base + "mem_info_vram_used");
                if (v.is_open()) {
                    long long val; v >> val;
                    float vUsedGb = (float)val / 1024.0f / 1024.0f / 1024.0f;
                    
                    // PRIORIDADE MÁXIMA: O cartão que estiver MAIS ATIVO no momento (Fase 37.1)
                    float currentLoad = 0;
                    std::ifstream f(base + "gpu_busy_percent");
                    if (f.is_open()) f >> currentLoad;

                    if (currentLoad > bestLoad || (currentLoad == bestLoad && vUsedGb > bestVram)) {
                        bestLoad = currentLoad;
                        bestVram = vUsedGb;
                        bestCard = "card" + std::to_string(i);
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(telemetryMutex);
                gpuLoad = bestLoad;
                vramUsed = bestVram;
                activeGpuName = bestCard;

                // Global RAM
                std::ifstream m("/proc/meminfo");
                std::string line;
                while (std::getline(m, line)) {
                    if (line.find("MemTotal:") == 0) vramTotal = (float)std::stol(line.substr(9)) / 1024.0f / 1024.0f;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void AgentUI::saveSession() {
    if (!hasOpenProject || currentProjectRoot.empty()) return;
    if (history.empty()) return;
    try {
        fs::path sessionDir = sessionsDir();
        if (!fs::exists(sessionDir)) fs::create_directories(sessionDir);
        
        nlohmann::json j;
        j["history"] = nlohmann::json::array();
        for (const auto& msg : history) {
            j["history"].push_back({{"role", msg.role}, {"text", msg.text}});
        }
        j["tokens"] = {{"prompt", totalPromptTokens}, {"completion", totalCompletionTokens}};
        j["meta"] = {
            {"project_root", currentProjectRoot},
            {"updated_at", newSessionFileName()}
        };
        
        fs::path currentFile = sessionDir / currentSessionFile;
        std::ofstream o(currentFile);
        o << j.dump(2);

        if (currentSessionFile != "last_session.json") {
            std::ofstream last(sessionDir / "last_session.json");
            last << j.dump(2);
        }
    } catch (...) {}
}

void AgentUI::loadSession() {
    if (!hasOpenProject || currentProjectRoot.empty()) { history.clear(); return; }
    try {
        fs::path dir = sessionsDir();
        fs::path target = dir / currentSessionFile;
        if (!fs::exists(target)) target = dir / "last_session.json";
        if (!fs::exists(target)) {
            fs::file_time_type newestTs{};
            fs::path newestFile;
            if (fs::exists(dir)) {
                for (const auto& e : fs::directory_iterator(dir)) {
                    if (!e.is_regular_file() || e.path().extension() != ".json") continue;
                    auto ts = e.last_write_time();
                    if (newestFile.empty() || ts > newestTs) {
                        newestTs = ts;
                        newestFile = e.path();
                    }
                }
            }
            if (!newestFile.empty()) target = newestFile;
        }

        if (!target.empty() && fs::exists(target) && loadSessionFromFile(target)) return;
        history.clear();
    } catch (...) {
        history.clear();
    }
}

void AgentUI::newDialogue() {
    history.clear();
    totalPromptTokens = 0;
    totalCompletionTokens = 0;
    tokensPerSec = 0;
    currentSessionFile = newSessionFileName();
    thoughtStream = "Novo diálogo iniciado. Sessão preparada: " + currentSessionFile + " (será salva ao enviar a primeira mensagem).";
}

fs::path AgentUI::sessionsDir() const {
    if (currentProjectRoot.empty()) return fs::path(".");
    return fs::path(currentProjectRoot) / ".agent" / "sessions";
}

fs::path AgentUI::contextPolicyFile() const {
    if (currentProjectRoot.empty()) return fs::path(".agent/context_policy.json");
    return fs::path(currentProjectRoot) / ".agent" / "context_policy.json";
}

bool AgentUI::loadSessionFromFile(const fs::path& sessionFile) {
    if (!fs::exists(sessionFile)) return false;
    std::ifstream i(sessionFile);
    nlohmann::json j;
    i >> j;

    history.clear();
    if (!j.contains("history") || !j["history"].is_array()) return false;
    for (const auto& msg : j["history"]) {
        std::string role = msg["role"];
        if (role == "agent") role = "assistant";
        history.push_back({role, msg["text"]});
    }
    totalPromptTokens = j["tokens"].value("prompt", 0);
    totalCompletionTokens = j["tokens"].value("completion", 0);
    currentSessionFile = sessionFile.filename().string();
    scrollToBottom = true;
    return true;
}

std::string AgentUI::newSessionFileName() const {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << "session_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".json";
    return oss.str();
}

std::vector<std::pair<fs::path, fs::file_time_type>> AgentUI::listRecentSessions(std::size_t maxCount) const {
    std::vector<std::pair<fs::path, fs::file_time_type>> out;
    if (!hasOpenProject || currentProjectRoot.empty()) return out;
    fs::path dir = sessionsDir();
    if (!fs::exists(dir)) return out;

    for (const auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".json") continue;
        // Evita poluir a lista com sessões vazias.
        try {
            std::ifstream in(e.path());
            nlohmann::json j;
            in >> j;
            if (!j.contains("history") || !j["history"].is_array() || j["history"].empty()) continue;
        } catch (...) {
            continue;
        }
        out.push_back({e.path(), e.last_write_time()});
    }

    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (out.size() > maxCount) out.resize(maxCount);
    return out;
}

int AgentUI::cleanupEmptySessions() {
    if (!hasOpenProject || currentProjectRoot.empty()) return 0;
    fs::path dir = sessionsDir();
    if (!fs::exists(dir)) return 0;

    int removed = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".json") continue;
        if (e.path().filename() == "last_session.json") continue;
        try {
            std::ifstream in(e.path());
            nlohmann::json j;
            in >> j;
            bool empty = (!j.contains("history") || !j["history"].is_array() || j["history"].empty());
            if (empty) {
                fs::remove(e.path());
                removed++;
            }
        } catch (...) {
            // Arquivo corrompido/inválido também pode ser removido para evitar poluir a lista.
            try {
                fs::remove(e.path());
                removed++;
            } catch (...) {}
        }
    }
    return removed;
}

void AgentUI::applyContextPolicy() {
    agent::core::setNativeToolContextSourceMode(contextSourceModeFromIdx(selectedContextSource));
    agent::core::setNativeToolApprovedRoots(approvedLibraryPaths);
    agent::core::setNativeToolApprovedDomains(splitCsvList(approvedDomainsCsv));

    std::string modeText = contextSourceHintFor(selectedContextSource);
    contextPolicyStatus = "Contexto ativo: " + modeText;
    if (!approvedLibraryPaths.empty()) contextPolicyStatus += "\nBibliotecas locais aprovadas: " + joinCommaList(approvedLibraryPaths);
    if (selectedContextSource == 2 && !approvedDomainsCsv.empty()) contextPolicyStatus += "\nDomínios aprovados: " + approvedDomainsCsv;
}

void AgentUI::loadContextPolicy() {
    if (!hasOpenProject || currentProjectRoot.empty()) return;
    try {
        fs::path file = contextPolicyFile();
        if (!fs::exists(file)) {
            approvedLibraryPaths.clear();
            approvedDomainsCsv = "embrapa.br";
            selectedContextSource = 0;
            contextUsageStatus.clear();
            return;
        }
        std::ifstream in(file);
        nlohmann::json j;
        in >> j;
        std::string mode = j.value("context_source", std::string("workspace"));
        if (mode == "workspace+library+web") selectedContextSource = 2;
        else if (mode == "workspace+library") selectedContextSource = 1;
        else selectedContextSource = 0;
        approvedLibraryPaths.clear();
        if (j.contains("approved_library_paths") && j["approved_library_paths"].is_array()) {
            for (const auto& item : j["approved_library_paths"]) {
                if (item.is_string()) approvedLibraryPaths.push_back(item.get<std::string>());
            }
        } else {
            std::string legacyPath = j.value("approved_library_path", std::string());
            if (!legacyPath.empty()) approvedLibraryPaths.push_back(legacyPath);
        }
        approvedDomainsCsv = j.value("approved_domains_csv", std::string("embrapa.br"));
        contextUsageStatus.clear();
    } catch (...) {
        approvedLibraryPaths.clear();
        approvedDomainsCsv = "embrapa.br";
        selectedContextSource = 0;
        contextUsageStatus.clear();
    }
}

void AgentUI::saveContextPolicy() {
    if (!hasOpenProject || currentProjectRoot.empty()) return;
    try {
        fs::path file = contextPolicyFile();
        if (!fs::exists(file.parent_path())) fs::create_directories(file.parent_path());
        nlohmann::json j;
        j["context_source"] = contextSourceLabel(selectedContextSource);
        j["approved_library_paths"] = approvedLibraryPaths;
        j["approved_domains_csv"] = approvedDomainsCsv;
        std::ofstream out(file);
        out << j.dump(2);
    } catch (...) {
    }
}

bool AgentUI::createGovernedProject(const fs::path& basePathIn, const std::string& rawName, int type, std::string& outPath, std::string& err) {
    try {
        std::string name = trimPathInput(rawName);
        if (name.empty()) {
            err = "Nome do projeto não pode ser vazio.";
            return false;
        }

        fs::path basePath = normalizeRootPath(basePathIn.string());
        if (!fs::exists(basePath) || !fs::is_directory(basePath)) {
            err = "Pasta base inválida.";
            return false;
        }

        fs::path root = basePath / name;
        if (fs::exists(root)) {
            err = "A pasta do projeto já existe.";
            return false;
        }

        fs::create_directories(root);
        fs::create_directories(root / "DDD");
        fs::create_directories(root / "DAI");
        fs::create_directories(root / "ADR");
        fs::create_directories(root / "ai-governance");
        fs::create_directories(root / "GSDD");
        fs::create_directories(root / "scripts");
        fs::create_directories(root / ".agent");
        fs::create_directories(root / ".github" / "workflows");
        fs::create_directories(root / "ai-governance" / "policies");
        fs::create_directories(root / "ai-governance" / "checklists");

        if (type == 0) {
            fs::create_directories(root / "src");
            fs::create_directories(root / "include");
            fs::create_directories(root / "tests");
        } else if (type == 1) {
            fs::create_directories(root / "src");
            fs::create_directories(root / "tests");
            fs::create_directories(root / "notebooks");
        } else if (type == 2) {
            fs::create_directories(root / "notes");
            fs::create_directories(root / "references");
            fs::create_directories(root / "drafts");
        } else {
            fs::create_directories(root / "chapters");
            fs::create_directories(root / "notes");
            fs::create_directories(root / "references");
        }

        const std::string projectType =
            (type == 0 ? "cpp" : type == 1 ? "python" : type == 2 ? "research" : "writing");

        auto write = [&](const fs::path& p, const std::string& content) {
            std::ofstream o(p);
            o << content;
        };

        write(root / "README.md",
              "# " + name + "\n\n"
              "Projeto inicializado com governança padrão.\n\n"
              "## Tipo\n- " + projectType + "\n\n"
              "## Estrutura de governança\n- `DDD/`\n- `DAI/`\n- `ADR/`\n- `ai-governance/`\n- `GSDD/`\n");
        write(root / "LICENSE", "Escolha e declare aqui a licença do projeto.\n");
        write(root / "CONTRIBUTING.md",
              "# Contributing\n\n"
              "## Fluxo recomendado\n"
              "1. Abra uma issue descrevendo objetivo e critérios.\n"
              "2. Crie branch curta e orientada a escopo.\n"
              "3. Atualize DDD/DAI/ADR quando houver decisão relevante.\n"
              "4. Execute validações locais antes do PR.\n\n"
              "## Critérios de PR\n"
              "- Mudança pequena e testável\n"
              "- Evidências de validação anexadas\n"
              "- Impacto e riscos descritos\n");
        write(root / "CODEOWNERS", "* @owner\n");
        write(root / ".github/workflows/ci.yml",
              "name: CI\n"
              "on: [push, pull_request]\n"
              "jobs:\n"
              "  build:\n"
              "    runs-on: ubuntu-latest\n"
              "    steps:\n"
              "      - uses: actions/checkout@v4\n"
              "      - name: Governance check\n"
              "        run: ./scripts/validate_governance.sh\n");
        write(root / "DDD/README.md", "# DDD\n\nContexto, linguagem ubíqua, agregados e fronteiras.\n");
        write(root / "DAI/README.md", "# DAI\n\nInvariantes, critérios decisórios e hipóteses de validação.\n");
        write(root / "ADR/README.md", "# ADR\n\nRegistre decisões arquiteturais em arquivos `ADR-XXXX.md`.\n");
        write(root / "ADR/ADR-0001-template.md",
              "# ADR-0001 - Título da decisão\n\n"
              "## Contexto\nDescreva o problema e o cenário.\n\n"
              "## Decisão\nDescreva a decisão adotada.\n\n"
              "## Consequências\nImpactos positivos, trade-offs e riscos.\n");
        write(root / "ai-governance/README.md", "# AI Governance\n\nPolíticas de uso de IA, rastreabilidade e validação.\n");
        write(root / "ai-governance/policies/model-usage.md",
              "# Model Usage Policy\n\n"
              "- Defina modelos permitidos por tipo de tarefa.\n"
              "- Defina níveis de acesso aceitos por ambiente.\n"
              "- Exija evidência objetiva para afirmações de execução/verificação.\n");
        write(root / "ai-governance/policies/evidence.md",
              "# Evidence Policy\n\n"
              "- Toda afirmação de execução deve ter evidência.\n"
              "- Evidência mínima: comando + saída curta ou caminho de arquivo.\n");
        write(root / "ai-governance/policies/context-sources.md",
              "# Context Sources Policy\n\n"
              "- Defina bibliotecas locais aprovadas por projeto.\n"
              "- Defina domínios web aprovados para consulta.\n"
              "- Separe sempre evidência local, evidência externa e inferência do agente.\n");
        write(root / "ai-governance/checklists/review.md",
              "# Review Checklist\n\n"
              "- [ ] Escopo claro\n"
              "- [ ] Riscos mapeados\n"
              "- [ ] Evidências anexadas\n"
              "- [ ] ADR atualizado quando necessário\n");
        write(root / "GSDD/README.md", "# GSDD\n\nEspecificação governada por estágios e entregáveis.\n");
        write(root / "scripts/validate_governance.sh",
              "#!/usr/bin/env sh\n"
              "set -eu\n"
              "for d in DDD DAI ADR ai-governance GSDD .github/workflows; do\n"
              "  [ -d \"$d\" ] || { echo \"Missing $d\"; exit 1; }\n"
              "done\n"
              "for f in README.md CONTRIBUTING.md CODEOWNERS ADR/ADR-0001-template.md ai-governance/policies/model-usage.md ai-governance/policies/evidence.md; do\n"
              "  [ -f \"$f\" ] || { echo \"Missing $f\"; exit 1; }\n"
              "done\n"
              "echo \"Governance structure OK\"\n");
        write(root / ".agent/context_policy.json",
              "{\n"
              "  \"context_source\": \"workspace\",\n"
              "  \"approved_library_paths\": [],\n"
              "  \"approved_domains_csv\": \"embrapa.br\"\n"
              "}\n");

        if (type == 0) {
            write(root / "CMakeLists.txt",
                  "cmake_minimum_required(VERSION 3.20)\n"
                  "project(" + name + " LANGUAGES CXX)\n"
                  "set(CMAKE_CXX_STANDARD 20)\n"
                  "add_executable(" + name + " src/main.cpp)\n");
            write(root / "src/main.cpp", "#include <iostream>\n\nint main(){ std::cout << \"Hello " + name + "\\n\"; }\n");
        } else if (type == 1) {
            write(root / "pyproject.toml",
                  "[project]\nname = \"" + name + "\"\nversion = \"0.1.0\"\nrequires-python = \">=3.10\"\n");
            write(root / "src/main.py", "def main():\n    print(\"Hello " + name + "\")\n\nif __name__ == '__main__':\n    main()\n");
        } else if (type == 2) {
            write(root / "notes/00-problem-statement.md", "# Problema\n\nDescreva problema, hipótese e critérios.\n");
        } else {
            write(root / "chapters/00-outline.md", "# Estrutura\n\nDefina capítulos e fluxo narrativo.\n");
        }

        outPath = root.string();
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    } catch (...) {
        err = "Erro desconhecido ao criar projeto.";
        return false;
    }
}

void AgentUI::runPythonAgent(const std::string& goal, const std::string& mode) {
    if (!orchestrator) return;
    const int promptEstimate = std::max(1, static_cast<int>(goal.size() / 4));

    agent::core::Orchestrator::MissionCallbacks cb;
    cb.onThought = [this](const std::string& thought) {
        std::lock_guard<std::mutex> lock(msgMutex);
        thoughtStream = iconLabel(emojiIconsEnabled, "🧠 PENSANDO:\n", "[THINKING]\n") + thought;
    };
    cb.onAction = [this](const std::string& action) {
        std::lock_guard<std::mutex> lock(msgMutex);
        thoughtStream += "\n" + iconLabel(emojiIconsEnabled, "🚀 AÇÃO: ", "[ACTION] ") + action;
    };
    cb.onObservation = [this](const std::string& obs) {
        std::lock_guard<std::mutex> lock(msgMutex);
        thoughtStream += "\n" + iconLabel(emojiIconsEnabled, "👁️ OBSERVAÇÃO: ", "[OBSERVATION] ") +
                         (obs.length() > 200 ? obs.substr(0, 200) + "..." : obs);
    };
    cb.onMessageChunk = [this](const std::string& chunk) {
        std::lock_guard<std::mutex> lock(msgMutex);
        if (!history.empty() && history.back().role == "assistant") {
            if (history.back().text == "...") history.back().text = "";
            else history.back().text += "\n\n---\n\n"; // Separador de passos
            history.back().text += chunk;
            scrollToBottom = true;
        }
        totalCompletionTokens += std::max(1, static_cast<int>(chunk.size() / 4));
        saveSession();
    };
    cb.onComplete = [this](bool success) {
        std::lock_guard<std::mutex> lock(msgMutex);
        thoughtStream += success
            ? ("\n" + iconLabel(emojiIconsEnabled, "🏁 Missão Finalizada Nativamente.", "[DONE] Missão Finalizada Nativamente."))
            : ("\n" + iconLabel(emojiIconsEnabled, "⏹️ Missão interrompida.", "[STOPPED] Missão interrompida."));
        llmBusy = false;
        saveSession();
    };

    {
        std::lock_guard<std::mutex> lock(msgMutex);
        thoughtStream = iconLabel(emojiIconsEnabled, "🚀 MISSION START: Iniciando Orquestrador C++20 Nativo...",
                                  "[MISSION START] Iniciando Orquestrador C++20 Nativo...");
        totalPromptTokens += promptEstimate;
    }

    orchestrator->runMission(goal, mode, 10, cb);
}

void AgentUI::triggerRagSync() {
    if (ragIndexingBusy) return;
    ragIndexingBusy = true;
    ragIndexingProgress = 0.0f;
    ragIndexingStatusMsg = "Iniciando varredura...";

    std::thread([this]() {
        try {
            std::vector<std::string> roots = agent::core::getNativeToolApprovedRoots();
            std::vector<std::filesystem::path> allFiles;
            
            std::vector<std::string> missingRoots;
            
            for (const auto& rootStr : roots) {
                try {
                    std::filesystem::path root(rootStr);
                    if (!std::filesystem::exists(root)) {
                        missingRoots.push_back(rootStr);
                        continue;
                    }
                    // Adicionando opcoes para pular erros de permissao e nao abortar a varredura
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied)) {
                        if (entry.is_regular_file()) {
                            std::string ext = entry.path().extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
                            if (ext == ".pdf" || ext == ".txt" || ext == ".md" || ext == ".rst") {
                                allFiles.push_back(entry.path());
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    // Logar erro de varredura mas continuar para as outras roots
                    missingRoots.push_back(rootStr + " (Erro: " + e.what() + ")");
                }
            }

            if (!missingRoots.empty()) {
                ragIndexingStatusMsg = "Aviso: Algumas pastas falharam.";
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }

            if (allFiles.empty()) {
                ragIndexingStatusMsg = "Nenhum arquivo encontrado nas pastas validas.";
                std::this_thread::sleep_for(std::chrono::seconds(3));
                ragIndexingBusy = false;
                return;
            }

            int successCount = 0;
            int errorCount = 0;
            for (size_t i = 0; i < allFiles.size(); ++i) {
                try {
                    ragIndexingStatusMsg = "Ingerindo [" + std::to_string(i+1) + "/" + std::to_string(allFiles.size()) + "]: " + allFiles[i].filename().string();
                    std::string res = agent::core::ingest_file_direct(allFiles[i].string());
                    if (res.rfind("Erro", 0) == 0) {
                        errorCount++;
                    } else {
                        successCount++;
                    }
                } catch (...) {
                    errorCount++;
                }
                ragIndexingProgress = static_cast<float>(i + 1) / static_cast<float>(allFiles.size());
                
                // Atualizar status parcial a cada 5 arquivos para dar feedback visual de progresso
                if (i % 5 == 0) ragStats = agent::core::getNativeToolRagStats();
            }

            ragIndexingStatusMsg = "Concluido! Sucesso: " + std::to_string(successCount) + " | Erros: " + std::to_string(errorCount);
            ragStats = agent::core::getNativeToolRagStats();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        } catch (const std::exception& e) {
            ragIndexingStatusMsg = "Erro no sync: " + std::string(e.what());
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
        ragIndexingBusy = false;
    }).detach();
}

} // namespace agent::ui
