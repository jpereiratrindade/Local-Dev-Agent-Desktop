#include "AgentUI_Internal.hpp"
#include "imgui.h"
#include "AgentSpec.hpp"
#include "Orchestrator.hpp"
#include "PromptComposer.hpp"
#include "json.hpp"
#include <regex>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>

namespace agent::ui {

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

const char* messagePartTypeName(MessagePartType type) {
    switch (type) {
        case MessagePartType::Reasoning: return "reasoning";
        case MessagePartType::ToolCall: return "tool_call";
        case MessagePartType::ToolResult: return "tool_result";
        case MessagePartType::Text:
        default: return "text";
    }
}

MessagePartType messagePartTypeFromString(const std::string& value) {
    if (value == "reasoning") return MessagePartType::Reasoning;
    if (value == "tool_call") return MessagePartType::ToolCall;
    if (value == "tool_result") return MessagePartType::ToolResult;
    return MessagePartType::Text;
}

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
    return sections;
}

namespace {
bool containsAnyLower(const std::string& haystack, const std::vector<std::string>& needles) {
    for (const auto& needle : needles) {
        if (haystack.find(needle) != std::string::npos) return true;
    }
    return false;
}

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::vector<std::string> findFilenameCandidatesInProject(const std::string& projectRoot, const std::string& lowerGoal) {
    std::vector<std::string> matches;
    if (projectRoot.empty() || lowerGoal.empty()) return matches;

    try {
        for (const auto& entry : fs::recursive_directory_iterator(projectRoot)) {
            if (!entry.is_regular_file()) continue;
            const std::string filename = toLowerCopy(entry.path().filename().string());
            if (filename.empty()) continue;
            if (lowerGoal.find(filename) != std::string::npos) {
                matches.push_back(entry.path().string());
            }
        }
    } catch (...) {}

    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    return matches;
}

std::vector<std::string> extractCodeBlocks(const std::string& text) {
    std::vector<std::string> blocks;
    std::regex codeBlockRegex("```(?:[A-Za-z0-9_+#.-]+)?\\s*([\\s\\S]*?)\\s*```");
    for (std::sregex_iterator it(text.begin(), text.end(), codeBlockRegex), end; it != end; ++it) {
        blocks.push_back(trimLoose((*it)[1].str()));
    }
    return blocks;
}

std::vector<std::string> extractJsonBlocks(const std::string& text) {
    std::vector<std::string> blocks;
    std::regex jsonBlockRegex("```json\\s*([\\s\\S]*?)\\s*```");
    for (std::sregex_iterator it(text.begin(), text.end(), jsonBlockRegex), end; it != end; ++it) {
        blocks.push_back(trimLoose((*it)[1].str()));
    }
    return blocks;
}

bool looksLikeMixedExplanatoryResponse(const std::string& text) {
    std::string lower = toLowerCopy(text);
    return containsAnyLower(lower, {
        "explicação", "explicacao", "código atualizado", "codigo atualizado",
        "para testar", "compile", "agora você terá", "agora voce tera",
        "vou implementar", "se precisar", "###", "1.", "2.", "3."
    });
}

std::string extractToolNameFromAction(const std::string& action) {
    std::smatch match;
    std::regex actionRegex(R"(Executando:\s*([A-Za-z0-9_./:-]+))");
    if (std::regex_search(action, match, actionRegex) && match.size() > 1) {
        return match[1].str();
    }
    return "";
}

std::string handleSlashCommand(const std::string& input) {
    if (input.empty() || input[0] != '/') return input;

    size_t spacePos = input.find(' ');
    std::string cmd = toLowerCopy(input.substr(1, spacePos - 1));
    std::string arg = (spacePos != std::string::npos) ? input.substr(spacePos + 1) : "";

    if (cmd == "fix") {
        return "Analise o arquivo ativo e corrija erros de sintaxe, bugs logicos ou falhas de build relatadas. Foque em estabilidade.\n\n" + arg;
    } else if (cmd == "explain") {
        return "Explique detalhadamente o funcionamento do código no arquivo ativo (ou a seleçao atual). Foque em arquitetura e lógica.\n\n" + arg;
    } else if (cmd == "test") {
        return "Gere testes unitários ou scripts de validaçao para o código no arquivo ativo. Siga os padroes de teste do projeto.\n\n" + arg;
    } else if (cmd == "refactor") {
        return "Refatore o código no arquivo ativo para melhorar legibilidade, performance ou manutençao, sem alterar o comportamento externo.\n\n" + arg;
    } else if (cmd == "doc") {
        return "Adicione documentaçao (Doxygen/Docstrings) clara e concisa para o código no arquivo ativo ou seleçao.\n\n" + arg;
    }

    return input;
}
} // namespace

StructuredChatMessage AgentUI::buildStructuredMessage(const ChatMessage& message) const {
    StructuredChatMessage structured;
    structured.role = message.role;
    if (message.role != "assistant") {
        structured.parts.push_back({MessagePartType::Text, message.text, "", "", false});
        return structured;
    }

    AgentMessageSections sections = splitAgentMessage(message.text);
    const std::string trimmedAnswer = trimLoose(sections.answer);
    if (!trimmedAnswer.empty()) {
        structured.parts.push_back({MessagePartType::Text, trimmedAnswer, "", "", false});
    }
    for (const auto& step : sections.actionSteps) {
        structured.parts.push_back({MessagePartType::ToolCall, step.content, step.title, "", false});
    }
    const std::string trimmedLogs = trimLoose(sections.logs);
    if (!trimmedLogs.empty()) {
        structured.parts.push_back({MessagePartType::ToolResult, trimmedLogs, "logs", "", false});
    }
    if (structured.parts.empty()) {
        structured.parts.push_back({MessagePartType::Text, message.text, "", "", false});
    }
    return structured;
}

