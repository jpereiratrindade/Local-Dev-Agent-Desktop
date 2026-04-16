#include "Orchestrator.hpp"
#include <regex>
#include <iostream>
#include <thread>
#include <fstream>
#include <sstream>

namespace agent::core {

Orchestrator::Orchestrator(agent::network::OllamaClient* client, const std::string& workspaceRoot)
    : ollama(client), workspaceRoot(workspaceRoot) {}

void Orchestrator::runMission(const std::string& goal, const std::string& mode, 
                             int maxSteps, MissionCallbacks callbacks) {
    stopRequested = false;
    history.clear();
    
    std::thread([this, goal, mode, maxSteps, callbacks]() {
        std::string currentGoal = goal;
        std::string systemPrompt = buildSystemPrompt(mode);
        history.push_back({"system", systemPrompt});
        history.push_back({"user", "META: " + currentGoal + "\n\nInicie pela menor ação verificável possível."});
        bool completed = false;

        for (int step = 0; step < maxSteps; ++step) {
            if (stopRequested) break;

            if (callbacks.onAction) callbacks.onAction("PASSO " + std::to_string(step + 1) + " / " + std::to_string(maxSteps));
            std::string response = ollama->chat(history);
            
            // 1. Extrair Pensamento
            std::smatch thoughtMatch;
            std::regex thoughtRegex("<thought>([\\s\\S]*?)</thought>");
            if (std::regex_search(response, thoughtMatch, thoughtRegex)) {
                if (callbacks.onThought) callbacks.onThought(thoughtMatch[1].str());
            }

            // 2. Extrair Ação (Tool Call)
            std::smatch toolMatch;
            std::regex toolRegex("```json\\s*([\\s\\S]*?)\\s*```");
            std::string observation = "";
            bool taskComplete = (response.find("TASK COMPLETE") != std::string::npos);

            if (std::regex_search(response, toolMatch, toolRegex)) {
                try {
                    auto toolJson = nlohmann::json::parse(toolMatch[1].str());
                    std::string toolName = toolJson.value("tool", "");
                    nlohmann::json args = toolJson.value("args", nlohmann::json::object());

                    if (callbacks.onAction) callbacks.onAction("Executando: " + toolName);
                    observation = ToolRegistry::instance().dispatch(toolName, args);
                    if (callbacks.onObservation) callbacks.onObservation(observation);
                    
                } catch (const std::exception& e) {
                    observation = "Erro ao processar JSON da ferramenta: " + std::string(e.what());
                }
            }

            // 3. Atualizar Histórico
            history.push_back({"assistant", response});
            if (!observation.empty()) {
                history.push_back({"user", "OBSERVAÇÃO: " + observation});
                currentGoal = "OBSERVAÇÃO: " + observation;
            } else if (!taskComplete) {
                currentGoal = "Continue com a próxima ação objetiva.";
                history.push_back({"user", currentGoal});
            }

            if (callbacks.onMessageChunk) callbacks.onMessageChunk(response);

            if (taskComplete) {
                if (callbacks.onAction) callbacks.onAction("MISSÃO CONCLUÍDA COM SUCESSO.");
                completed = true;
                break;
            }
        }

        if (callbacks.onComplete) callbacks.onComplete(completed && !stopRequested);
    }).detach();
}

std::string Orchestrator::getModeInstructions(const std::string& mode) {
    if (mode == "AGENT") return "FOCO EM EXECUÇÃO. Use ferramentas para investigar o repo, editar arquivos e rodar comandos. Não faça perguntas se puder encontrar a resposta via 'read_file'.";
    if (mode == "DEBUG") return "FOCO EM CORREÇÃO. Analise logs, reproduza o erro e aplique patches. Teste após cada mudança.";
    if (mode == "REVIEW") return "FOCO EM ANÁLISE. Examine o código buscando bugs ou desalinhamentos com o ADR.";
    return "FOCO EM ASSISTÊNCIA TÉCNICA.";
}

std::string Orchestrator::buildSystemPrompt(const std::string& mode) {
    std::string specs = ToolRegistry::instance().getToolSpecs();
    std::string modeInstr = getModeInstructions(mode);
    
    // Tenta carregar contexto adicional se existir
    std::string agentMd = "";
    std::string contextMd = "";
    
    std::ifstream f1(workspaceRoot + "/AGENT.md");
    if (f1) { std::stringstream ss; ss << f1.rdbuf(); agentMd = ss.str(); }
    
    std::ifstream f2(workspaceRoot + "/PROJECT_CONTEXT.md");
    if (f2) { std::stringstream ss; ss << f2.rdbuf(); contextMd = ss.str(); }

    return "## AGENT NATIVO CODEX - PROTOCOLO DE AUTONOMIA\n\n" +
           agentMd + "\n\n" +
           "## CONTEXTO DO PROJETO\n" + contextMd + "\n\n" +
           "## MODO DE OPERAÇÃO: " + mode + "\n" + modeInstr + "\n\n" +
           "## FERRAMENTAS DISPONÍVEIS\n" + specs + "\n" +
           "## REGRAS DE OURO:\n" +
           "1. Pense antes de agir com <thought>...</thought>.\n" +
           "2. Para agir, use OBRIGATORIAMENTE o bloco ```json ... ``` com {\"tool\": \"...\", \"args\": {...}}.\n" +
           "3. PRIORIZE AÇÃO SOBRE EXPLICAÇÃO. Se a meta é 'analisar', comece com 'list_dir'.\n" +
           "4. Se a tarefa estiver pronta, finalize com 'TASK COMPLETE'.";
}

} // namespace agent::core
