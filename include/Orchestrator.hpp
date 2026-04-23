#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include "AgentSpec.hpp"
#include "OllamaClient.hpp"
#include "ToolRegistry.hpp"
#include "NativeTools.hpp"

namespace agent::core {

struct MissionConfig {
    AgentSpec agentSpec;
    std::string mode = "MISSION";
    std::string reasoning = "medium";
    std::string access = "workspace-write";
    std::string contextSource = "workspace";
};

class Orchestrator {
public:
    Orchestrator(agent::network::OllamaClient* client, const std::string& workspaceRoot);

    struct MissionCallbacks {
        std::function<void(const std::string&)> onThought;
        std::function<void(const std::string&)> onAction;
        std::function<void(const std::string&)> onObservation;
        std::function<void(const std::string&)> onMessageChunk;
        std::function<void(const agent::network::OllamaStreamStats&)> onStreamStats;
        std::function<void(bool)> onComplete;
        std::function<void(const std::vector<agent::network::Message>&)> onMissionComplete;
        // P0.4: Callback de aprovacao para operacoes destrutivas.
        // Retorna true=aprovado, false=rejeitado. Bloqueia ate resposta do usuario.
        std::function<bool(const std::string& toolName, const nlohmann::json& args)> onApprovalRequired;
    };

    void runMission(const std::vector<agent::network::Message>& contextHistory,
                    const std::string& goal, const MissionConfig& config,
                    int maxSteps, MissionCallbacks callbacks,
                    const agent::network::OllamaOptions& options = agent::network::OllamaOptions());

    void setGovernance(const std::string& gov) { projectGovernance = gov; }
    void setWorkspaceRoot(const std::string& root);
    void stopMission() { stopRequested = true; }

private:
    agent::network::OllamaClient* ollama;
    std::string workspaceRoot;
    std::string projectGovernance;
    std::atomic<bool> stopRequested{false};

    std::string buildSystemPrompt(const MissionConfig& config) const;
    std::string getModeInstructions(const std::string& mode, const std::string& profile) const;
};

} // namespace agent::core
