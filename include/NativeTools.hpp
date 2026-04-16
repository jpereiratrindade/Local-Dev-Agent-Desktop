#pragma once

#include <string>

namespace agent::core {
    enum class AccessLevel {
        ReadOnly,
        WorkspaceWrite,
        FullAccess
    };

    void registerNativeTools(const std::string& workspaceRoot = ".");
    void setNativeToolAccessLevel(AccessLevel level);
    AccessLevel getNativeToolAccessLevel();
    std::string getNativeToolAccessLevelName();
}