std::string AgentUI::flattenStructuredMessageText(const StructuredChatMessage& message) const {
    std::string result;
    for (const auto& part : message.parts) {
        if (!result.empty()) result += "\n";
        result += part.text;
    }
    return result;
}

void AgentUI::ensureStructuredHistoryLocked() {
    if (structuredHistory.size() == history.size()) return;
    structuredHistory.clear();
    for (const auto& msg : history) {
        structuredHistory.push_back(buildStructuredMessage(msg));
    }
}

void AgentUI::appendHistoryMessageLocked(const ChatMessage& message) {
    history.push_back(message);
    structuredHistory.push_back(buildStructuredMessage(message));
}

StructuredChatMessage& AgentUI::ensureAssistantStructuredMessageLocked() {
    if (history.empty() || history.back().role != "assistant") {
        appendHistoryMessageLocked({"assistant", ""});
    } else {
        ensureStructuredHistoryLocked();
    }
    return structuredHistory.back();
}

void AgentUI::renderMarkdown(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    std::string codeBlockContent;
    std::string codeBlockLang;
    bool inCodeBlock = false;
    int codeBlockId = 0;

    while (std::getline(stream, line)) {
        // Detectar abertura/fechamento de code block
        if (line.substr(0, 3) == "```") {
            if (!inCodeBlock) {
                inCodeBlock = true;
                codeBlockLang = line.size() > 3 ? line.substr(3) : "";
                // remover possível \r
                if (!codeBlockLang.empty() && codeBlockLang.back() == '\r') codeBlockLang.pop_back();
                codeBlockContent.clear();
            } else {
                // Fechar e renderizar code block
                inCodeBlock = false;
                std::string blockLabel = "##codeblock" + std::to_string(codeBlockId++);
                if (!codeBlockLang.empty()) {
                    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "[%s]", codeBlockLang.c_str());
                }
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.12f, 1.0f));
                ImGui::InputTextMultiline(blockLabel.c_str(), &codeBlockContent,
                    ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() *
                           std::min(20, static_cast<int>(std::count(codeBlockContent.begin(), codeBlockContent.end(), '\n') + 2))),
                    ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor();
                codeBlockContent.clear();
            }
            continue;
        }

        if (inCodeBlock) {
            codeBlockContent += line + "\n";
            continue;
        }

        // Remover \r de fim de linha
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Headings
        if (line.substr(0, 4) == "### ") {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%s", line.substr(4).c_str());
            ImGui::Separator();
        } else if (line.substr(0, 3) == "## ") {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "%s", line.substr(3).c_str());
        } else if (line.substr(0, 2) == "# ") {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", line.substr(2).c_str());
            ImGui::Separator();
        }
        // Separadores horizontais
        else if (line == "---" || line == "***" || line == "___") {
            ImGui::Separator();
        }
        // Listas não-ordenadas
        else if (line.size() >= 2 && (line.substr(0, 2) == "- " || line.substr(0, 2) == "* ")) {
            ImGui::Bullet();
            ImGui::SameLine();
            // Detectar negrito simples **text**
            std::string content = line.substr(2);
            if (content.find("**") != std::string::npos) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.7f, 1.0f), "%s", content.c_str());
            } else {
                ImGui::TextWrapped("%s", content.c_str());
            }
        }
        // Listas numeradas
        else if (line.size() >= 3 && std::isdigit(static_cast<unsigned char>(line[0])) && line[1] == '.' && line[2] == ' ') {
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%c.", line[0]);
            ImGui::SameLine();
            ImGui::TextWrapped("%s", line.substr(3).c_str());
        }
        // Citações
        else if (line.substr(0, 2) == "> ") {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "│ %s", line.substr(2).c_str());
        }
        // Linha com negrito **text**
        else if (line.find("**") != std::string::npos) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.7f, 1.0f), "%s", line.c_str());
        }
        // Linha com inline code `text`
        else if (line.find("`") != std::string::npos) {
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1.0f), "%s", line.c_str());
        }
        // Linha vazia
        else if (line.empty()) {
            ImGui::Spacing();
        }
        // Texto normal
        else {
            ImGui::TextWrapped("%s", line.c_str());
        }
    }
}

std::string AgentUI::buildSimpleDiffPreview(const std::string& oldText, const std::string& newText) const {
    // P2.3: Diff com contexto — LCS simplificado com 3 linhas de contexto (estilo unified diff)
    auto splitLines = [](const std::string& text) -> std::vector<std::string> {
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
        return lines;
    };

    const auto oldLines = splitLines(oldText);
    const auto newLines = splitLines(newText);
    const size_t M = std::min(oldLines.size(), size_t{300});
    const size_t N = std::min(newLines.size(), size_t{300});

    // Calcular LCS via DP (limitado a 300x300 = 90k células)
    std::vector<std::vector<int>> dp(M + 1, std::vector<int>(N + 1, 0));
    for (size_t i = 1; i <= M; ++i)
        for (size_t j = 1; j <= N; ++j)
            dp[i][j] = (oldLines[i-1] == newLines[j-1]) ? dp[i-1][j-1] + 1
                                                         : std::max(dp[i-1][j], dp[i][j-1]);

    // Reconstruir diff como vetor de operações: 0=context, -1=removed, +1=added
    struct DiffLine { int op; std::string text; };
    std::vector<DiffLine> diff;
    size_t i = M, j = N;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && oldLines[i-1] == newLines[j-1]) {
            diff.push_back({0, oldLines[i-1]});
            --i; --j;
        } else if (j > 0 && (i == 0 || dp[i][j-1] >= dp[i-1][j])) {
            diff.push_back({+1, newLines[j-1]});
            --j;
        } else {
            diff.push_back({-1, oldLines[i-1]});
            --i;
        }
    }
    std::reverse(diff.begin(), diff.end());

    // Emitir com 3 linhas de contexto
    constexpr int kCtx = 3;
    std::vector<bool> show(diff.size(), false);
    for (size_t k = 0; k < diff.size(); ++k) {
        if (diff[k].op != 0) {
            for (int c = -kCtx; c <= kCtx; ++c) {
                int idx = static_cast<int>(k) + c;
                if (idx >= 0 && idx < static_cast<int>(diff.size())) show[static_cast<size_t>(idx)] = true;
            }
        }
    }

    std::stringstream out;
    bool inGap = false;
    int changed = 0;
    for (size_t k = 0; k < diff.size(); ++k) {
        if (!show[k]) {
            if (!inGap) { out << "@@ ... @@\n"; inGap = true; }
            continue;
        }
        inGap = false;
        const auto& dl = diff[k];
        if      (dl.op == -1) { out << "- " << dl.text << "\n"; changed++; }
        else if (dl.op == +1) { out << "+ " << dl.text << "\n"; changed++; }
        else                  { out << "  " << dl.text << "\n"; }
        if (changed > 200) { out << "...diff truncado (> 200 linhas modificadas)...\n"; break; }
    }
    if (changed == 0) out << "(sem diferenças detectadas)\n";
    return out.str();
}

