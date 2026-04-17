#include "AgentUI_Internal.hpp"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace agent::ui {

namespace {
static std::string lowerPathString(const fs::path& path) {
    std::string value = path.string();
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static TextEditor::LanguageDefinition languageForPath(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cc" || ext == ".c") {
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
} // namespace

void AgentUI::generateProjectMap() {
    if (!hasOpenProject || currentProjectRoot.empty()) return;
    projectMap = "ESTRUTURA DO PROJETO:\n";
    fs::path root(currentProjectRoot);
    try {
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            const auto& path = entry.path();
            auto rel = fs::relative(path, root);
            int depth = std::distance(rel.begin(), rel.end());
            
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

    projectGovernance.clear();
    try {
        fs::path govFile = fs::path(currentProjectRoot) / ".agent" / "PROCEDURES.md";
        if (!fs::exists(govFile)) govFile = fs::path(currentProjectRoot) / "AGENTS.md";
        
        if (fs::exists(govFile)) {
            std::ifstream in(govFile);
            if (in) {
                std::stringstream buffer;
                buffer << in.rdbuf();
                projectGovernance = buffer.str();
            }
        }
    } catch (...) {}

    thoughtStream = "Project Map reconstruído. Governança Local: " + 
                    (projectGovernance.empty() ? std::string("Não encontrada") : (std::to_string(projectGovernance.length()) + " bytes"));
}

void AgentUI::drawFileExplorer() {
    ImGui::BeginChild("ExplorerArea", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "DIALOGOS");
    ImGui::Separator();

    if (hasOpenProject && !currentProjectRoot.empty()) {
        ImGui::TextWrapped("%s", fs::path(currentProjectRoot).filename().string().c_str());
        ImGui::TextDisabled("%s", currentProjectRoot.c_str());
        if (!lastResolvedProjectRoot.empty() && lastResolvedProjectRoot != currentProjectRoot) {
            ImGui::TextDisabled("Raiz resolvida: %s", lastResolvedProjectRoot.c_str());
        }
        ImGui::Separator();
    }

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
                    loadSessionFromFile(path);
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
        if (ImGui::SmallButton("Novo Arquivo")) {
            newEntryModeDirectory = false;
            newEntryFormVisible = true;
            newEntryPathBuf[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Nova Pasta")) {
            newEntryModeDirectory = true;
            newEntryFormVisible = true;
            newEntryPathBuf[0] = '\0';
        }
        if (newEntryFormVisible) {
            ImGui::InputText(newEntryModeDirectory ? "Pasta relativa" : "Arquivo relativo", newEntryPathBuf, sizeof(newEntryPathBuf));
            ImGui::SameLine();
            if (ImGui::SmallButton("Criar")) {
                if (createWorkspaceEntry(newEntryPathBuf, newEntryModeDirectory)) {
                    thoughtStream = std::string(newEntryModeDirectory ? "Pasta criada: " : "Arquivo criado: ") + newEntryPathBuf;
                    newEntryFormVisible = false;
                } else {
                    thoughtStream = "ERRO: nao foi possivel criar o caminho solicitado.";
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancelar")) {
                newEntryFormVisible = false;
            }
            ImGui::Separator();
        }
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
                    noteFileTouched(selectedFile);
                }
            }
        }
    } catch (...) {}
}

void AgentUI::loadFileIntoEditor(const std::string& path) {
    if (path.empty()) return;
    try {
        std::ifstream ifs(path);
        if (ifs.is_open()) {
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
            noteFileTouched(editorFilePath);
        }
    } catch (...) {}
}

bool AgentUI::saveEditorFile() {
    if (editorFilePath.empty()) return false;
    try {
        std::ofstream ofs(editorFilePath);
        if (!ofs.is_open()) return false;
        const std::string content = editorUsesPlainText ? editorPlainTextBuffer : codeEditor.GetText();
        ofs << content;
        editorSavedText = content;
        editorDirty = false;
        noteFileTouched(editorFilePath);
        return true;
    } catch (...) { return false; }
}

void AgentUI::noteFileTouched(const std::string& path) {
    const std::string trimmed = trimLoose(path);
    if (trimmed.empty()) return;
    recentFiles.erase(std::remove(recentFiles.begin(), recentFiles.end(), trimmed), recentFiles.end());
    recentFiles.push_front(trimmed);
    while (recentFiles.size() > 8) recentFiles.pop_back();
}

bool AgentUI::createWorkspaceEntry(const std::string& relativePath, bool directory) {
    if (!hasOpenProject || currentProjectRoot.empty()) return false;
    const std::string trimmed = trimLoose(relativePath);
    if (trimmed.empty()) return false;
    try {
        fs::path target = fs::path(currentProjectRoot) / fs::path(trimmed);
        fs::path parent = target.has_parent_path() ? target.parent_path() : fs::path(currentProjectRoot);
        fs::path normalizedParent = fs::weakly_canonical(parent);
        fs::path normalizedRoot = fs::weakly_canonical(currentProjectRoot);
        if (normalizedParent.string().rfind(normalizedRoot.string(), 0) != 0) return false;
        if (directory) {
            fs::create_directories(target);
            selectedFile.clear();
        } else {
            fs::create_directories(parent);
            std::ofstream ofs(target);
            if (!ofs.is_open()) return false;
            selectedFile = target.string();
            loadFileIntoEditor(selectedFile);
            noteFileTouched(selectedFile);
        }
        generateProjectMap();
        return true;
    } catch (...) {
        return false;
    }
}

bool AgentUI::applyTextToActiveFile(const std::string& text, bool saveAfter) {
    if (editorFilePath.empty()) return false;
    if (editorUsesPlainText) editorPlainTextBuffer = text;
    else codeEditor.SetText(text);
    editorDirty = true;
    noteFileTouched(editorFilePath);
    return !saveAfter || saveEditorFile();
}

bool AgentUI::ensureEditorTarget(const std::string& targetPath) {
    const std::string trimmed = trimLoose(targetPath);
    if (trimmed.empty()) return false;

    fs::path target(trimmed);
    try {
        if (!target.is_absolute()) {
            if (!hasOpenProject || currentProjectRoot.empty()) return false;
            target = fs::path(currentProjectRoot) / target;
        }

        fs::path parent = target.has_parent_path() ? target.parent_path() : fs::path(currentProjectRoot);
        fs::create_directories(parent);
        if (!fs::exists(target)) {
            std::ofstream ofs(target);
            if (!ofs.is_open()) return false;
        }

        selectedFile = target.string();
        loadFileIntoEditor(selectedFile);
        noteFileTouched(selectedFile);
        return !editorFilePath.empty();
    } catch (...) {
        return false;
    }
}

void AgentUI::drawFileEditor() {
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "EDITOR");
    ImGui::Separator();

