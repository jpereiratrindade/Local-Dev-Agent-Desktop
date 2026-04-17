#pragma once

#include <string>
#include <vector>

namespace agent::core {
    enum class AccessLevel {
        ReadOnly,
        WorkspaceWrite,
        FullAccess
    };

    enum class ContextSourceMode {
        WorkspaceOnly,
        WorkspaceAndLibrary,
        WorkspaceLibraryAndWeb
    };

    struct RagStats {
        int docCount = 0;
        long long cacheSizeBytes = 0;
        std::vector<std::string> indexedPaths;
    };

    void registerNativeTools(const std::string& workspaceRoot = ".");
    void setNativeToolAccessLevel(AccessLevel level);
    AccessLevel getNativeToolAccessLevel();
    std::string getNativeToolAccessLevelName();
    void setNativeToolContextSourceMode(ContextSourceMode mode);
    ContextSourceMode getNativeToolContextSourceMode();
    std::string getNativeToolContextSourceModeName();
    void setNativeToolApprovedRoots(const std::vector<std::string>& roots);
    std::vector<std::string> getNativeToolApprovedRoots();
    void setNativeToolApprovedDomains(const std::vector<std::string>& domains);
    std::vector<std::string> getNativeToolApprovedDomains();
    std::string getNativeToolUsageSummary();
    void clearNativeToolUsageSummary();
    
    RagStats getNativeToolRagStats();
    std::string ingest_file_direct(const std::string& path); 
}
