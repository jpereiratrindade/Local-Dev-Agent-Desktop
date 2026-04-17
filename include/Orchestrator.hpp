#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include "OllamaClient.hpp"
#include "ToolRegistry.hpp"
#include "NativeTools.hpp"

namespace agent::core {

class Orchestrator {
public:
    Orchestrator(agent::network::OllamaClient* client, const std::string& workspaceRoot);

    struct MissionCallbacks {
        std::function<void(const std::string&)> onThought;
        std::function<void(const std::string&)> onAction;
        std::function<void(const std::string&)> onObservation;
        std::function<void(const std::string&)> onMessageChunk;
        std::function<void(bool)> onComplete;
    };

    void runMission(const std::string& goal, const std::string& mode, 
                    int maxSteps, MissionCallbacks callbacks,
                    const agent::network::OllamaOptions& options = agent::network::OllamaOptions());

    void setGovernance(const std::string& gov) { projectGovernance = gov; }
    void stopMission() { stopRequested = true; }

private:
    agent::network::OllamaClient* ollama;
    std::string workspaceRoot;
    std::string projectGovernance;
    std::vector<agent::network::Message> history;
    std::atomic<bool> stopRequested{false};

    std::string buildSystemPrompt(const std::string& mode, const std::string& profile, const std::string& reasoning);
    std::string getModeInstructions(const std::string& mode, const std::string& profile);
};

} // namespace agent::core
