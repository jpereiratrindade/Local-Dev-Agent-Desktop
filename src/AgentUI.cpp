#include "AgentUI.hpp"
#include "imgui.h"
#include "imgui_internal.h"
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
    currentProjectRoot = normalizeRootPath(currentProjectRoot);
    if (ollama) {
        agent::core::registerNativeTools(currentProjectRoot);
        if (orchestrator) delete orchestrator;
        orchestrator = new agent::core::Orchestrator(ollama, currentProjectRoot);
        detectProjectTech();
    }
}

void AgentUI::render() {
    // 1. Menu e Atalhos Globais
    drawMainMenu();
    
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
         auto dir = pfd::select_folder("Selecionar Projeto Agent", currentProjectRoot).result();
         if (!dir.empty()) {
             currentProjectRoot = normalizeRootPath(dir);
             setOllama(ollama);
         }
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N)) {
         newDialogue();
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
}

void AgentUI::drawMainMenu() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Arquivo")) {
            if (ImGui::MenuItem("Novo Diálogo", "Ctrl+N")) { newDialogue(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Abrir Pasta...", "Ctrl+O")) {
                auto dir = pfd::select_folder("Selecionar Projeto Agent", currentProjectRoot).result();
                if (!dir.empty()) {
                    currentProjectRoot = normalizeRootPath(dir);
                    setOllama(ollama);
                }
            }
            if (ImGui::MenuItem("Fechar Pasta")) {
                currentProjectRoot = normalizeRootPath(fs::current_path().string());
                projectMap = "";
                setOllama(ollama);
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
            ImGui::EndMenu();
        }
        
        std::string projectLabel = projectLabelFromRoot(currentProjectRoot);
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
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "EXPLORER");
    ImGui::Separator();
    
    if (fs::exists(currentProjectRoot)) {
        renderDirectory(currentProjectRoot);
    } else {
        ImGui::TextDisabled("Pasta nao selecionada.");
    }
    
    ImGui::EndChild();
}