std::string AgentUI::loadWorkspaceFileText(const std::string& path) const {
    const std::string trimmed = trimLoose(path);
    if (trimmed.empty()) return "";

    try {
        fs::path target(trimmed);
        if (!target.is_absolute()) {
            if (!hasOpenProject || currentProjectRoot.empty()) return "";
            target = fs::path(currentProjectRoot) / target;
        }

        std::ifstream in(target);
        if (!in.is_open()) return "";
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    } catch (...) {
        return "";
    }
}

std::string AgentUI::inferActiveFileForGoal(const std::string& goal) const {
    const std::string lowerGoal = toLowerCopy(goal);

    auto candidateScore = [&](const std::string& candidatePath) -> int {
        if (candidatePath.empty()) return -1;
        fs::path path(candidatePath);
        const std::string filename = toLowerCopy(path.filename().string());
        const std::string rel = hasOpenProject && !currentProjectRoot.empty()
            ? toLowerCopy(fs::relative(path, currentProjectRoot).string())
            : filename;

        int score = 0;
        if (!filename.empty() && lowerGoal.find(filename) != std::string::npos) score += 100;
        if (!rel.empty() && lowerGoal.find(rel) != std::string::npos) score += 120;
        if (!selectedFile.empty() && candidatePath == selectedFile) score += 25;
        if (!editorFilePath.empty() && candidatePath == editorFilePath) score += 35;
        if (!lastChangeTargetPath.empty() && candidatePath == lastChangeTargetPath) score += 30;

        const auto it = std::find(recentFiles.begin(), recentFiles.end(), candidatePath);
        if (it != recentFiles.end()) {
            score += std::max(5, 20 - static_cast<int>(std::distance(recentFiles.begin(), it)) * 3);
        }

        if (lowerGoal.find("este arquivo") != std::string::npos || lowerGoal.find("arquivo ativo") != std::string::npos) {
            if (!editorFilePath.empty() && candidatePath == editorFilePath) score += 60;
            else if (!selectedFile.empty() && candidatePath == selectedFile) score += 40;
        }

        if (lowerGoal.find("esse texto") != std::string::npos || lowerGoal.find("continua") != std::string::npos ||
            lowerGoal.find("continuar") != std::string::npos || lowerGoal.find("corrigir") != std::string::npos ||
            lowerGoal.find("revisar") != std::string::npos || lowerGoal.find("ajustar") != std::string::npos) {
            if (!editorFilePath.empty() && candidatePath == editorFilePath) score += 20;
        }
        return score;
    };

    std::string bestPath;
    int bestScore = -1;
    std::vector<std::string> candidates;
    if (!editorFilePath.empty()) candidates.push_back(editorFilePath);
    if (!selectedFile.empty() && selectedFile != editorFilePath) candidates.push_back(selectedFile);
    if (!lastChangeTargetPath.empty() &&
        std::find(candidates.begin(), candidates.end(), lastChangeTargetPath) == candidates.end()) {
        candidates.push_back(lastChangeTargetPath);
    }
    for (const auto& recent : recentFiles) {
        if (std::find(candidates.begin(), candidates.end(), recent) == candidates.end()) candidates.push_back(recent);
    }
    if (hasOpenProject && !currentProjectRoot.empty()) {
        for (const auto& match : findFilenameCandidatesInProject(currentProjectRoot, lowerGoal)) {
            if (std::find(candidates.begin(), candidates.end(), match) == candidates.end()) candidates.push_back(match);
        }
    }

    for (const auto& candidate : candidates) {
        int score = candidateScore(candidate);
        if (score > bestScore) {
            bestScore = score;
            bestPath = candidate;
        }
    }

    if (bestScore <= 0) {
        if (!editorFilePath.empty()) return editorFilePath;
        if (!selectedFile.empty()) return selectedFile;
    }
    return bestPath;
}