    if (editorFilePath.empty()) {
        ImGui::TextDisabled("Selecione um arquivo no explorer.");
        return;
    }

    editorDirty = editorUsesPlainText ? (editorPlainTextBuffer != editorSavedText) : (codeEditor.GetText() != editorSavedText);
    std::string fileName = fs::path(editorFilePath).filename().string();
    if (editorDirty) fileName += " *";
    ImGui::TextWrapped("%s", fileName.c_str());
    ImGui::TextDisabled("%s", editorFilePath.c_str());

    if (ImGui::SmallButton("Salvar")) saveEditorFile();
    ImGui::SameLine();
    if (ImGui::SmallButton("Contexto")) selectedFile = editorFilePath;
    ImGui::SameLine();
    if (ImGui::SmallButton("Recarregar")) loadFileIntoEditor(editorFilePath);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", editorDirty ? "Nao salvo" : "Salvo");

    ImGui::BeginChild("InlineFileEditor", ImVec2(0, 260), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (editorUsesPlainText) {
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
        ImGui::InputTextMultiline("##PlainTextEditor", &editorPlainTextBuffer, ImVec2(-1, -1), flags);
    } else {
        codeEditor.Render("ProjectFileEditor");
    }
    ImGui::EndChild();
}

} // namespace agent::ui
