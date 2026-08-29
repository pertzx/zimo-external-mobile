#pragma once

#include <imgui.h>

namespace ZmInternal {
    namespace UI {

        void RenderMenu();
        bool IsMenuOpen();
        void SetMenuOpen(bool open);
        void ToggleMenu();

    } // namespace UI
} // namespace ZmInternal