std::string AgentUI::inferActiveFileAmbiguityNote(const std::string& goal) const {
    const std::string lowerGoal = toLowerCopy(goal);
    struct CandidateScore { std::string path; int score; };
    std::vector<CandidateScore> scored;
    std::vector<std::string> candidates;
    if (!editorFilePath.empty()) candidates.push_back(editorFilePath);
    if (!selectedFile.empty() && selectedFile != editorFilePath) candidates.push_back(selectedFile);
    if (!lastChangeTargetPath.empty() &&
        std::find(candidates.begin(), candidates.end(), lastChangeTargetPath) == candidates.end()) {
        candidates.push_back(lastChangeTargetPath);
    }
    for (const auto& recent : recentFiles) {
        if (std::find(candidates.begin(), candidates.end(), recent) == candidates.end()) candidates.push_back(recent);
    }
    if (hasOpenProject && !currentProjectRoot.empty()) {
        for (const auto& match : findFilenameCandidatesInProject(currentProjectRoot, lowerGoal)) {
            if (std::find(candidates.begin(), candidates.end(), match) == candidates.end()) candidates.push_back(match);
        }
    }

    for (const auto& candidatePath : candidates) {
        fs::path path(candidatePath);
        const std::string filename = toLowerCopy(path.filename().string());
        const std::string rel = hasOpenProject && !currentProjectRoot.empty()
            ? toLowerCopy(fs::relative(path, currentProjectRoot).string())
            : filename;
        int score = 0;
        if (!filename.empty() && lowerGoal.find(filename) != std::string::npos) score += 100;
        if (!rel.empty() && lowerGoal.find(rel) != std::string::npos) score += 120;
        if (!selectedFile.empty() && candidatePath == selectedFile) score += 25;
        if (!editorFilePath.empty() && candidatePath == editorFilePath) score += 35;
        if (!lastChangeTargetPath.empty() && candidatePath == lastChangeTargetPath) score += 30;
        const auto it = std::find(recentFiles.begin(), recentFiles.end(), candidatePath);
        if (it != recentFiles.end()) score += std::max(5, 20 - static_cast<int>(std::distance(recentFiles.begin(), it)) * 3);
        if (score > 0) scored.push_back({candidatePath, score});
    }

    if (scored.size() < 2) return "";
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        return a.score > b.score;
    });
    if (scored[0].score - scored[1].score <= 15) {
        return "Ambiguidade de arquivo: " + fs::path(scored[0].path).filename().string() +
               " ou " + fs::path(scored[1].path).filename().string();
    }
    return "";
}

std::string AgentUI::buildActiveContextBlock() const {
    std::stringstream context;
    const bool isLmStudio = ollama && ollama->getProvider() == agent::network::ModelProvider::LMStudio;
    const size_t maxContextChars = isLmStudio ? 4000 : 12000;
    if (hasOpenProject && !currentProjectRoot.empty()) {
        context << "Projeto atual: " << currentProjectRoot << "\n";
    }
    const std::string activeFile = inferActiveFileForGoal(history.empty() ? "" : history.back().text);
    
    // Check for explicit selection in active file
    std::string selectedText;
    if (!editorUsesPlainText && codeEditor.HasSelection() && activeFile == editorFilePath) {
        selectedText = codeEditor.GetSelectedText();
    }

    if (!activeFile.empty()) {
        context << "Arquivo ativo inferido: " << activeFile << "\n";
        const std::string ambiguity = inferActiveFileAmbiguityNote(history.empty() ? "" : history.back().text);
        if (!ambiguity.empty()) context << ambiguity << "\n";
        
        if (!selectedText.empty()) {
            context << "SELEÇAO ATIVA NO EDITOR:\n```text\n" << selectedText << "\n```\n";
            context << "Trate a seleçao acima como o alvo prioritário da solicitaçao.\n";
        }

        std::string fullContent;
        if (activeFile == editorFilePath) {
            fullContent = editorUsesPlainText ? editorPlainTextBuffer : codeEditor.GetText();
        } else {
            try {
                std::ifstream in(activeFile);
                if (in) {
                    std::stringstream buffer;
                    buffer << in.rdbuf();
                    fullContent = buffer.str();
                }
            } catch (...) {}
        }
        if (!fullContent.empty()) {
            if (fullContent.size() > maxContextChars) {
                fullContent = fullContent.substr(0, maxContextChars) + "\n...[conteudo truncado]...";
            }
            context << "Conteudo atual do arquivo ativo:\n```text\n" << fullContent << "\n```\n";
        }
    }
    if (!projectGovernance.empty()) {
        context << "Governanca local ativa.\n";
    }
    return context.str();
}

std::string AgentUI::buildChatSystemPrompt() const {
    const agent::core::AgentSpec spec = currentAgentSpec();
    agent::core::PromptContext ctx;
    ctx.agentSpec = spec;
    ctx.provider = (ollama && ollama->getProvider() == agent::network::ModelProvider::LMStudio)
        ? "LM Studio / OpenAI-compatible"
        : "Ollama";
    ctx.mode = "CHAT";
    ctx.profile = profileLabel(selectedProfile);
    ctx.reasoning = reasoning;
    ctx.workspaceRoot = hasOpenProject ? currentProjectRoot : "";
    ctx.activeFile = inferActiveFileForGoal(history.empty() ? "" : history.back().text);
    ctx.projectMap = projectMap;
    ctx.governance = projectGovernance;
    ctx.compactForProvider = ollama && ollama->getProvider() == agent::network::ModelProvider::LMStudio;
    return agent::core::buildSharedAgentPrompt(ctx);
}

agent::core::AgentSpec AgentUI::currentAgentSpec() const {
    const std::string provider = (ollama && ollama->getProvider() == agent::network::ModelProvider::LMStudio)
        ? "LM Studio / OpenAI-compatible"
        : "Ollama";
    return agent::core::buildAgentSpec(profileLabel(selectedProfile), provider, reasoning, access);
}

std::vector<std::string> AgentUI::currentAgentToolNames() const {
    return agent::core::ToolRegistry::instance().listToolNamesForProfile(currentAgentSpec().toolProfile);
}

