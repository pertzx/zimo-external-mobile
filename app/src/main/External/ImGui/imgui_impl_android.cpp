// dear imgui: Platform Backend for Android native app
// This needs to be used along with Renderer Backend (e.g. ImGui_ImplOpenGL3)

#include "imgui.h"
#include "imgui_impl_android.h"
#include <android/native_window.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <time.h>
#include <map>

// Android data
static double                                   g_Time = 0.0;
static ANativeWindow*                           g_Window = nullptr;
static std::map<int32_t, bool>                  g_KeyPressed;
static int32_t                                  g_LastKey = 0;
static bool                                     g_WantTextInput = false;

// Map Android keycodes to ImGui keys
static ImGuiKey AndroidKeycodeToImGuiKey(int32_t keycode)
{
    switch (keycode)
    {
        case AKEYCODE_TAB: return ImGuiKey_Tab;
        case AKEYCODE_DPAD_LEFT: return ImGuiKey_LeftArrow;
        case AKEYCODE_DPAD_RIGHT: return ImGuiKey_RightArrow;
        case AKEYCODE_DPAD_UP: return ImGuiKey_UpArrow;
        case AKEYCODE_DPAD_DOWN: return ImGuiKey_DownArrow;
        case AKEYCODE_PAGE_UP: return ImGuiKey_PageUp;
        case AKEYCODE_PAGE_DOWN: return ImGuiKey_PageDown;
        case AKEYCODE_MOVE_HOME: return ImGuiKey_Home;
        case AKEYCODE_MOVE_END: return ImGuiKey_End;
        case AKEYCODE_INSERT: return ImGuiKey_Insert;
        case AKEYCODE_FORWARD_DEL: return ImGuiKey_Delete;
        case AKEYCODE_DEL: return ImGuiKey_Backspace;
        case AKEYCODE_SPACE: return ImGuiKey_Space;
        case AKEYCODE_ENTER: return ImGuiKey_Enter;
        case AKEYCODE_ESCAPE: return ImGuiKey_Escape;
        case AKEYCODE_APOSTROPHE: return ImGuiKey_Apostrophe;
        case AKEYCODE_COMMA: return ImGuiKey_Comma;
        case AKEYCODE_MINUS: return ImGuiKey_Minus;
        case AKEYCODE_PERIOD: return ImGuiKey_Period;
        case AKEYCODE_SLASH: return ImGuiKey_Slash;
        case AKEYCODE_SEMICOLON: return ImGuiKey_Semicolon;
        case AKEYCODE_EQUALS: return ImGuiKey_Equal;
        case AKEYCODE_LEFT_BRACKET: return ImGuiKey_LeftBracket;
        case AKEYCODE_BACKSLASH: return ImGuiKey_Backslash;
        case AKEYCODE_RIGHT_BRACKET: return ImGuiKey_RightBracket;
        case AKEYCODE_GRAVE: return ImGuiKey_GraveAccent;
        case AKEYCODE_0: return ImGuiKey_0;
        case AKEYCODE_1: return ImGuiKey_1;
        case AKEYCODE_2: return ImGuiKey_2;
        case AKEYCODE_3: return ImGuiKey_3;
        case AKEYCODE_4: return ImGuiKey_4;
        case AKEYCODE_5: return ImGuiKey_5;
        case AKEYCODE_6: return ImGuiKey_6;
        case AKEYCODE_7: return ImGuiKey_7;
        case AKEYCODE_8: return ImGuiKey_8;
        case AKEYCODE_9: return ImGuiKey_9;
        case AKEYCODE_A: return ImGuiKey_A;
        case AKEYCODE_B: return ImGuiKey_B;
        case AKEYCODE_C: return ImGuiKey_C;
        case AKEYCODE_D: return ImGuiKey_D;
        case AKEYCODE_E: return ImGuiKey_E;
        case AKEYCODE_F: return ImGuiKey_F;
        case AKEYCODE_G: return ImGuiKey_G;
        case AKEYCODE_H: return ImGuiKey_H;
        case AKEYCODE_I: return ImGuiKey_I;
        case AKEYCODE_J: return ImGuiKey_J;
        case AKEYCODE_K: return ImGuiKey_K;
        case AKEYCODE_L: return ImGuiKey_L;
        case AKEYCODE_M: return ImGuiKey_M;
        case AKEYCODE_N: return ImGuiKey_N;
        case AKEYCODE_O: return ImGuiKey_O;
        case AKEYCODE_P: return ImGuiKey_P;
        case AKEYCODE_Q: return ImGuiKey_Q;
        case AKEYCODE_R: return ImGuiKey_R;
        case AKEYCODE_S: return ImGuiKey_S;
        case AKEYCODE_T: return ImGuiKey_T;
        case AKEYCODE_U: return ImGuiKey_U;
        case AKEYCODE_V: return ImGuiKey_V;
        case AKEYCODE_W: return ImGuiKey_W;
        case AKEYCODE_X: return ImGuiKey_X;
        case AKEYCODE_Y: return ImGuiKey_Y;
        case AKEYCODE_Z: return ImGuiKey_Z;
        case AKEYCODE_F1: return ImGuiKey_F1;
        case AKEYCODE_F2: return ImGuiKey_F2;
        case AKEYCODE_F3: return ImGuiKey_F3;
        case AKEYCODE_F4: return ImGuiKey_F4;
        case AKEYCODE_F5: return ImGuiKey_F5;
        case AKEYCODE_F6: return ImGuiKey_F6;
        case AKEYCODE_F7: return ImGuiKey_F7;
        case AKEYCODE_F8: return ImGuiKey_F8;
        case AKEYCODE_F9: return ImGuiKey_F9;
        case AKEYCODE_F10: return ImGuiKey_F10;
        case AKEYCODE_F11: return ImGuiKey_F11;
        case AKEYCODE_F12: return ImGuiKey_F12;
        case AKEYCODE_CTRL_LEFT: return ImGuiKey_LeftCtrl;
        case AKEYCODE_CTRL_RIGHT: return ImGuiKey_RightCtrl;
        case AKEYCODE_SHIFT_LEFT: return ImGuiKey_LeftShift;
        case AKEYCODE_SHIFT_RIGHT: return ImGuiKey_RightShift;
        case AKEYCODE_ALT_LEFT: return ImGuiKey_LeftAlt;
        case AKEYCODE_ALT_RIGHT: return ImGuiKey_RightAlt;
        case AKEYCODE_META_LEFT: return ImGuiKey_LeftSuper;
        case AKEYCODE_META_RIGHT: return ImGuiKey_RightSuper;
        case AKEYCODE_CAPS_LOCK: return ImGuiKey_CapsLock;
        case AKEYCODE_SCROLL_LOCK: return ImGuiKey_ScrollLock;
        case AKEYCODE_NUM_LOCK: return ImGuiKey_NumLock;
        case AKEYCODE_SYSRQ: return ImGuiKey_PrintScreen;
        case AKEYCODE_BREAK: return ImGuiKey_Pause;
        case AKEYCODE_NUMPAD_0: return ImGuiKey_Keypad0;
        case AKEYCODE_NUMPAD_1: return ImGuiKey_Keypad1;
        case AKEYCODE_NUMPAD_2: return ImGuiKey_Keypad2;
        case AKEYCODE_NUMPAD_3: return ImGuiKey_Keypad3;
        case AKEYCODE_NUMPAD_4: return ImGuiKey_Keypad4;
        case AKEYCODE_NUMPAD_5: return ImGuiKey_Keypad5;
        case AKEYCODE_NUMPAD_6: return ImGuiKey_Keypad6;
        case AKEYCODE_NUMPAD_7: return ImGuiKey_Keypad7;
        case AKEYCODE_NUMPAD_8: return ImGuiKey_Keypad8;
        case AKEYCODE_NUMPAD_9: return ImGuiKey_Keypad9;
        case AKEYCODE_NUMPAD_DOT: return ImGuiKey_KeypadDecimal;
        case AKEYCODE_NUMPAD_DIVIDE: return ImGuiKey_KeypadDivide;
        case AKEYCODE_NUMPAD_MULTIPLY: return ImGuiKey_KeypadMultiply;
        case AKEYCODE_NUMPAD_SUBTRACT: return ImGuiKey_KeypadSubtract;
        case AKEYCODE_NUMPAD_ADD: return ImGuiKey_KeypadAdd;
        case AKEYCODE_NUMPAD_ENTER: return ImGuiKey_KeypadEnter;
        case AKEYCODE_NUMPAD_EQUALS: return ImGuiKey_KeypadEqual;
        default: return ImGuiKey_None;
    }
}

