#include "input.hpp"
#include <android/input.h>
#include <imgui.h>
#include "interface.hpp"

namespace ZmInternal {
    namespace UI {

        // Thread-local storage for input state
        thread_local bool g_touchConsumedByUI = false;

        void InitializeInput() {
            // Initialize any input-related resources
            g_touchConsumedByUI = false;
        }

        void ShutdownInput() {
            // Cleanup input-related resources
        }

        bool ProcessInputEvent(AInputEvent* event) {
            if (!event) {
                return false;
            }

            int32_t eventType = AInputEvent_getType(event);
            int32_t source = AInputEvent_getSource(event);

            // Handle touch events
            if (eventType == AINPUT_EVENT_TYPE_MOTION &&
                (source == AINPUT_SOURCE_TOUCHSCREEN || source == AINPUT_SOURCE_TOUCH_NAVIGATION)) {

                // Get touch coordinates
                float x = AMotionEvent_getX(event, 0);
                float y = AMotionEvent_getY(event, 0);
                int32_t action = AMotionEvent_getAction(event);

                // Convert to ImGui coordinates
                ImGuiIO& io = ImGui::GetIO();

                // Update touch position
                if (action == AMOTION_EVENT_ACTION_DOWN ||
                    action == AMOTION_EVENT_ACTION_POINTER_DOWN ||
                    action == AMOTION_EVENT_ACTION_MOVE ||
                    action == AMOTION_EVENT_ACTION_UP ||
                    action == AMOTION_EVENT_ACTION_POINTER_UP) {

                    io.AddMouseSourceEvent(IMGUI_SOURCE_TOUCH);
                    io.AddMousePosEvent(x, y);

                    // Handle button state
                    bool isDown = (action == AMOTION_EVENT_ACTION_DOWN ||
                                 action == AMOTION_EVENT_ACTION_POINTER_DOWN ||
                                 action == AMOTION_EVENT_ACTION_MOVE);

                    io.AddMouseButtonEvent(0, isDown);
                }

                // Check if touch is within UI bounds (simplified)
                // In a real implementation, we would check against actual menu rectangles
                bool isOverUI = ImGui::IsMouseHoveringAnyWindow();

                if (isOverUI) {
                    g_touchConsumedByUI = true;
                    return true; // Consume the event
                } else {
                    g_touchConsumedByUI = false;
                    return false; // Let the event pass through to the game
                }
            }
            // Handle key events (back button, etc.)
            else if (eventType == AINPUT_EVENT_TYPE_KEY) {
                int32_t keyCode = AKeyEvent_getKeyCode(event);
                int32_t action = AKeyEvent_getAction(event);

                // Handle back button
                if (keyCode == AKEYCODE_BACK && action == AKEY_EVENT_ACTION_DOWN) {
                    // Toggle menu on back button press
                    ToggleMenu();
                    return true; // Consume the event
                }
            }

            return false;
        }

        bool IsTouchConsumedByUI() {
            return g_touchConsumedByUI;
        }

        void SetTouchConsumedByUI(bool consumed) {
            g_touchConsumedByUI = consumed;
        }

    } // namespace UI
} // namespace ZmInternal