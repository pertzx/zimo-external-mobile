#pragma once

#include <android/shared/IpcProtocol.h>

namespace ZmInternal {
    namespace Logic {
        void UpdateSilentAim(const IpcMsgSnapshot* snapshot, IpcMsgConfig* config);
        bool IsSilentAimEnabled(const IpcMsgConfig* config);
    }
}