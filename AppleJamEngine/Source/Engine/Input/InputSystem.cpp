#include "Engine/Input/InputSystem.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>

#ifndef VK_GAMEPAD_A
#define VK_GAMEPAD_A 0xC3
#define VK_GAMEPAD_B 0xC4
#define VK_GAMEPAD_X 0xC5
#define VK_GAMEPAD_Y 0xC6
#define VK_GAMEPAD_RIGHT_SHOULDER 0xC7
#define VK_GAMEPAD_LEFT_SHOULDER 0xC8
#define VK_GAMEPAD_LEFT_TRIGGER 0xC9
#define VK_GAMEPAD_RIGHT_TRIGGER 0xCA
#define VK_GAMEPAD_DPAD_UP 0xCB
#define VK_GAMEPAD_DPAD_DOWN 0xCC
#define VK_GAMEPAD_DPAD_LEFT 0xCD
#define VK_GAMEPAD_DPAD_RIGHT 0xCE
#define VK_GAMEPAD_MENU 0xCF
#define VK_GAMEPAD_VIEW 0xD0
#define VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON 0xD1
#define VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON 0xD2
#define VK_GAMEPAD_LEFT_THUMBSTICK_UP 0xD3
#define VK_GAMEPAD_LEFT_THUMBSTICK_DOWN 0xD4
#define VK_GAMEPAD_LEFT_THUMBSTICK_RIGHT 0xD5
#define VK_GAMEPAD_LEFT_THUMBSTICK_LEFT 0xD6
#define VK_GAMEPAD_RIGHT_THUMBSTICK_UP 0xD7
#define VK_GAMEPAD_RIGHT_THUMBSTICK_DOWN 0xD8
#define VK_GAMEPAD_RIGHT_THUMBSTICK_RIGHT 0xD9
#define VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT 0xDA
#endif

namespace
{
    using FXInputGetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

    bool GHasTriedLoadXInput = false;
    HMODULE GXInputModule = nullptr;
    FXInputGetState GXInputGetState = nullptr;

    bool EnsureXInputLoaded()
    {
        if (GHasTriedLoadXInput)
        {
            return GXInputGetState != nullptr;
        }

        GHasTriedLoadXInput = true;

        const char* DllNames[] = {
            "xinput1_4.dll",
            "xinput1_3.dll",
            "xinput9_1_0.dll"
        };

        for (const char* DllName : DllNames)
        {
            GXInputModule = ::LoadLibraryA(DllName);
            if (!GXInputModule)
            {
                continue;
            }

            GXInputGetState = reinterpret_cast<FXInputGetState>(::GetProcAddress(GXInputModule, "XInputGetState"));
            if (GXInputGetState)
            {
                return true;
            }

            ::FreeLibrary(GXInputModule);
            GXInputModule = nullptr;
        }

        return false;
    }

    float NormalizeSignedGamepadAxis(SHORT Value)
    {
        const float Denominator = Value < 0 ? 32768.0f : 32767.0f;
        return std::clamp(static_cast<float>(Value) / Denominator, -1.0f, 1.0f);
    }

    float NormalizeTriggerAxis(BYTE Value)
    {
        return std::clamp(static_cast<float>(Value) / 255.0f, 0.0f, 1.0f);
    }
}

