#pragma once

#include "AgentSpec.hpp"
#include <string>

namespace agent::core {

struct PromptContext {
    AgentSpec agentSpec;
    std::string provider;
    std::string mode;
    std::string profile;
    std::string reasoning;
    std::string workspaceRoot;
    std::string activeFile;
    std::string projectMap;
    std::string governance;
    std::string approvedRoots;
    std::string approvedDomains;
    std::string skillsSummary;
    std::string distributedContextSummary;
    std::string distributedContextBody;
    std::string modeInstructions;
    std::string toolSpecs;
    bool compactForProvider = false;
};


std::string buildSharedAgentPrompt(const PromptContext& ctx);

} // namespace agent::core
