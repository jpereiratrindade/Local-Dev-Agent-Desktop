#include "AgentUI_Internal.hpp"
#include "AgentSpec.hpp"
#include "imgui.h"
#include "OllamaClient.hpp"
#include <fstream>
#include <sstream>

namespace agent::ui {

void AgentUI::drawOpenFolderPickerDialog() {
    if (openFolderPickerRequested) {
        ImGui::OpenPopup("Escolher Pasta do Projeto###FolderPicker");
        openFolderPickerRequested = false;
        openFolderPickerVisible = true;
    }

    if (ImGui::BeginPopupModal("Escolher Pasta do Projeto###FolderPicker", &openFolderPickerVisible, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Navegue e selecione a pasta raiz:");
        ImGui::Separator();
        
        ImGui::Text("Atual: %s", folderPickerCurrentDir.c_str());
        if (ImGui::Button("..")) {
            fs::path p(folderPickerCurrentDir);
            if (p.has_parent_path()) {
                folderPickerCurrentDir = p.parent_path().string();
                std::snprintf(folderPickerPathBuf, sizeof(folderPickerPathBuf), "%s", folderPickerCurrentDir.c_str());
            }
        }
        
        ImGui::BeginChild("FolderList", ImVec2(500, 300), true);
        try {
            fs::path current(folderPickerCurrentDir);
            for (const auto& entry : fs::directory_iterator(current)) {
                if (entry.is_directory()) {
                    std::string name = entry.path().filename().string();
                    if (ImGui::Selectable((name + "/").c_str())) {
                        folderPickerCurrentDir = entry.path().string();
                        std::snprintf(folderPickerPathBuf, sizeof(folderPickerPathBuf), "%s", folderPickerCurrentDir.c_str());
                    }
                }
            }
        } catch (const std::exception& e) {
            ImGui::TextColored(ImVec4(1,0,0,1), "Erro ao ler pasta: %s", e.what());
        }
        ImGui::EndChild();

        ImGui::InputText("Caminho", folderPickerPathBuf, sizeof(folderPickerPathBuf));
        
        if (ImGui::Button("Selecionar Esta Pasta", ImVec2(200, 0))) {
            lastResolvedProjectRoot = resolveProjectRoot(folderPickerPathBuf);
            currentProjectRoot = lastResolvedProjectRoot;
            hasOpenProject = true;
            syncNativeToolsRuntime();
            history.clear();
            currentSessionFile = "last_session.json";
            loadSession();
            generateProjectMap();
            thoughtStream = "Projeto aberto em: " + currentProjectRoot;
            ImGui::CloseCurrentPopup();
            openFolderPickerVisible = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
            openFolderPickerVisible = false;
        }
        ImGui::EndPopup();
    }
}

void AgentUI::renderModelManagerModal() {
    if (modelManagerRequested) {
        ImGui::OpenPopup("Model Manager###ModelManagerModal");
        modelManagerRequested = false;
        modelManagerVisible = true;
    }

    if (ImGui::BeginPopupModal("Model Manager###ModelManagerModal", &modelManagerVisible, ImGuiWindowFlags_AlwaysAutoResize)) {
        const agent::core::AgentSpec agentSpec = currentAgentSpec();
        const std::vector<std::string> agentTools = currentAgentToolNames();
        ImGui::Text("Provider: %s", currentProvider.c_str());
        ImGui::Text("Endpoint: %s", providerEndpoint.c_str());
        ImGui::Text("%s Version: %s", currentProvider.c_str(), ollamaVersion.empty() ? "unknown" : ollamaVersion.c_str());
        ImGui::Text("Agent: %s", agentSpec.displayName.c_str());
        ImGui::Text("Tool profile: %s", agentSpec.toolProfile.c_str());
        ImGui::Separator();
        
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Modelos Presentes:");
        for (const auto& m : availableModels) ImGui::BulletText("%s", m.c_str());
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "Ferramentas Visiveis Para Este Agent:");
        if (agentTools.empty()) {
            ImGui::TextDisabled("(nenhuma)");
        } else {
            ImGui::BeginChild("VisibleToolsList", ImVec2(460, 140), true);
            for (const auto& tool : agentTools) {
                ImGui::BulletText("%s", tool.c_str());
            }
            ImGui::EndChild();
        }
        
        ImGui::Separator();
        if (currentProvider == "Ollama") {
            ImGui::InputText("Nome do Modelo", modelPullNameBuf, sizeof(modelPullNameBuf));
            if (pullingModel) {
                ImGui::ProgressBar(pullProgress, ImVec2(-FLT_MIN, 0), pullStatus.c_str());
            } else {
                if (ImGui::Button("Baixar Modelo")) {
                    pullingModel = true;
                    ollama->pullModel(modelPullNameBuf, [this](const std::string& s, float p) {
                        pullStatus = s; pullProgress = p;
                    }, [this](bool ok) {
                        pullingModel = false;
                        if (ok) availableModels = ollama->listModels();
                    });
                }
            }
        } else {
            ImGui::TextDisabled("No LM Studio, carregue o modelo no servidor local.");
        }
        
        if (ImGui::Button("Fechar")) {
            ImGui::CloseCurrentPopup();
            modelManagerVisible = false;
        }
        ImGui::EndPopup();
    }
}

void AgentUI::drawContextPolicyDialog() {
    if (contextPolicyDialogRequested) {
        ImGui::OpenPopup("Política de Contexto e Bibliotecas###ContextPolicy");
        contextPolicyDialogRequested = false;
        contextPolicyDialogVisible = true;
    }
    if (ImGui::BeginPopupModal("Política de Contexto e Bibliotecas###ContextPolicy", &contextPolicyDialogVisible, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Configuração de Domínios e Bibliotecas Locais");
        ImGui::Separator();
        // ... (truncated for brevity in execution, will implement fully)
        if (ImGui::Button("OK")) {
            ImGui::CloseCurrentPopup();
            contextPolicyDialogVisible = false;
        }
        ImGui::EndPopup();
    }
}

void AgentUI::drawGovernedProjectDialog() {
    if (governedProjectDialogRequested) {
        ImGui::OpenPopup("Novo Projeto Governado###GovernedProject");
        governedProjectDialogRequested = false;
        governedProjectDialogVisible = true;
    }
    if (ImGui::BeginPopupModal("Novo Projeto Governado###GovernedProject", &governedProjectDialogVisible, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Nome", governedProjectNameBuf, sizeof(governedProjectNameBuf));
        if (ImGui::Button("Criar")) {
            // Logic handled in AgentUI.cpp for now or moved to a specialized helper
            ImGui::CloseCurrentPopup();
            governedProjectDialogVisible = false;
        }
        ImGui::EndPopup();
    }
}

void AgentUI::drawChangeProposalDialog() {
    if (changeApprovalState == ApprovalState::Pending && !changeProposalVisible) {
        changeProposalVisible = true;
    }

    if (changeProposalVisible) {
        ImGui::OpenPopup("Revisar Mudanca###ChangeProposal");
    }

    bool open = true;

    ImGui::SetNextWindowSize(ImVec2(850, 650), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Revisar Mudanca###ChangeProposal", &open)) {
        
        // Confidence/Safety Bar
        ImVec4 confColor = ImVec4(0.4f, 0.8f, 0.4f, 1.0f); // High (Green)
        std::string confText = "SEGURO: Proposta estruturada ou limpa.";
        if (pendingChangeProposal.confidence == "medium") {
            confColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); // Medium (Yellow)
            confText = "ATENÇAO: Resposta mista detectada. Revise com cuidado.";
        } else if (pendingChangeProposal.confidence == "low") {
            confColor = ImVec4(0.9f, 0.4f, 0.4f, 1.0f); // Low (Red)
            confText = "RISCO: Resposta ambígua ou técnica. Aplicaçao direta desencorajada.";
        }

        ImGui::PushStyleColor(ImGuiCol_Text, confColor);
        ImGui::Text("STATUS: %s", confText.c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
        
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "OBJETIVO:");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", pendingChangeProposal.summary.empty() ? "Nenhuma descrição informada." : pendingChangeProposal.summary.c_str());
        
        ImGui::Spacing();
        ImGui::Columns(2, "ProposalMeta", false);
        ImGui::SetColumnWidth(0, 110);
        
        ImGui::TextDisabled("Operação:"); ImGui::NextColumn();
        ImGui::Text("%s", pendingChangeProposal.kind.c_str()); ImGui::NextColumn();
        
        ImGui::TextDisabled("Caminho Alvo:"); ImGui::NextColumn();
        ImGui::PushItemWidth(-1);
        ImGui::InputText("##TargetFile", pendingChangeTargetBuf, sizeof(pendingChangeTargetBuf));
        ImGui::PopItemWidth();
        ImGui::NextColumn();
        ImGui::Columns(1);

        ImGui::Separator();

        if (ImGui::BeginTabBar("ProposalTabs")) {
            if (ImGui::BeginTabItem("Diff Visual")) {
                if (!pendingChangeDiff.empty()) {
                    ImGui::BeginChild("ChangeProposalDiff", ImVec2(0, 320), true, ImGuiWindowFlags_HorizontalScrollbar);
                    std::istringstream diffStream(pendingChangeDiff);
                    std::string diffLine;
                    while (std::getline(diffStream, diffLine)) {
                        if (!diffLine.empty() && diffLine[0] == '+') {
                            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "%s", diffLine.c_str());
                        } else if (!diffLine.empty() && diffLine[0] == '-') {
                            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", diffLine.c_str());
                        } else {
                            ImGui::TextDisabled("%s", diffLine.c_str());
                        }
                    }
                    ImGui::EndChild();
                } else {
                    ImGui::TextDisabled("Nenhuma diferença detectada (ou arquivo novo/append).");
                }
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Conteúdo (Editável)")) {
                ImGui::TextDisabled("Você pode ajustar o código abaixo antes de aplicar:");
                ImGui::InputTextMultiline("##EditProposalContent", &pendingChangeProposal.content, ImVec2(-1, 300), ImGuiInputTextFlags_AllowTabInput);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        ImGui::Spacing();

        float btnWidth = 160.0f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float totalWidth = (btnWidth * 3) + (spacing * 2);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalWidth) * 0.5f);

        if (ImGui::Button("Aplicar Mudança", ImVec2(btnWidth, 45))) {
            if (pendingChangeProposal.source == ChangeProposalSource::NativeTool) {
                {
                    std::lock_guard<std::mutex> lock(changeApprovalMutex);
                    changeApprovalState = ApprovalState::Approved;
                }
                changeApprovalCv.notify_one();
                thoughtStream = "Aprovação concedida ao Orchestrator.";
            } else {
                pendingChangeProposal.targetPath = trimLoose(pendingChangeTargetBuf);
                if (!pendingChangeProposal.targetPath.empty() && ensureEditorTarget(pendingChangeProposal.targetPath) &&
                    applyPartialChangeToEditor(pendingChangeProposal, false)) {
                    lastChangeTargetPath = pendingChangeProposal.targetPath;
                    thoughtStream = "Mudança aplicada com sucesso no editor.";
                } else {
                    thoughtStream = "ERRO: Falha ao aplicar mudança estruturada.";
                }
            }
            pendingChangeProposal = {};
            pendingChangeDiff.clear();
            changeProposalVisible = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Aplicar e Salvar", ImVec2(btnWidth, 45))) {
            if (pendingChangeProposal.source == ChangeProposalSource::NativeTool) {
                {
                    std::lock_guard<std::mutex> lock(changeApprovalMutex);
                    changeApprovalState = ApprovalState::Approved;
                }
                changeApprovalCv.notify_one();
                thoughtStream = "Aprovação concedida ao Orchestrator.";
            } else {
                pendingChangeProposal.targetPath = trimLoose(pendingChangeTargetBuf);
                if (!pendingChangeProposal.targetPath.empty() && ensureEditorTarget(pendingChangeProposal.targetPath) &&
                    applyPartialChangeToEditor(pendingChangeProposal, true)) {
                    lastChangeTargetPath = pendingChangeProposal.targetPath;
                    thoughtStream = "Mudança aplicada e persistida com sucesso.";
                } else {
                    thoughtStream = "ERRO: Falha ao persistir mudança estruturada.";
                }
            }
            pendingChangeProposal = {};
            pendingChangeDiff.clear();
            changeProposalVisible = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1.00f));
        if (ImGui::Button("Descartar", ImVec2(btnWidth, 45))) {
            if (pendingChangeProposal.source == ChangeProposalSource::NativeTool) {
                {
                    std::lock_guard<std::mutex> lock(changeApprovalMutex);
                    changeApprovalState = ApprovalState::Rejected;
                }
                changeApprovalCv.notify_one();
                thoughtStream = "Aprovação negada ao Orchestrator.";
            } else {
                thoughtStream = "Mudança descartada pelo usuário.";
            }
            pendingChangeProposal = {};
            pendingChangeDiff.clear();
            changeProposalVisible = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();

        ImGui::EndPopup();
    }

    if (!open && changeProposalVisible) {
        if (changeApprovalState == ApprovalState::Pending) {
            {
                std::lock_guard<std::mutex> lock(changeApprovalMutex);
                changeApprovalState = ApprovalState::Rejected;
            }
            changeApprovalCv.notify_one();
        }
        changeProposalVisible = false;
    }
}

// P0.4: Modal de confirmação para delete_path
void AgentUI::drawDeleteApprovalModal() {
    if (deleteApprovalState.load() != 1) return; // Só exibir quando pendente

    ImGui::OpenPopup("Confirmar Remoção###DeleteApproval");
    ImGui::SetNextWindowSize(ImVec2(520, 220), ImGuiCond_Always);
    bool open = true;
    if (ImGui::BeginPopupModal("Confirmar Remoção###DeleteApproval", &open, ImGuiWindowFlags_NoResize)) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "⚠  O agente quer DELETAR o seguinte:");
        ImGui::Separator();
        ImGui::Spacing();

