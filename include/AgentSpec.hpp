#pragma once

#include <string>

namespace agent::core {

struct AgentSpec {
    std::string id;
    std::string displayName;
    std::string role;
    std::string toolProfile;
    std::string responseStyle;
    bool prefersCompactAnswers = false;
};

AgentSpec buildAgentSpec(const std::string& profile,
                         const std::string& provider,
                         const std::string& reasoning,
                         const std::string& access);

} // namespace agent::core