bool AgentUI::buildChangeProposalFromAssistantText(const std::string& text, ChangeProposal& proposal) const {
    proposal = {};
    const std::string inferredTarget = inferActiveFileForGoal(history.empty() ? "" : history.back().text);
    proposal.kind = inferredTarget.empty() ? "create_file" : "replace_file";
    proposal.targetPath = !inferredTarget.empty() ? inferredTarget : (!editorFilePath.empty() ? editorFilePath : selectedFile);
    proposal.confidence = "high";

    // 1. Preferred: JSON Envelope
    const auto jsonBlocks = extractJsonBlocks(text);
    if (!jsonBlocks.empty()) {
        for (const auto& block : jsonBlocks) {
            try {
                auto j = nlohmann::json::parse(block);
                if (!j.is_object() || !j.contains("content")) continue;
                
                proposal.kind = j.value("kind", proposal.kind);
                proposal.targetPath = j.value("target", proposal.targetPath);
                proposal.summary = j.value("summary", "Proposta estruturada recebida.");
                proposal.content = j.value("content", "");
                
                // If it's a JSON proposal, we assume high confidence unless it's empty
                proposal.directlyApplicable = !proposal.content.empty();
                if (proposal.directlyApplicable) {
                    proposal.confidence = "high";
                    return true;
                }
            } catch (...) {}
        }
    }

    // 2. Secondary: Raw Code Blocks (only if simple)
    AgentMessageSections sections = splitAgentMessage(text);
    std::string cleaned = trimLoose(sections.answer);
    cleaned = std::regex_replace(cleaned, std::regex("<thought>[\\s\\S]*?</thought>"), "");
    cleaned = std::regex_replace(cleaned, std::regex("TASK COMPLETE"), "");
    cleaned = trimLoose(cleaned);

    const auto codeBlocks = extractCodeBlocks(text);
    if (codeBlocks.size() == 1) {
        proposal.content = codeBlocks.front();
        proposal.summary = "Código extraído da resposta.";
        
        // Evaluate confidence: if there's a lot of prose around the block, confidence is medium
        bool hasSignificantProse = cleaned.length() > (codeBlocks.front().length() + 100);
        proposal.confidence = hasSignificantProse ? "medium" : "high";
        proposal.directlyApplicable = true;
        return true;
    }

    // 3. Fallback: mixed or ambiguous text still deserves a review modal.
    if (codeBlocks.empty() && !cleaned.empty()) {
        if (!looksLikeMixedExplanatoryResponse(cleaned)) {
            proposal.content = cleaned;
            proposal.summary = "Texto direto tratado como mudança.";
            proposal.confidence = "medium";
            proposal.directlyApplicable = true;
            return true;
        }

        proposal.content = cleaned;
        proposal.summary = "Resposta ambígua ou misturada. Revise e edite antes de aplicar.";
        proposal.confidence = "low";
        proposal.directlyApplicable = false;
        return true;
    }

    proposal.summary = "Resposta ambígua ou puramente explicativa.";
    proposal.confidence = "low";
    proposal.directlyApplicable = false;
    return !proposal.targetPath.empty();
}

std::string AgentUI::inferTaskMode(const std::string& goal) const {
    std::string lower = toLowerCopy(goal);
    const bool hasPathHint = lower.find('/') != std::string::npos ||
                             lower.find('\\') != std::string::npos ||
                             lower.find(".cpp") != std::string::npos ||
                             lower.find(".hpp") != std::string::npos ||
                             lower.find(".h") != std::string::npos ||
                             lower.find(".txt") != std::string::npos ||
                             lower.find(".md") != std::string::npos ||
                             lower.find("makefile") != std::string::npos ||
                             lower.find("doxyfile") != std::string::npos ||
                             lower.find(".json") != std::string::npos ||
                             lower.find(".yaml") != std::string::npos ||
                             lower.find(".yml") != std::string::npos;
    const bool documentationTask = containsAnyLower(lower, {
        "documentacao", "documentação", "documentar", "docs", "readme", "gerar doc"
    });
    const bool hasOperationalVerb = containsAnyLower(lower, {
        "crie", "criar", "gere", "gerar", "scaffold", "rodar", "executar",
        "corrigir build", "refator", "refactor", "renomear", "mover",
        "edite", "editar", "altere", "alterar", "inclua", "incluir",
        "adicione", "adicionar", "configure", "configurar", "apague", "deletar", "remover"
    });
    if (containsAnyLower(lower, {"crie arquivo", "criar arquivo", "gere projeto", "scaffold", "rodar", "executar", "corrigir build", "refator", "refactor", "renomear arquivo", "mover arquivo"}) ||
        (hasOperationalVerb && (hasPathHint || documentationTask)) ||
        documentationTask) {
        return "MISSION";
    }
    if (!inferActiveFileForGoal(goal).empty() && containsAnyLower(lower, {"inclua", "incluir", "continue", "continuar", "revise este texto", "reescreva", "melhore o texto", "edite", "ajuste o documento", "insira", "corrija", "corrigir"})) {
        return "ASSIST";
    }
    return "CHAT";
}