void AgentUI::renderDirectory(const std::string& path) {
    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            const auto& entryPath = entry.path();
            std::string filename = entryPath.filename().string();
            
            if (entry.is_directory()) {
                std::string folderLabel = "📁 " + filename;
                if (ImGui::TreeNode(folderLabel.c_str())) {
                    renderDirectory(entryPath.string());
                    ImGui::TreePop();
                }
            } else {
                std::string fileLabel = "📄 " + filename;
                bool isSelected = (selectedFile == entryPath.string());
                if (ImGui::Selectable(fileLabel.c_str(), isSelected)) {
                    selectedFile = entryPath.string();
                    // Carregar conteúdo do arquivo (em uma thread ou direto se for pequeno)
                }
            }
        }
    } catch (...) {}
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
    ImGui::Checkbox("Autônomo (Codex)", &autonomousMode);

    ImGui::PushItemWidth(-70);
    bool submitted = ImGui::InputText("##Input", inputBuf, IM_ARRAYSIZE(inputBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    
    // Atalho Ninja: Ctrl + Enter
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter)) submitted = true;

    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (submitted || ImGui::Button("Send")) {
        if (inputBuf[0] != '\0' && ollama) {
            std::string userMsg = inputBuf;
            
            // Contexto V5: Injeção de Autoridade via System Prompt
            std::string systemPrompt = "VOCÊ É O AGENT. MODO ANALISTA TÉCNICO DE ELITE.\n"
                                       "CONTEXTO FÍSICO DO WORKSPACE:\n" + projectMap + "\n"
                                       "DIRETRIZES IMUTÁVEIS:\n"
                                       "1. O mapa acima é a sua única fonte de verdade sobre a estrutura do projeto.\n"
                                       "2. Nunca diga que não conhece o projeto ou peça dados de estrutura.\n"
                                       "3. Se um arquivo estiver em <active_file>, trate-o como seu código principal.\n"
                                       "4. Respostas cordiais e extremamente técnicas baseadas no contexto fornecido.";

            std::string fullMsg = "";
            if (!selectedFile.empty()) {
                std::ifstream ifs(selectedFile);
                std::stringstream buffer; buffer << ifs.rdbuf();
                fullMsg += "<active_file name=\"" + fs::path(selectedFile).filename().string() + "\">\n" + buffer.str() + "\n</active_file>\n";
            }
            fullMsg += "<user_query>\n" + userMsg + "\n</user_query>";

            {
                std::lock_guard<std::mutex> lock(msgMutex);
                history.push_back({"user", userMsg});
                history.push_back({"assistant", "..."});
                thoughtStream = "Modo Analista de Elite: Consultando Realidade do Workspace...";
                saveSession();
            }
            inputBuf[0] = '\0';
            scrollToBottom = true;

            if (autonomousMode) {
                runPythonAgent(userMsg, "AGENT");
            } else {
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
                }, [this](bool success, agent::network::OllamaClient::StreamStats stats) {
                    std::lock_guard<std::mutex> lock(msgMutex);
                    totalPromptTokens += stats.prompt_tokens;
                    totalCompletionTokens += stats.completion_tokens;
                    tokensPerSec = (stats.total_duration_ms > 0) ? (stats.completion_tokens / (stats.total_duration_ms / 1000.0)) : 0;
                    thoughtStream = "Transmissão concluída. (+" + std::to_string(stats.completion_tokens) + " tkn)";
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
    std::string shortHint = hint;
    const std::string prefix = "Vocação: ";
    if (shortHint.rfind(prefix, 0) == 0) shortHint = shortHint.substr(prefix.size());
    ImGui::TextColored(ImVec4(0.85f, 0.8f, 0.45f, 1.0f), "%s", modelBadge.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", shortHint.c_str());
    
    ImGui::End();
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
    currentProjectRoot = normalizeRootPath(currentProjectRoot);
    detectedTech = "Desconhecida/Outra";
    detectedDeps = "";
    try {
        generateProjectMap();
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
            prefix += entry.is_directory() ? "├── 📁 " : "├── 📄 ";
            
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
    try {
        std::string sessionDir = (fs::path(currentProjectRoot) / ".agent" / "sessions").string();
        if (!fs::exists(sessionDir)) fs::create_directories(sessionDir);
        
        nlohmann::json j;
        j["history"] = nlohmann::json::array();
        for (const auto& msg : history) {
            j["history"].push_back({{"role", msg.role}, {"text", msg.text}});
        }
        j["tokens"] = {{"prompt", totalPromptTokens}, {"completion", totalCompletionTokens}};
        
        std::ofstream o(fs::path(sessionDir) / "last_session.json");
        o << j.dump(4);
    } catch (...) {}
}

void AgentUI::loadSession() {
    try {
        std::string sessionFile = (fs::path(currentProjectRoot) / ".agent" / "sessions" / "last_session.json").string();
        if (!fs::exists(sessionFile)) { history.clear(); return; }

        std::ifstream i(sessionFile);
        nlohmann::json j; i >> j;
        
        history.clear();
        for (const auto& msg : j["history"]) {
            std::string role = msg["role"];
            if (role == "agent") role = "assistant";
            history.push_back({role, msg["text"]});
        }
        totalPromptTokens = j["tokens"].value("prompt", 0);
        totalCompletionTokens = j["tokens"].value("completion", 0);
        scrollToBottom = true;
    } catch (...) {
        history.clear();
    }
}

void AgentUI::newDialogue() {
    history.clear();
    totalPromptTokens = 0;
    totalCompletionTokens = 0;
    tokensPerSec = 0;
    saveSession();
    thoughtStream = "Novo diálogo iniciado. Contexto limpo.";
}

void AgentUI::runPythonAgent(const std::string& goal, const std::string& mode) {
    if (!orchestrator) return;

    agent::core::Orchestrator::MissionCallbacks cb;
    cb.onThought = [this](const std::string& thought) {
        std::lock_guard<std::mutex> lock(msgMutex);
        thoughtStream = "🧠 PENSANDO:\n" + thought;
    };
    cb.onAction = [this](const std::string& action) {
        std::lock_guard<std::mutex> lock(msgMutex);
        thoughtStream += "\n🚀 AÇÃO: " + action;
    };
    cb.onObservation = [this](const std::string& obs) {
        std::lock_guard<std::mutex> lock(msgMutex);
        thoughtStream += "\n👁️ OBSERVAÇÃO: " + (obs.length() > 200 ? obs.substr(0, 200) + "..." : obs);
    };
    cb.onMessageChunk = [this](const std::string& chunk) {
        std::lock_guard<std::mutex> lock(msgMutex);
        if (!history.empty() && history.back().role == "assistant") {
            if (history.back().text == "...") history.back().text = "";
            else history.back().text += "\n\n---\n\n"; // Separador de passos
            history.back().text += chunk;
            scrollToBottom = true;
        }
        saveSession();
    };
    cb.onComplete = [this](bool success) {
        std::lock_guard<std::mutex> lock(msgMutex);
        thoughtStream += "\n🏁 Missão Finalizada Nativamente.";
        saveSession();
    };

    {
        std::lock_guard<std::mutex> lock(msgMutex);
        thoughtStream = "🚀 MISSION START: Iniciando Orquestrador C++20 Nativo...";
    }

    orchestrator->runMission(goal, mode, 10, cb);
}

} // namespace agent::ui