bool ImGui_ImplAndroid_Init(ANativeWindow* window)
{
    IM_ASSERT(g_Window == nullptr && "Already initialized");
    g_Window = window;
    g_Time = 0.0;

    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "imgui_impl_android";

    // Setup display size
    int32_t width = ANativeWindow_getWidth(window);
    int32_t height = ANativeWindow_getHeight(window);
    io.DisplaySize = ImVec2((float)width, (float)height);

    return true;
}

void ImGui_ImplAndroid_Shutdown()
{
    g_Window = nullptr;
    g_KeyPressed.clear();
}

void ImGui_ImplAndroid_NewFrame()
{
    ImGuiIO& io = ImGui::GetIO();

    // Update display size
    if (g_Window)
    {
        int32_t width = ANativeWindow_getWidth(g_Window);
        int32_t height = ANativeWindow_getHeight(g_Window);
        io.DisplaySize = ImVec2((float)width, (float)height);
    }

    // Setup time step
    struct timespec current_timespec;
    clock_gettime(CLOCK_MONOTONIC, &current_timespec);
    double current_time = (double)(current_timespec.tv_sec) + (double)(current_timespec.tv_nsec) / 1000000000.0;
    io.DeltaTime = g_Time > 0.0 ? (float)(current_time - g_Time) : 1.0f / 60.0f;
    g_Time = current_time;

    // Update key states
    for (auto& key : g_KeyPressed)
    {
        ImGuiKey imgui_key = AndroidKeycodeToImGuiKey(key.first);
        if (imgui_key != ImGuiKey_None)
        {
            io.AddKeyEvent(imgui_key, key.second);
        }
    }
}

