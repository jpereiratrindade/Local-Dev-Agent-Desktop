#pragma once

#include <string>
#include <vector>

namespace agent::core {

enum class EvidenceType {
    FileExists,
    FileContains,
    DirectoryContains,
    CommandSucceeds,
    Unknown
};

struct EvidenceItem {
    EvidenceType type = EvidenceType::Unknown;
    std::string path;
    std::string text;
    std::string command;
    bool required = true;
    bool satisfied = false;
    std::string lastObservation;
};

struct EvidenceModel {
    std::string goal;
    std::vector<EvidenceItem> items;
};

struct ExecutionAttempt {
    std::string tool;
    std::string target;
    std::string observation;
    bool changedState = false;
    bool improvedEvidence = false;
};

EvidenceModel parseEvidenceModelFromText(const std::string& text, const std::string& fallbackGoal = "");
void refreshEvidence(EvidenceModel& model, const std::string& workspaceRoot);
bool isSatisfied(const EvidenceModel& model);
bool isWeakEvidence(const EvidenceModel& model);
std::string summarizeEvidence(const EvidenceModel& model);
std::string evidenceTypeName(EvidenceType type);

} // namespace agent::core
