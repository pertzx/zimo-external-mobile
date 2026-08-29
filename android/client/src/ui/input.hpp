#pragma once

#include <android/input.h>

namespace ZmInternal {
    namespace UI {

        void InitializeInput();
        void ShutdownInput();
        bool ProcessInputEvent(AInputEvent* event);
        bool IsTouchConsumedByUI();
        void SetTouchConsumedByUI(bool consumed);

    } // namespace UI
} // namespace ZmInternal