int32_t ImGui_ImplAndroid_HandleInputEvent(const AInputEvent* input_event)
{
    ImGuiIO& io = ImGui::GetIO();
    int32_t event_type = AInputEvent_getType(input_event);

    if (event_type == AINPUT_EVENT_TYPE_MOTION)
    {
        int32_t action = AMotionEvent_getAction(input_event);
        int32_t pointer_index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        action &= AMOTION_EVENT_ACTION_MASK;

        switch (action)
        {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_UP:
            {
                float x = AMotionEvent_getX(input_event, pointer_index);
                float y = AMotionEvent_getY(input_event, pointer_index);
                io.AddMousePosEvent(x, y);
                io.AddMouseButtonEvent(0, action == AMOTION_EVENT_ACTION_DOWN);
                return 1;
            }
            case AMOTION_EVENT_ACTION_MOVE:
            {
                float x = AMotionEvent_getX(input_event, pointer_index);
                float y = AMotionEvent_getY(input_event, pointer_index);
                io.AddMousePosEvent(x, y);
                return 1;
            }
        }
    }
    else if (event_type == AINPUT_EVENT_TYPE_KEY)
    {
        int32_t keycode = AKeyEvent_getKeyCode(input_event);
        int32_t action = AKeyEvent_getAction(input_event);
        int32_t meta_state = AKeyEvent_getMetaState(input_event);

        bool down = (action == AKEY_EVENT_ACTION_DOWN);
        g_KeyPressed[keycode] = down;

        ImGuiKey imgui_key = AndroidKeycodeToImGuiKey(keycode);
        if (imgui_key != ImGuiKey_None)
        {
            io.AddKeyEvent(imgui_key, down);
        }

        // Handle volume up/down as menu toggle
        if (keycode == AKEYCODE_VOLUME_UP && down)
        {
            // Will be handled by AndroidInput layer
        }

        return 1;
    }

    return 0;
}
