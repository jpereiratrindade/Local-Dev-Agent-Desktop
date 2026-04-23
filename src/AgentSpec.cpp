#include "AgentSpec.hpp"

namespace agent::core {

AgentSpec buildAgentSpec(const std::string& profile,
                         const std::string& provider,
                         const std::string& reasoning,
                         const std::string& access) {
    AgentSpec spec;
    spec.role = profile.empty() ? "general" : profile;
    spec.displayName = "Agent";
    spec.responseStyle = "balanced";
    spec.toolProfile = "workspace";

    if (spec.role == "coding") {
        spec.id = "coder";
        spec.displayName = "Coder";
        spec.toolProfile = "coding";
        spec.responseStyle = "concise";
    } else if (spec.role == "review") {
        spec.id = "reviewer";
        spec.displayName = "Reviewer";
        spec.toolProfile = "inspect";
        spec.responseStyle = "critical";
    } else if (spec.role == "analysis") {
        spec.id = "analyst";
        spec.displayName = "Analyst";
        spec.toolProfile = "inspect";
        spec.responseStyle = "structured";
    } else if (spec.role == "research" || spec.role == "research-project") {
        spec.id = "researcher";
        spec.displayName = "Researcher";
        spec.toolProfile = "research";
        spec.responseStyle = "structured";
    } else if (spec.role.rfind("writing-", 0) == 0) {
        spec.id = "writer";
        spec.displayName = "Writer";
        spec.toolProfile = "writing";
        spec.responseStyle = "editorial";
    } else {
        spec.id = "generalist";
        spec.displayName = "Generalist";
        spec.toolProfile = "workspace";
        spec.responseStyle = "balanced";
    }

    if (access == "read-only") {
        spec.toolProfile = "read-only";
    } else if (access == "full-access" && spec.toolProfile == "workspace") {
        spec.toolProfile = "full-access";
    }

    spec.prefersCompactAnswers = (provider.find("LM Studio") != std::string::npos) || reasoning == "low";
    if (spec.responseStyle == "balanced" && spec.prefersCompactAnswers) {
        spec.responseStyle = "concise";
    }
    return spec;
}

} // namespace agent::core
