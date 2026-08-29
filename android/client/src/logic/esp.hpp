#pragma once

#include <android/shared/IpcProtocol.h>
#include <imgui.h>

namespace ZmInternal {
    namespace Logic {
        void RenderESP(const IpcMsgSnapshot* snapshot, ImDrawList* draw_list);
    }
}