void InputSystem::Tick()
{
    // 윈도우 포커스가 없으면 모든 입력 상태 해제
    bWindowFocused = !OwnerHWnd || GetForegroundWindow() == OwnerHWnd;
    if (!bWindowFocused)
    {
        ResetAllKeyStates();
        ResetTransientState();
        UpdateCurrentSnapshot();
        return;
    }

    for (int i = 0; i < 256; ++i)
    {
        PrevStates[i] = CurrentStates[i];
        CurrentStates[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    }
    UpdateGamepadState();

    bLeftDragJustStarted = false;
    bRightDragJustStarted = false;
    bLeftDragJustEnded = false;
    bRightDragJustEnded = false;

    PrevScrollDelta = ScrollDelta;
    ScrollDelta = 0;

    PrevMousePos = MousePos;
    GetCursorPos(&MousePos);
    FrameMouseDeltaX = MousePos.x - PrevMousePos.x;
    FrameMouseDeltaY = MousePos.y - PrevMousePos.y;
    if (bUseRawMouse)
    {
        FrameMouseDeltaX = RawMouseDeltaAccumX;
        FrameMouseDeltaY = RawMouseDeltaAccumY;
    }
    RawMouseDeltaAccumX = 0;
    RawMouseDeltaAccumY = 0;

    if (GetKeyDown(VK_LBUTTON))
    {
        bLeftDragCandidate = true;
        LeftMouseDownPos = MousePos;
    }
    if (GetKeyDown(VK_RBUTTON))
    {
        bRightDragCandidate = true;
        RightMouseDownPos = MousePos;
    }

    // Left drag
    if (!bLeftDragging && IsDraggingLeft())
    {
        FilterDragThreshold(bLeftDragCandidate, bLeftDragging, bLeftDragJustStarted,
            LeftMouseDownPos, LeftDragStartPos);
    }
    else if (GetKeyUp(VK_LBUTTON))
    {
        if (bLeftDragging) bLeftDragJustEnded = true;
        bLeftDragging = false;
        bLeftDragCandidate = false;
    }

    // Right drag
    if (!bRightDragging && IsDraggingRight())
    {
        FilterDragThreshold(bRightDragCandidate, bRightDragging, bRightDragJustStarted,
            RightMouseDownPos, RightDragStartPos);
    }
    else if (GetKeyUp(VK_RBUTTON))
    {
        if (bRightDragging) bRightDragJustEnded = true;
        bRightDragging = false;
        bRightDragCandidate = false;
    }

    UpdateCurrentSnapshot();
}

FInputSystemSnapshot InputSystem::TickAndMakeSnapshot()
{
    Tick();
    return MakeSnapshot();
}

FInputSystemSnapshot InputSystem::MakeSnapshot() const
{
    return CurrentSnapshot;
}

void InputSystem::RefreshSnapshot()
{
    UpdateCurrentSnapshot();
}

void InputSystem::SetUseRawMouse(bool bEnable)
{
    if (bUseRawMouse == bEnable)
    {
        return;
    }

    bUseRawMouse = bEnable;
    ResetMouseDelta();
    UpdateCurrentSnapshot();
}

void InputSystem::AddRawMouseDelta(int DeltaX, int DeltaY)
{
    RawMouseDeltaAccumX += DeltaX;
    RawMouseDeltaAccumY += DeltaY;
}

void InputSystem::ResetTransientState()
{
    bLeftDragJustStarted = false;
    bRightDragJustStarted = false;
    bLeftDragJustEnded = false;
    bRightDragJustEnded = false;
    ResetDragState();
    ResetMouseDelta();
    ResetWheelDelta();
    UpdateCurrentSnapshot();
}

void InputSystem::ResetAllKeyStates()
{
    for (int VK = 0; VK < 256; ++VK)
    {
        CurrentStates[VK] = false;
        PrevStates[VK] = false;
    }
    ClearGamepadState();
    UpdateCurrentSnapshot();
}

void InputSystem::ResetMouseDelta()
{
    GetCursorPos(&MousePos);
    PrevMousePos = MousePos;
    FrameMouseDeltaX = 0;
    FrameMouseDeltaY = 0;
    RawMouseDeltaAccumX = 0;
    RawMouseDeltaAccumY = 0;
    UpdateCurrentSnapshot();
}

void InputSystem::ResetWheelDelta()
{
    ScrollDelta = 0;
    PrevScrollDelta = 0;
    UpdateCurrentSnapshot();
}

void InputSystem::ResetCaptureStateForPIEEnd()
{
    SetUseRawMouse(false);
    ResetAllKeyStates();
    ResetTransientState();
    GuiState.bUsingMouse = false;
    GuiState.bUsingKeyboard = false;
    GuiState.bUsingTextInput = false;
    UpdateCurrentSnapshot();
}

void InputSystem::UpdateCurrentSnapshot()
{
    FInputSystemSnapshot Snapshot{};
    for (int VK = 0; VK < 256; ++VK)
    {
        Snapshot.KeyDown[VK] = CurrentStates[VK];
        Snapshot.KeyPressed[VK] = CurrentStates[VK] && !PrevStates[VK];
        Snapshot.KeyReleased[VK] = !CurrentStates[VK] && PrevStates[VK];
    }

    Snapshot.bLeftMouseDown = Snapshot.KeyDown[VK_LBUTTON];
    Snapshot.bLeftMousePressed = Snapshot.KeyPressed[VK_LBUTTON];
    Snapshot.bLeftMouseReleased = Snapshot.KeyReleased[VK_LBUTTON];
    Snapshot.bRightMouseDown = Snapshot.KeyDown[VK_RBUTTON];
    Snapshot.bRightMousePressed = Snapshot.KeyPressed[VK_RBUTTON];
    Snapshot.bRightMouseReleased = Snapshot.KeyReleased[VK_RBUTTON];
    Snapshot.bMiddleMouseDown = Snapshot.KeyDown[VK_MBUTTON];
    Snapshot.bMiddleMousePressed = Snapshot.KeyPressed[VK_MBUTTON];
    Snapshot.bMiddleMouseReleased = Snapshot.KeyReleased[VK_MBUTTON];
    Snapshot.bXButton1Down = Snapshot.KeyDown[VK_XBUTTON1];
    Snapshot.bXButton1Pressed = Snapshot.KeyPressed[VK_XBUTTON1];
    Snapshot.bXButton1Released = Snapshot.KeyReleased[VK_XBUTTON1];
    Snapshot.bXButton2Down = Snapshot.KeyDown[VK_XBUTTON2];
    Snapshot.bXButton2Pressed = Snapshot.KeyPressed[VK_XBUTTON2];
    Snapshot.bXButton2Released = Snapshot.KeyReleased[VK_XBUTTON2];

    Snapshot.MousePos = MousePos;
    Snapshot.MouseDeltaX = FrameMouseDeltaX;
    Snapshot.MouseDeltaY = FrameMouseDeltaY;
    Snapshot.ScrollDelta = PrevScrollDelta;

    Snapshot.bLeftDragStarted = bLeftDragJustStarted;
    Snapshot.bLeftDragging = bLeftDragging;
    Snapshot.bLeftDragEnded = bLeftDragJustEnded;
    Snapshot.LeftDragVector = GetLeftDragVector();

    Snapshot.bRightDragStarted = bRightDragJustStarted;
    Snapshot.bRightDragging = bRightDragging;
    Snapshot.bRightDragEnded = bRightDragJustEnded;
    Snapshot.RightDragVector = GetRightDragVector();

    Snapshot.bUsingRawMouse = bUseRawMouse;
    Snapshot.bGuiUsingMouse = GuiState.bUsingMouse;
    Snapshot.bGuiUsingKeyboard = GuiState.bUsingKeyboard;
    Snapshot.bGuiUsingTextInput = GuiState.bUsingTextInput;
    Snapshot.bWindowFocused = bWindowFocused;
    Snapshot.bGamepadConnected = bGamepadConnected;
    Snapshot.GamepadLeftStickX = GamepadLeftStickX;
    Snapshot.GamepadLeftStickY = GamepadLeftStickY;
    Snapshot.GamepadRightStickX = GamepadRightStickX;
    Snapshot.GamepadRightStickY = GamepadRightStickY;
    Snapshot.GamepadLeftTrigger = GamepadLeftTrigger;
    Snapshot.GamepadRightTrigger = GamepadRightTrigger;
    CurrentSnapshot = Snapshot;
}

void InputSystem::UpdateGamepadState()
{
    ClearGamepadState();

    if (!EnsureXInputLoaded())
    {
        return;
    }

    XINPUT_STATE State{};
    if (GXInputGetState(0, &State) != ERROR_SUCCESS)
    {
        return;
    }

    const XINPUT_GAMEPAD& Pad = State.Gamepad;
    bGamepadConnected = true;
    GamepadLeftStickX = NormalizeSignedGamepadAxis(Pad.sThumbLX);
    GamepadLeftStickY = NormalizeSignedGamepadAxis(Pad.sThumbLY);
    GamepadRightStickX = NormalizeSignedGamepadAxis(Pad.sThumbRX);
    GamepadRightStickY = NormalizeSignedGamepadAxis(Pad.sThumbRY);
    GamepadLeftTrigger = NormalizeTriggerAxis(Pad.bLeftTrigger);
    GamepadRightTrigger = NormalizeTriggerAxis(Pad.bRightTrigger);

    const WORD Buttons = Pad.wButtons;
    SetGamepadButtonState(VK_GAMEPAD_A, (Buttons & XINPUT_GAMEPAD_A) != 0);
    SetGamepadButtonState(VK_GAMEPAD_B, (Buttons & XINPUT_GAMEPAD_B) != 0);
    SetGamepadButtonState(VK_GAMEPAD_X, (Buttons & XINPUT_GAMEPAD_X) != 0);
    SetGamepadButtonState(VK_GAMEPAD_Y, (Buttons & XINPUT_GAMEPAD_Y) != 0);
    SetGamepadButtonState(VK_GAMEPAD_LEFT_SHOULDER, (Buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);
    SetGamepadButtonState(VK_GAMEPAD_RIGHT_SHOULDER, (Buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
    SetGamepadButtonState(VK_GAMEPAD_DPAD_UP, (Buttons & XINPUT_GAMEPAD_DPAD_UP) != 0);
    SetGamepadButtonState(VK_GAMEPAD_DPAD_DOWN, (Buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
    SetGamepadButtonState(VK_GAMEPAD_DPAD_LEFT, (Buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
    SetGamepadButtonState(VK_GAMEPAD_DPAD_RIGHT, (Buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0);
    SetGamepadButtonState(VK_GAMEPAD_MENU, (Buttons & XINPUT_GAMEPAD_START) != 0);
    SetGamepadButtonState(VK_GAMEPAD_VIEW, (Buttons & XINPUT_GAMEPAD_BACK) != 0);
    SetGamepadButtonState(VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON, (Buttons & XINPUT_GAMEPAD_LEFT_THUMB) != 0);
    SetGamepadButtonState(VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON, (Buttons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);
    SetGamepadButtonState(VK_GAMEPAD_LEFT_TRIGGER, Pad.bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    SetGamepadButtonState(VK_GAMEPAD_RIGHT_TRIGGER, Pad.bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD);

    constexpr float LeftStickDirectionThreshold = static_cast<float>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) / 32767.0f;
    constexpr float RightStickDirectionThreshold = static_cast<float>(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) / 32767.0f;
    SetGamepadButtonState(VK_GAMEPAD_LEFT_THUMBSTICK_UP, GamepadLeftStickY > LeftStickDirectionThreshold);
    SetGamepadButtonState(VK_GAMEPAD_LEFT_THUMBSTICK_DOWN, GamepadLeftStickY < -LeftStickDirectionThreshold);
    SetGamepadButtonState(VK_GAMEPAD_LEFT_THUMBSTICK_RIGHT, GamepadLeftStickX > LeftStickDirectionThreshold);
    SetGamepadButtonState(VK_GAMEPAD_LEFT_THUMBSTICK_LEFT, GamepadLeftStickX < -LeftStickDirectionThreshold);
    SetGamepadButtonState(VK_GAMEPAD_RIGHT_THUMBSTICK_UP, GamepadRightStickY > RightStickDirectionThreshold);
    SetGamepadButtonState(VK_GAMEPAD_RIGHT_THUMBSTICK_DOWN, GamepadRightStickY < -RightStickDirectionThreshold);
    SetGamepadButtonState(VK_GAMEPAD_RIGHT_THUMBSTICK_RIGHT, GamepadRightStickX > RightStickDirectionThreshold);
    SetGamepadButtonState(VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT, GamepadRightStickX < -RightStickDirectionThreshold);
}

void InputSystem::ClearGamepadState()
{
    bGamepadConnected = false;
    GamepadLeftStickX = 0.0f;
    GamepadLeftStickY = 0.0f;
    GamepadRightStickX = 0.0f;
    GamepadRightStickY = 0.0f;
    GamepadLeftTrigger = 0.0f;
    GamepadRightTrigger = 0.0f;

    for (int VK = VK_GAMEPAD_A; VK <= VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT; ++VK)
    {
        SetGamepadButtonState(VK, false);
    }
}

void InputSystem::SetGamepadButtonState(int VK, bool bDown)
{
    if (VK >= 0 && VK < 256)
    {
        CurrentStates[VK] = bDown;
    }
}

void InputSystem::ResetDragState()
{
    bLeftDragCandidate = false;
    bRightDragCandidate = false;
    bLeftDragging = false;
    bRightDragging = false;
    bLeftDragJustStarted = false;
    bRightDragJustStarted = false;
    bLeftDragJustEnded = false;
    bRightDragJustEnded = false;
    LeftDragStartPos = MousePos;
    LeftMouseDownPos = MousePos;
    RightDragStartPos = MousePos;
    RightMouseDownPos = MousePos;
}

void InputSystem::FilterDragThreshold(
    bool& bCandidate, bool& bDragging, bool& bJustStarted,
    const POINT& MouseDownPos, POINT& DragStartPos)
{
    if (bCandidate && !bDragging)
    {
        int DX = MousePos.x - MouseDownPos.x;
        int DY = MousePos.y - MouseDownPos.y;
        int DistSq = DX * DX + DY * DY;

        if (DistSq >= DRAG_THRESHOLD * DRAG_THRESHOLD)
        {
            bJustStarted = true;
            bDragging = true;
            DragStartPos = MouseDownPos;
        }
    }
}

POINT InputSystem::GetLeftDragVector() const
{
    POINT V;
    V.x = MousePos.x - LeftDragStartPos.x;
    V.y = MousePos.y - LeftDragStartPos.y;
    return V;
}

POINT InputSystem::GetRightDragVector() const
{
    POINT V;
    V.x = MousePos.x - RightDragStartPos.x;
    V.y = MousePos.y - RightDragStartPos.y;
    return V;
}

float InputSystem::GetLeftDragDistance() const
{
    POINT V = GetLeftDragVector();
    return std::sqrt((float)(V.x * V.x + V.y * V.y));
}

float InputSystem::GetRightDragDistance() const
{
    POINT V = GetRightDragVector();
    return std::sqrt((float)(V.x * V.x + V.y * V.y));
}