        {
            std::lock_guard<std::mutex> lock(deleteApprovalMutex);
            ImGui::TextWrapped("Caminho: %s", deleteApprovalPath.c_str());
            if (deleteApprovalIsRecursive == "true") {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Modo: recursive=true (removerá todo o conteúdo)");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.65f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Confirmar — Deletar", ImVec2(200, 36))) {
            deleteApprovalState.store(2); // approved
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();

        if (ImGui::Button("Cancelar — Não deletar", ImVec2(200, 36))) {
            deleteApprovalState.store(3); // rejected
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // Se o popup foi fechado sem escolha explícita, rejeitar
    if (!open && deleteApprovalState.load() == 1) {
        deleteApprovalState.store(3);
    }
}

// P3.2: Modal de Busca Rápida de Arquivos (Ctrl+P)
void AgentUI::drawQuickOpenModal() {
    if (quickOpenVisible) {
        ImGui::OpenPopup("Quick Open (Ctrl+P)");
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }

    bool open = quickOpenVisible;
    if (ImGui::BeginPopupModal("Quick Open (Ctrl+P)", &open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("Buscar Arquivo no Projeto:");
        if (quickOpenVisible) {
            ImGui::SetKeyboardFocusHere();
            quickOpenVisible = false; // flag de trigger já consumida
        }
        
        bool executeOpen = false;
        if (ImGui::InputText("##QuickOpenInput", quickOpenBuf, sizeof(quickOpenBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            executeOpen = true; // Enter pressionado no campo de busca
        }

        ImGui::Separator();
        
        // Filtro muito simples (case-insensitive contains)
        std::string query = quickOpenBuf;
        std::transform(query.begin(), query.end(), query.begin(), ::tolower);
        
        std::vector<std::string> filtered;
        for (const auto& path : quickOpenMatches) {
            if (query.empty()) {
                filtered.push_back(path);
            } else {
                std::string lowerPath = path;
                std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
                if (lowerPath.find(query) != std::string::npos) {
                    filtered.push_back(path);
                }
            }
        }

        ImGui::BeginChild("QuickOpenResults", ImVec2(0, 0), true);
        int selectionIndex = -1;
        for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
            if (ImGui::Selectable(filtered[i].c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(0)) {
                    selectionIndex = i;
                    executeOpen = true;
                }
            }
        }
        ImGui::EndChild();

        if (executeOpen && !filtered.empty()) {
            std::string target = (selectionIndex >= 0) ? filtered[selectionIndex] : filtered[0];
            fs::path absolutePath = fs::path(currentProjectRoot) / target;
            pinnedActiveFile = absolutePath.string();
            thoughtStream = "Aberto arquivo via Quick Open: " + target;
            open = false; // Fechar modal
        }

        ImGui::EndPopup();
    }
    
    if (!open) {
        quickOpenVisible = false;
    }
}

} // namespace agent::ui
