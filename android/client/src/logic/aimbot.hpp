#pragma once

#include <android/shared/IpcProtocol.h>
#include <imgui.h>

namespace ZmInternal {
    namespace Logic {
        void UpdateAimbot(const IpcMsgSnapshot* snapshot, const IpcMsgConfig* config);
        bool IsAimbotActive();
    }
}