void AgentUI::runPythonAgent(const std::string& goal, const std::string& mode) {
    if (llmBusy || !orchestrator) return;
    llmBusy = true;
    
    std::thread([this, goal, mode]() {
        try {
            syncNativeToolsRuntime();
            thoughtStream = "Iniciada missão: " + goal + " (" + mode + ")";
            
            agent::network::OllamaOptions opts;
            opts.temperature = (reasoning == "high") ? 0.2f : 0.7f;
            if (ollama && ollama->getProvider() == agent::network::ModelProvider::LMStudio) {
                opts.num_ctx = 4096;
                opts.num_predict = 2048;
            }

            ollama->setModel(currentModel); // Sincroniza o modelo selecionado

            std::vector<agent::network::Message> contextHistory;
            {
                std::lock_guard<std::mutex> lock(msgMutex);
                contextHistory = llmHistory;
            }
            const std::string activeContext = buildActiveContextBlock();
            agent::core::Orchestrator::MissionCallbacks callbacks;
            callbacks.onMessageChunk = [this](const std::string& chunk) {
                std::lock_guard<std::mutex> lock(msgMutex);
                StructuredChatMessage& structured = ensureAssistantStructuredMessageLocked();
                history.back().text += chunk;
                if (structured.parts.empty() || structured.parts.back().type != MessagePartType::Text) {
                    structured.parts.push_back({MessagePartType::Text, "", "", "", false});
                }
                structured.parts.back().text += chunk;
                scrollToBottom = true;
            };
            callbacks.onThought = [this](const std::string& thought) {
                thoughtStream = thought;
                std::lock_guard<std::mutex> lock(msgMutex);
                StructuredChatMessage& structured = ensureAssistantStructuredMessageLocked();
                if (structured.parts.empty() || structured.parts.back().type != MessagePartType::Reasoning) {
                    structured.parts.push_back({MessagePartType::Reasoning, "", "reasoning", "", false});
                }
                structured.parts.back().text = thought;
            };
            callbacks.onAction = [this](const std::string& action) {
                thoughtStream = "Ação: " + action;
                std::lock_guard<std::mutex> lock(msgMutex);
                StructuredChatMessage& structured = ensureAssistantStructuredMessageLocked();
                history.back().text += "\n\nAÇÃO: " + action + "\n";
                const std::string toolName = extractToolNameFromAction(action);
                structured.parts.push_back({
                    MessagePartType::ToolCall,
                    action,
                    toolName.empty() ? "tool_call" : toolName,
                    "",
                    false
                });
            };
            callbacks.onObservation = [this](const std::string& obs) {
                // P1.4: Notificação visual para operações de escrita bem-sucedidas em Mission
                if (obs.rfind("Sucesso:", 0) == 0 || obs.rfind("Arquivo gravado", 0) == 0) {
                    // Extrai o path da mensagem de sucesso para exibir na UI
                    std::string notifMsg = obs;
                    // Tentativa de extrair arquivo para mensagem mais limpa
                    size_t emPos = obs.find(" em ");
                    if (emPos != std::string::npos) {
                        notifMsg = "✅ Arquivo modificado: " + obs.substr(emPos + 4);
                    }
                    thoughtStream = notifMsg;
                } else {
                    thoughtStream = "Observação recebida (" + std::to_string(obs.size()) + " bytes)";
                }

                std::lock_guard<std::mutex> lock(msgMutex);
                StructuredChatMessage& structured = ensureAssistantStructuredMessageLocked();
                std::string trimmedObs = obs;
                if (trimmedObs.size() > 4000) {
                    trimmedObs = trimmedObs.substr(0, 4000) + "\n...[observação truncada]...";
                }
                history.back().text += "\nOBSERVAÇÃO: " + trimmedObs + "\n";
                std::string resultName = "tool_result";
                for (auto it = structured.parts.rbegin(); it != structured.parts.rend(); ++it) {
                    if (it->type == MessagePartType::ToolCall && !it->name.empty()) {
                        resultName = it->name;
                        break;
                    }
                }
                const bool isError = trimmedObs.rfind("Erro", 0) == 0 || trimmedObs.rfind("ERRO", 0) == 0;
                structured.parts.push_back({MessagePartType::ToolResult, trimmedObs, resultName, "", isError});
            };

            callbacks.onMissionComplete = [this](const std::vector<agent::network::Message>& newHistory) {
                std::lock_guard<std::mutex> lock(msgMutex);
                llmHistory = newHistory;
            };
            callbacks.onComplete = [this](bool success) {
                llmBusy = false; // Reset ONLY when background thread completes
                thoughtStream = success ? "Missão concluída." : "Missão interrompida.";
                generateProjectMap();
                saveSession();
            };
            callbacks.onStreamStats = [this](const agent::network::OllamaStreamStats& stats) {
                std::lock_guard<std::mutex> lock(telemetryMutex);
                totalPromptTokens = stats.prompt_tokens;
                totalCompletionTokens = stats.completion_tokens;
                tokensPerSec = (stats.total_duration_ms > 0.0)
                    ? static_cast<float>(stats.completion_tokens / (stats.total_duration_ms / 1000.0))
                    : 0.0f;
                tokenRateMs = (stats.completion_tokens > 0)
                    ? static_cast<float>(stats.total_duration_ms / stats.completion_tokens)
                    : 0.0f;
            };

            // P0.4: Approval gate para delete_path — mostra modal e aguarda resposta (max 30s)
            callbacks.onApprovalRequired = [this](const std::string& toolName,
                                                   const nlohmann::json& args) -> bool {
                if (toolName != "delete_path") return true; // Só intercepta delete
                {
                    std::lock_guard<std::mutex> lock(deleteApprovalMutex);
                    deleteApprovalPath          = args.value("path", "(desconhecido)");
                    deleteApprovalIsRecursive   = args.value("recursive", false) ? "true" : "false";
                }
                deleteApprovalState.store(1); // pending — UI irá renderizar o modal

                // Polling por até 30 segundos
                auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                while (std::chrono::steady_clock::now() < deadline) {
                    int st = deleteApprovalState.load();
                    if (st == 2) { deleteApprovalState.store(0); return true;  } // approved
                    if (st == 3) { deleteApprovalState.store(0); return false; } // rejected
                    std::this_thread::sleep_for(std::chrono::milliseconds(80));
                }
                deleteApprovalState.store(0);
                return false; // Timeout → rejeitar por segurança
            };

            std::string fullGoal = goal;
            if (!activeContext.empty()) {
                fullGoal += "\n\n[CONTEXTO ATIVO]\n" + activeContext;
            }
            if (!projectGovernance.empty()) {
                fullGoal = "[GOVERNANÇA ATIVA: SIGA ESTAS REGRAS]\n" + projectGovernance + "\n\n[OBJETIVO ATUAL]\n" + fullGoal;
            }

            agent::core::MissionConfig missionConfig;
            missionConfig.agentSpec = currentAgentSpec();
            missionConfig.mode = "MISSION";
            missionConfig.reasoning = reasoning;
            missionConfig.access = access;
            missionConfig.contextSource = contextSource;

            orchestrator->runMission(contextHistory, fullGoal, missionConfig, 10, callbacks, opts);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(msgMutex);
            thoughtStream = "ERRO NA MISSÃO: " + std::string(e.what());
            llmBusy = false;
        }
    }).detach();
}

void AgentUI::drawChatWindow() {
    ImGui::BeginChild("ChatWindowChild", ImVec2(0, 0), true);
    const agent::core::AgentSpec agentSpec = currentAgentSpec();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "AGENT CHAT");
    ImGui::SameLine();
    ImGui::TextDisabled("| Provider:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("##ProviderSelector", &currentProviderIndex, "Ollama\0LM Studio\0")) {
        currentProvider = currentProviderIndex == 1 ? "LM Studio" : "Ollama";
        providerEndpoint = currentProviderIndex == 1 ? "http://127.0.0.1:1234" : "http://localhost:11434";
        if (ollama) {
            ollama->configureBackend(
                currentProviderIndex == 1 ? agent::network::ModelProvider::LMStudio : agent::network::ModelProvider::Ollama,
                providerEndpoint
            );
            ollamaVersion = ollama->fetchVersion();
            availableModels = ollama->listModels();
            if (!availableModels.empty()) {
                currentModel = availableModels.front();
                ollama->setModel(currentModel);
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("| Model:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("##ModelSelector", currentModel.c_str())) {
        for (const auto& model : availableModels) {
            bool isSelected = (currentModel == model);
            if (ImGui::Selectable(model.c_str(), isSelected)) {
                currentModel = model;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("@ %s", currentProvider.c_str());
    ImGui::SameLine();
    int reasoningIdx = (reasoning == "low") ? 0 : (reasoning == "high" ? 2 : 1);
    ImGui::SetNextItemWidth(95);
    if (ImGui::Combo("##ReasoningSelector", &reasoningIdx, "low\0medium\0high\0")) {
        reasoning = reasoningLabel(reasoningIdx);
    }
    ImGui::SameLine();
    int accessIdx = 1;
    if (access == "read-only") accessIdx = 0;
    else if (access == "full-access") accessIdx = 2;
    ImGui::SetNextItemWidth(135);
    if (ImGui::Combo("##AccessSelector", &accessIdx, "Read-only\0Workspace-write\0Full-access\0")) {
        access = accessLabel(accessIdx);
        std::transform(access.begin(), access.end(), access.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        syncNativeToolsRuntime();
    }
    ImGui::SameLine();
    int contextIdx = 0;
    if (contextSource == "workspace+library") contextIdx = 1;
    else if (contextSource == "workspace+library+web") contextIdx = 2;
    ImGui::SetNextItemWidth(180);
    if (ImGui::Combo("##ContextSelector", &contextIdx, "workspace\0workspace+library\0workspace+library+web\0")) {
        contextSource = contextSourceLabel(contextIdx);
        syncNativeToolsRuntime();
    }
    ImGui::Separator();
    const std::string inferredMode = inferTaskMode(inputBuf);
    std::string inferredActiveFile = pinnedActiveFile;
    if (inferredActiveFile.empty()) inferredActiveFile = inferActiveFileForGoal(inputBuf);
    const std::string ambiguityNote = inferActiveFileAmbiguityNote(inputBuf);
    const std::vector<std::string> agentTools = currentAgentToolNames();
    ImGui::TextDisabled("Modo sugerido: %s", inferredMode == "MISSION" ? "Missao" : "Chat assistido");
    ImGui::SameLine();
    ImGui::TextDisabled("| Agent: %s", agentSpec.displayName.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("| Tools: %s", agentSpec.toolProfile.c_str());
    if (!agentTools.empty()) {
        std::string toolPreview = agentTools.front();
        const size_t previewCount = std::min<size_t>(agentTools.size(), 3);
        for (size_t i = 1; i < previewCount; ++i) toolPreview += ", " + agentTools[i];
        if (agentTools.size() > previewCount) toolPreview += ", ...";
        ImGui::SameLine();
        ImGui::TextDisabled("| Disponiveis: %s", toolPreview.c_str());
    }
    if (!inferredActiveFile.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("| Arquivo em foco%s: %s", pinnedActiveFile.empty() ? " (inferido)" : " (pinado)", fs::path(inferredActiveFile).filename().string().c_str());
        if (!pinnedActiveFile.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("X##ClearPinTop")) {
                pinnedActiveFile.clear();
            }
        }
    }
    if (!ambiguityNote.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "%s", ambiguityNote.c_str());
    }

    float footerHeight = ImGui::GetFrameHeightWithSpacing() + 55.0f;
    if (!selectedFile.empty()) footerHeight += 25.0f;

    ImGui::BeginChild("ChatHistory", ImVec2(0, -footerHeight), false);
    {
        std::lock_guard<std::mutex> lock(msgMutex);
        for (size_t i = 0; i < history.size(); ++i) {
            const auto& msg = history[i];
            if (msg.role == "user") {
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "[VOCÊ]");
                ImGui::TextWrapped("%s", msg.text.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "[AGENT]");
                AgentMessageSections sections = splitAgentMessage(msg.text);
                
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::SmallButton("Copiar")) ImGui::SetClipboardText(msg.text.c_str());
                ChangeProposal proposal;
                const bool hasProposal = buildChangeProposalFromAssistantText(msg.text, proposal);
                if (hasProposal) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Propor Mudanca")) {
                        pendingChangeProposal = proposal;
                        std::string currentText = (!editorFilePath.empty() && proposal.targetPath == editorFilePath)
                            ? (editorUsesPlainText ? editorPlainTextBuffer : codeEditor.GetText())
                            : loadWorkspaceFileText(proposal.targetPath);
                        pendingChangeDiff = buildSimpleDiffPreview(currentText, proposal.content);
                        std::snprintf(pendingChangeTargetBuf, sizeof(pendingChangeTargetBuf), "%s", pendingChangeProposal.targetPath.c_str());
                        changeProposalVisible = true;
                        thoughtStream = proposal.directlyApplicable
                            ? "Mudanca pronta para revisao."
                            : "Mudanca ambigua aberta para revisao manual.";
                    }
                }
                ImGui::PopID();

                ImGui::BeginChild(("AgentMsgBlock_" + std::to_string(i)).c_str(), ImVec2(0, 300), true);
                if (ImGui::BeginTabBar("AgentMessageTabs")) {
                    StructuredChatMessage structured = (i < structuredHistory.size())
                        ? structuredHistory[i]
                        : buildStructuredMessage(msg);
                    if (ImGui::BeginTabItem("Resposta")) {
                        for (const auto& part : structured.parts) {
                            if (part.type != MessagePartType::Text) continue;
                            renderMarkdown(part.text);
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Pensamento")) {
                        bool hasReasoning = false;
                        for (const auto& part : structured.parts) {
                            if (part.type != MessagePartType::Reasoning) continue;
                            hasReasoning = true;
                            if (!part.name.empty()) {
                                ImGui::TextColored(ImVec4(0.8f, 0.75f, 0.45f, 1.0f), "[%s]", part.name.c_str());
                            }
                            ImGui::TextWrapped("%s", part.text.c_str());
                            ImGui::Separator();
                        }
                        if (!hasReasoning) {
                            ImGui::TextDisabled("Nenhum reasoning estruturado disponível.");
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Ações")) {
                        bool hasToolCalls = false;
                        for (const auto& part : structured.parts) {
                            if (part.type != MessagePartType::ToolCall) continue;
                            hasToolCalls = true;
                            std::string header = part.name.empty() ? "Tool call" : ("Tool: " + part.name);
                            if (ImGui::CollapsingHeader(header.c_str())) {
                                ImGui::TextWrapped("%s", part.text.c_str());
                            }
                        }
                        if (!hasToolCalls) {
                            ImGui::TextDisabled("Nenhuma ferramenta foi utilizada nesta resposta.");
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Logs")) {
                        bool hasLogs = false;
                        for (const auto& part : structured.parts) {
                            if (part.type != MessagePartType::ToolResult) continue;
                            hasLogs = true;
                            if (!part.name.empty()) {
                                ImGui::TextColored(
                                    part.isError ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f) : ImVec4(0.55f, 0.85f, 1.0f, 1.0f),
                                    "[%s]",
                                    part.name.c_str());
                            }
                            ImGui::TextUnformatted(part.text.c_str());
                            ImGui::Separator();
                        }
                        if (!hasLogs) {
                            ImGui::TextDisabled("Nenhum log técnico disponível.");
                        }
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::EndChild();
            }
            ImGui::Separator();
        }
    }
    if (scrollToBottom) { ImGui::SetScrollHereY(1.0f); scrollToBottom = false; }
    ImGui::EndChild();

    ImGui::Separator();
    
    // Determinar qual arquivo mostrar na barra de status
    std::string displayFile = pinnedActiveFile;
    if (displayFile.empty()) {
        displayFile = inferActiveFileForGoal(inputBuf);
    }

    if (!displayFile.empty()) {
        ImGui::TextDisabled("Arquivo em foco%s: %s", 
            pinnedActiveFile.empty() ? " (inferido)" : " (pinado)", 
            fs::path(displayFile).filename().string().c_str());
        
        ImGui::SameLine();
        if (ImGui::SmallButton("X##ClearPin")) {
            pinnedActiveFile.clear();
        }
    }

    ImGui::PushItemWidth(-1);
    if (ImGui::InputTextWithHint("##ChatInput", "Questione ou use /fix, /explain, /doc...", inputBuf, sizeof(inputBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (std::strlen(inputBuf) > 0 && !llmBusy) {
            std::string queryText = handleSlashCommand(inputBuf);
            {
                std::lock_guard<std::mutex> lock(msgMutex);
                appendHistoryMessageLocked({"user", inputBuf}); // Keep the command in UI history
            }
            std::memset(inputBuf, 0, sizeof(inputBuf));
            scrollToBottom = true;
            runPythonAgent(queryText, "AUTO");
        }
    }
    ImGui::PopItemWidth();

    if (llmBusy) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Agent está pensando...");
        ImGui::SameLine();
        if (ImGui::SmallButton("PARAR")) {
            if (ollama) ollama->requestStop();
            if (orchestrator) orchestrator->stopMission();
            thoughtStream = "Parada forçada pelo usuário.";
        }
    } else {
        if (ImGui::Button("SND (Chat)", ImVec2(100, 0))) {
            if (std::strlen(inputBuf) > 0) {
                std::string queryText = inputBuf;
                {
                    std::lock_guard<std::mutex> lock(msgMutex);
                    appendHistoryMessageLocked({"user", queryText});
                }
                std::memset(inputBuf, 0, sizeof(inputBuf));
                scrollToBottom = true;
                runPythonAgent(queryText, "CHAT");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("MISSION (Auto)", ImVec2(120, 0))) {
             if (std::strlen(inputBuf) > 0) {
                std::string queryText = inputBuf;
                {
                    std::lock_guard<std::mutex> lock(msgMutex);
                    appendHistoryMessageLocked({"user", "MISSÃO: " + queryText});
                }
                std::memset(inputBuf, 0, sizeof(inputBuf));
                scrollToBottom = true;
                runPythonAgent(queryText, "MISSION");
            }
        }
    }

    ImGui::EndChild();
}

} // namespace agent::ui
