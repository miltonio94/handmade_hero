// -*- lsst-c++ -*-

/*
** HUGE thanks to Casey Muratori for doing the handmade heroe series
** Most of this code is thanks to him
*/

/*
** TODO(MILTON) THIS IS NOT THE ACTUAL PLAT LAYER
** - Saved game locations
** - Getting a handle to our own executable file
** - Asset loading path
** - Threading (launch a thread)
** - Raw input (support for multiple keyboards)
** - Sleep/timeBeginPeriod
** - ClipCursor() (for multimonitor suppor)
** - Fullscreen support
** - WM_SETCURSOR
** - QueryCancelAutoplay
** - WM_ACTIVATEAPP (for when we are not the active application)
** - Blit speed improvements (BitBlt)
** - Hardware acceleration (OpenGl or Direct3D or BOTH???)
** - GetKeyboardLayout (for French keyboards, international WASD support)
*/
#include <math.h>
#include <stdint.h>

#include "HandmadeDef.h"

#include "handmade.h"
#include "handmade.cpp"

#include <dsound.h>
#include <malloc.h>
#include <stdio.h>
#include <windows.h>
#include <winnt.h>
#include <xinput.h>

#include "win32_handmade.h"

global_variable bool32                 GlobalRunning;
global_variable LPDIRECTSOUNDBUFFER    GlobalSecondaryBuffer;
global_variable win32_offscreen_buffer GlobalBackbuffer;
global_variable int64                  GlobalPerfCountFrequency;

//////////////////////////
// NOTE: XInputGetState //
//////////////////////////
#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub)
{
    return ERROR_DEVICE_NOT_CONNECTED;
}
global_variable x_input_get_state *XInputGetState_ = XInputGetStateStub;
#define XInputGetState XInputGetState_

//////////////////////////
// NOTE: XInputSetState //
//////////////////////////
#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub)
{
    return ERROR_DEVICE_NOT_CONNECTED;
}
global_variable x_input_set_state *XInputSetState_ = XInputSetStateStub;
#define XInputSetState XInputSetState_

#define DIRECT_SOUND_CREATE(name) \
    HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(direct_sound_create);

internal debug_read_file_result
DEBUGPlatformReadEntireFile(char *FileName)
{
    debug_read_file_result Result = {};

    HANDLE FileHandle =
        CreateFileA(FileName, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (FileHandle != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER FileSize;
        if (GetFileSizeEx(FileHandle, &FileSize)) {
            Assert(FileSize.QuadPart <= 0xffffffff);
            Result.ContentsSize = SafeTruncateUInt64(FileSize.QuadPart);
            Result.Contents =
                VirtualAlloc(0, Result.ContentsSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (Result.Contents) {
                DWORD BytesRead;
                if (ReadFile(FileHandle, Result.Contents, Result.ContentsSize, &BytesRead, 0) &&
                    (Result.ContentsSize == BytesRead)) {
                    // NOTE: Read Successfully
                } else {
                    DEBUGPlatformFreeFileMemory(Result.Contents);
                    Result.Contents = 0;
                }
            } else {
                // TODO Log
            }
        } else {
            // TODO Log
        }

        CloseHandle(FileHandle);
    } else {
        // TODO Log
    }
    return Result;
}

internal void
DEBUGPlatformFreeFileMemory(void *BitmapMemory)
{
    VirtualFree(BitmapMemory, 0, MEM_RELEASE);
}

internal bool32
DEBUGPlatformWriteEntireFile(char *FileName, uint32 MemorySize, void *Memory)
{
    bool32 Result = false;

    HANDLE FileHandle = CreateFileA(FileName, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (FileHandle != INVALID_HANDLE_VALUE) {
        DWORD BytesWritten;
        if (WriteFile(FileHandle, Memory, MemorySize, &BytesWritten, 0)) {
            // NOTE: Read Successfully
            Result = (MemorySize == BytesWritten);
        } else {
            // TODO Log
        }

        CloseHandle(FileHandle);
    } else {
        // TODO Log
    }
    return Result;
}

internal void
Win32LoadXInput()
{
    // TODO: Test on Windows 8
    HMODULE XInputLibrary = LoadLibraryA("xinput1_4.dll");
    if (!XInputLibrary) {
        XInputLibrary = LoadLibraryA("xinput1_3.dll");
    }
    if (!XInputLibrary) {
        XInputLibrary = LoadLibraryA("xinput1_1.dll");
    }
    if (!XInputLibrary) {
        XInputLibrary = LoadLibraryA("xinput9_1_0.dll");
    }
    if (XInputLibrary) {
        XInputGetState = (x_input_get_state *)GetProcAddress(XInputLibrary, "XInputGetState");
        XInputSetState = (x_input_set_state *)GetProcAddress(XInputLibrary, "XInputSetState");
    }
}

internal void
Win32InitDSound(HWND Window, int32 SamplesPerSecond, int32 BufferSize)
{
    // NOTE Load lib
    HMODULE DSoundLibrary = LoadLibrary("dsound.dll");
    if (DSoundLibrary) {
        // NOTE Get direct sound object
        direct_sound_create *DirectSoundCreate =
            (direct_sound_create *)GetProcAddress(DSoundLibrary, "DirectSoundCreate");
        LPDIRECTSOUND DirectSound;
        if (DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &DirectSound, 0))) {
            WAVEFORMATEX WaveFormat = {};
            WaveFormat.wFormatTag = WAVE_FORMAT_PCM;
            WaveFormat.nChannels = 2;
            WaveFormat.nSamplesPerSec = SamplesPerSecond;
            WaveFormat.wBitsPerSample = 16;
            WaveFormat.nBlockAlign = (WaveFormat.nChannels * WaveFormat.wBitsPerSample) / 8;
            WaveFormat.nAvgBytesPerSec = WaveFormat.nSamplesPerSec * WaveFormat.nBlockAlign;
            WaveFormat.cbSize = 0;

            if (SUCCEEDED(DirectSound->SetCooperativeLevel(Window, DSSCL_PRIORITY))) {
                // Create a primary buffer
                // TODO: DSBCAPS_GLOBALFOCUS?
                DSBUFFERDESC BufferDescription = {};
                BufferDescription.dwSize = sizeof(BufferDescription);
                BufferDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;

                LPDIRECTSOUNDBUFFER PrimaryBuffer;

                if (SUCCEEDED(DirectSound->CreateSoundBuffer(&BufferDescription, &PrimaryBuffer, 0))) {
                    HRESULT Error = PrimaryBuffer->SetFormat(&WaveFormat);
                    if (SUCCEEDED(Error)) {
                        OutputDebugStringA("Primary buffer ready\n");
                    } else {
                        // TODO: Diagnostics
                    }
                }
            } else {
                // TODO: Diagnostic
            }

            // Create a secondary buffer
            // TODO: DSBCAPS_GETCURRENTPOSITION2?
            DSBUFFERDESC BufferDescription = {};
            BufferDescription.dwSize = sizeof(BufferDescription);
            BufferDescription.dwFlags = DSBCAPS_GETCURRENTPOSITION2;
            BufferDescription.dwBufferBytes = BufferSize;
            BufferDescription.lpwfxFormat = &WaveFormat;

            if (DirectSound->CreateSoundBuffer(&BufferDescription, &GlobalSecondaryBuffer, 0) == DS_OK) {
                OutputDebugStringA("Secondary buffer created\n");
            }

            // Start playing

        } else {
            // TODO Error handling
        }
    } else {
        // TODO Error handling
    }
}

internal win32_window_dimension
Win32GetWindowDimension(HWND Window)
{
    win32_window_dimension Result;

    RECT ClientRect;
    GetClientRect(Window, &ClientRect);

    Result.Width = ClientRect.right - ClientRect.left;
    Result.Height = ClientRect.bottom - ClientRect.top;

    return Result;
}

internal void
Win32ResizeDIBSection(win32_offscreen_buffer *Buffer, const int Width, const int Height)
{
    if (Buffer->Memory) {
        VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
    }

    Buffer->Width = Width;
    Buffer->Height = Height;

    int BytesPerPixel = 4;
    Buffer->BytesPerPixel = BytesPerPixel;

    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Buffer->Width;
    Buffer->Info.bmiHeader.biHeight = -Buffer->Height;
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32;
    Buffer->Info.bmiHeader.biCompression = BI_RGB;

    int BitmapMemorySize = (Buffer->Width * Buffer->Height) * BytesPerPixel;
    Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Buffer->Pitch = Buffer->Width * BytesPerPixel; // Size of Row
    // TODO Clear to black, maybe
}

internal void
Win32CopyBufferToWindow(win32_offscreen_buffer *Buffer, HDC DeviceContext, int WindowWidth,
                        int WindowHeight)
{
    StretchDIBits(DeviceContext,
                  // X, Y, Width, Height, // dest
                  0, 0, WindowWidth, WindowHeight,
                  // X, Y, Width, Height, // src
                  0, 0, Buffer->Width, Buffer->Height, Buffer->Memory, &Buffer->Info,
                  DIB_RGB_COLORS, // Colour type, either pallet or RGB, in this case RGB
                  SRCCOPY);       // hows to rasterise when upscalling, we just want to copy
}

internal LRESULT CALLBACK
Win32MainWindowCallback(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    LRESULT Result = 0;

    switch (Message) {
        case WM_DESTROY: {
            // TODO Handle this with an error
            GlobalRunning = false;
        } break;

        case WM_CLOSE: {
            // TODO Handle with message
            GlobalRunning = false;
        } break;

        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP: {
            Assert(!"dispatch code hahah");

        } break;

        case WM_ACTIVATEAPP: {
            OutputDebugStringA("Active\n");
        } break;

        case WM_PAINT: {
            PAINTSTRUCT            Paint;
            HDC                    DeviceContext = BeginPaint(Window, &Paint);
            win32_window_dimension Dimension = Win32GetWindowDimension(Window);
            Win32CopyBufferToWindow(&GlobalBackbuffer, DeviceContext, Dimension.Width,
                                    Dimension.Height);
            EndPaint(Window, &Paint);
        } break;

        default: {
            Result = DefWindowProcA(Window, Message, WParam, LParam);
        } break;
    }

    return Result;
}

internal void
Win32ClearSoundBuffer(win32_sound_output *SoundOutput)
{
    VOID   *Region1;
    DWORD   Region1Size;
    VOID   *Region2;
    DWORD   Region2Size;
    HRESULT SecondaryBufferLockResult = DS_OK ==
                                        GlobalSecondaryBuffer->Lock(0, SoundOutput->SecondaryBufferSize,
                                                                    &Region1, &Region1Size, &Region2,
                                                                    &Region2Size, 0);
    if (SecondaryBufferLockResult) {
        int8 *DestSamples = (int8 *)Region1;
        DWORD ByteIndex;
        for (ByteIndex = 0; ByteIndex < Region1Size; ++ByteIndex) {
            *DestSamples++ = 0;
        }

        DestSamples = (int8 *)Region2;
        for (ByteIndex = 0; ByteIndex < Region2Size; ++ByteIndex) {
            *DestSamples++ = 0;
        }
        GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
    }
}

internal void
Win32FillSoundBuffer(win32_sound_output *SoundOutput, DWORD ByteToLock, DWORD BytesToWrite,
                     game_sound_output_buffer *SourceBuffer)
{
    VOID *Region1;
    DWORD Region1Size;
    VOID *Region2;
    DWORD Region2Size;

    HRESULT SecondaryBufferLockResult = GlobalSecondaryBuffer->Lock(
                                            ByteToLock, BytesToWrite, &Region1, &Region1Size, &Region2, &Region2Size, 0) == DS_OK;
    if (SecondaryBufferLockResult) {
        // TODO: Assert that Region1Size and Region2Size are valid
        DWORD  Region1SampleCount = Region1Size / SoundOutput->BytesPerSample;
        int16 *DestSamples = (int16 *)Region1;
        int16 *SourceSamples = (int16 *)SourceBuffer->Samples;
        for (DWORD SampleIndex = 0; SampleIndex < Region1SampleCount; ++SampleIndex) {
            *DestSamples++ = *SourceSamples++;
            *DestSamples++ = *SourceSamples++;
            ++SoundOutput->RunningSampleIndex;
        }

        DWORD Region2SampleCount = Region2Size / SoundOutput->BytesPerSample;
        DestSamples = (int16 *)Region2;
        for (DWORD SampleIndex = 0; SampleIndex < Region2SampleCount; ++SampleIndex) {
            *DestSamples++ = *SourceSamples++;
            *DestSamples++ = *SourceSamples++;
            ++SoundOutput->RunningSampleIndex;
        }
        GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
    }
}

internal void
Win32ProcessXInputDigitalButton(DWORD XInputButtonState, game_button_state *OldState,
                                DWORD ButtonBit, game_button_state *NewState)
{
    NewState->EndedDown = (XInputButtonState & ButtonBit) == ButtonBit;
    NewState->HalfTransitionCount = (OldState->EndedDown != NewState->EndedDown) ? 1 : 0;
}

internal void
Win32ProcessKeyboardMessage(game_button_state *NewState, bool32 IsDown)
{
    Assert(NewState->EndedDown != IsDown);
    NewState->EndedDown = IsDown;
    ++NewState->HalfTransitionCount;
}
internal real32
Win32ProcessXInputStickValue(SHORT Value, SHORT DeadZoneThreshold)
{
    real32 Result = 0;
    if (Value < -DeadZoneThreshold) {
        Result = (real32)((Value + DeadZoneThreshold) / (32768.0f - DeadZoneThreshold));
    } else if (Value > DeadZoneThreshold) {
        Result = (real32)((Value - DeadZoneThreshold) / (32767.0f - DeadZoneThreshold));
    }
    return Result;
}

internal void
Win32ProcessPendingMessages(game_controller_input *KeyboardController)
{
    MSG Message;
    while (PeekMessageA(&Message, 0, 0, 0, PM_REMOVE)) {
        switch (Message.message) {
            case WM_QUIT: {
                GlobalRunning = false;
            } break;

            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_KEYDOWN:
            case WM_KEYUP: {
                uint32 VKCode = (uint32)Message.wParam;
                bool32 WasDown = (Message.lParam & (1 << 30)) != 0;
                bool32 IsDown = ((Message.lParam & (1 << 31)) == 0);
                if (WasDown != IsDown) {
                    if (VKCode == 'W') {
                        Win32ProcessKeyboardMessage(&KeyboardController->MoveUp, IsDown);
                    } else if (VKCode == 'A') {
                        Win32ProcessKeyboardMessage(&KeyboardController->MoveLeft, IsDown);
                    } else if (VKCode == 'S') {
                        Win32ProcessKeyboardMessage(&KeyboardController->MoveDown, IsDown);
                    } else if (VKCode == 'D') {
                        Win32ProcessKeyboardMessage(&KeyboardController->MoveRight, IsDown);
                    } else if (VKCode == 'Q') {
                        Win32ProcessKeyboardMessage(&KeyboardController->LeftShoulder, IsDown);
                    } else if (VKCode == 'E') {
                        Win32ProcessKeyboardMessage(&KeyboardController->RightShoulder, IsDown);
                    } else if (VKCode == VK_UP) {
                        Win32ProcessKeyboardMessage(&KeyboardController->ActionUp, IsDown);
                    } else if (VKCode == VK_DOWN) {
                        Win32ProcessKeyboardMessage(&KeyboardController->ActionDown, IsDown);
                    } else if (VKCode == VK_LEFT) {
                        Win32ProcessKeyboardMessage(&KeyboardController->ActionLeft, IsDown);
                    } else if (VKCode == VK_RIGHT) {
                        Win32ProcessKeyboardMessage(&KeyboardController->ActionRight, IsDown);
                    } else if (VKCode == VK_ESCAPE) {
                        GlobalRunning = false;
                    } else if (VKCode == VK_SPACE) {
                    }

                    bool32 AltKeyWasDown = (Message.lParam & (1 << 29));
                    if ((VKCode == VK_F4) && AltKeyWasDown) {
                        GlobalRunning = false;
                    }
                }
            } break;
            default: {
                TranslateMessage(&Message);
                DispatchMessageA(&Message);
            }
        }
    }
}

inline LARGE_INTEGER
Win32GetWallClock()
{
    LARGE_INTEGER Result;
    QueryPerformanceCounter(&Result);

    return Result;
}

inline real32
Win32SecondsElapsed(LARGE_INTEGER Start, LARGE_INTEGER End)
{
    return (((real32)End.QuadPart - Start.QuadPart) / (real32)GlobalPerfCountFrequency);
}

internal void
Win32DebugDrawVertical(win32_offscreen_buffer *BackBuffer, int X, int Top, int Bottom, uint32 Colour)
{
    uint8 *Pixel = ((uint8 *)BackBuffer->Memory +
                    ((X * BackBuffer->BytesPerPixel) +
                     (Top * BackBuffer->Pitch)));
    for (int Y = Top; Y < Bottom; ++Y) {
        *(uint32 *)Pixel = Colour;
        Pixel += (BackBuffer->Pitch);
    }
}

inline void
Win32DrawSoundBufferMarker(win32_offscreen_buffer *BackBuffer,
                           win32_sound_output     *SoundOuput,
                           real32 C, int PadX, int Top, int Bottom,
                           DWORD Value, uint32 Colour)
{
    Assert(Value < SoundOuput->SecondaryBufferSize);
    real32 XReal32 = (C * (real32)Value);
    int    X = PadX + (int)XReal32;
    Win32DebugDrawVertical(BackBuffer, X, Top, Bottom, Colour);
}

internal void
Win32DebugSyncDisplay(win32_offscreen_buffer *BackBuffer, int MarkerCount,
                      win32_debug_time_marker *Markers,
                      win32_sound_output *SoundOuput, real32 TargetsSecondsPerFrame)
{
    int PadX = 16;
    int PadY = 16;

    int Top = PadY;
    int Bottom = BackBuffer->Height - PadY;

    real32 C = (real32)(BackBuffer->Width - (2 * PadY)) / (real32)SoundOuput->SecondaryBufferSize;
    for (int MarkerIndex = 0; MarkerIndex < MarkerCount; ++MarkerIndex) {
        win32_debug_time_marker *ThisMarker = &Markers[MarkerIndex];
        Win32DrawSoundBufferMarker(BackBuffer, SoundOuput,
                                   C, PadX, Top, Bottom,
                                   ThisMarker->PlayCursor,
                                   0xffffffff);
        Win32DrawSoundBufferMarker(BackBuffer, SoundOuput,
                                   C, PadX, Top, Bottom,
                                   ThisMarker->WriteCursor,
                                   0x00ff0000);
    }
}

int
WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CmdLine, int ShowCode)
{
    LARGE_INTEGER PerfCountFrequencyResult;
    QueryPerformanceFrequency(&PerfCountFrequencyResult);
    GlobalPerfCountFrequency = PerfCountFrequencyResult.QuadPart;

    // NOTE: Windows scheduler granularity being set to 1ms
    // So sleep function will work with a finer granularity
    UINT   DesiredSchedulerMS = 1;
    bool32 SleepIsGranular = (timeBeginPeriod(DesiredSchedulerMS) == TIMERR_NOERROR);

    Win32LoadXInput();

    WNDCLASS WindowClass = {};

    Win32ResizeDIBSection(&GlobalBackbuffer, 1280, 720);

    WindowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    WindowClass.lpfnWndProc = Win32MainWindowCallback;
    WindowClass.hInstance = Instance;
    WindowClass.lpszClassName = "Handmade Hero";

#define FramesOfAudioLatency 3
#define MonitorRefreshHz 60
#define GameUpdateHz (MonitorRefreshHz / 2)
    real32 TargetSecondsPerFrame = 1.0f / (real32)GameUpdateHz;

    if (RegisterClass(&WindowClass)) {
        HWND Window = CreateWindowExA(
            0, WindowClass.lpszClassName, "Handmade hero", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, Instance, 0);

        if (Window) {
            HDC DeviceContext = GetDC(Window);

            win32_sound_output SoundOutput = {};
            SoundOutput.SamplesPerSeconds = 48000;
            SoundOutput.BytesPerSample = sizeof(int16) * 2;
            SoundOutput.SecondaryBufferSize =
                SoundOutput.SamplesPerSeconds * SoundOutput.BytesPerSample;
            SoundOutput.LatencySampleCount = FramesOfAudioLatency *
                                             (SoundOutput.SamplesPerSeconds / GameUpdateHz);

            Win32InitDSound(Window, SoundOutput.SamplesPerSeconds, SoundOutput.SecondaryBufferSize);
            Win32ClearSoundBuffer(&SoundOutput);
            GlobalSecondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);

            GlobalRunning = true;
#if 0
            // NOTE: Testing playcursor white cursor frquency of update
            // Test this and ensure figure out the number of samples.
            // For reference Casey show's the calculations on ep 19
            while(GlobalRunning)
            {
                DWORD PlayCursor;
                DWORD WriteCursor;
                GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor);

                char TextBuffer[256];
                _snprintf_s(TextBuffer, sizeof(TextBuffer),
                            "PC:%u WC:%u\n", PlayCursor, WriteCursor);
                OutputDebugStringA(TextBuffer);
            }
#endif

            int16 *Samples = (int16 *)VirtualAlloc(0, SoundOutput.SecondaryBufferSize,
                                                   MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

#if HANDMADE_INTERNAL
            LPVOID BaseAddress = (LPVOID)Terabytes((uint64)2);
#else
            LPVOID BaseAddress = 0;
#endif

            game_memory GameMemory = {};
            GameMemory.PermanentStorageSize = Megabytes(64);
            GameMemory.TransientStorageSize = Gigabytes(1);
            uint64 TotalSize = GameMemory.PermanentStorageSize +
                               GameMemory.TransientStorageSize;

            GameMemory.PermanentStorage =
                VirtualAlloc(BaseAddress, (size_t)TotalSize,
                             MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            GameMemory.TransientStorage =
                ((uint8 *)GameMemory.PermanentStorage + GameMemory.PermanentStorageSize);

            if (Samples && GameMemory.PermanentStorage && GameMemory.TransientStorage) {
                game_input  Input[2] = {};
                game_input *NewInput = &Input[0];
                game_input *OldInput = &Input[1];

                LARGE_INTEGER LastCounter = Win32GetWallClock();

                // TODO: Handle startup
                DWORD  LastPlayCursor = 0;
                bool32 SoundIsValid = false;

                uint64 LastCycleCount = __rdtsc();

                int                                     DebugTimeMarkerIndex = 0;
                global_variable win32_debug_time_marker DebugTimeMakers[GameUpdateHz / 2] = {};

                while (GlobalRunning) {

                    game_controller_input *OldKeyboardController = GetController(OldInput, 0);
                    game_controller_input *NewKeyboardController = GetController(NewInput, 0);
                    *NewKeyboardController = {};
                    NewKeyboardController->IsConnected = true;
                    for (int ButtonIndex = 0;
                         ButtonIndex < ArrayCount(NewKeyboardController->Buttons);
                         ++ButtonIndex) {
                        NewKeyboardController->Buttons[ButtonIndex].EndedDown =
                            OldKeyboardController->Buttons[ButtonIndex].EndedDown;
                    }

                    Win32ProcessPendingMessages(NewKeyboardController);

                    // TODO Should this be polled more often
                    DWORD MaxControllerCount = XUSER_MAX_COUNT;
                    if (MaxControllerCount > (ArrayCount(NewInput->Controllers) - 1)) {
                        MaxControllerCount = (ArrayCount(NewInput->Controllers) - 1);
                    }
                    for (DWORD ControllerIndex = 0; ControllerIndex < MaxControllerCount;
                         ControllerIndex++) {
                        DWORD OurControllerIndex = ControllerIndex + 1;

                        game_controller_input *OldController = GetController(OldInput, OurControllerIndex);
                        game_controller_input *NewController = GetController(NewInput, OurControllerIndex);
                        XINPUT_STATE           ControllerState;
                        if (XInputGetState(ControllerIndex, &ControllerState) == ERROR_SUCCESS) {
                            NewController->IsConnected = true;
                            // Controller plugged in
                            // TODO: See if increments too rapidly \/
                            // ControllerState.dwPacketNumber;
                            XINPUT_GAMEPAD *Pad = &ControllerState.Gamepad;

                            NewController->StickAverageX = Win32ProcessXInputStickValue(
                                Pad->sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                            NewController->StickAverageY = Win32ProcessXInputStickValue(
                                Pad->sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);

                            if ((NewController->StickAverageX != 0.0f) ||
                                (NewController->StickAverageY != 0.0f)) {
                                NewController->IsAnalog = true;
                            }

                            if (Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP) {
                                NewController->StickAverageY = 1.0f;
                                NewController->IsAnalog = false;
                            }
                            if (Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN) {
                                NewController->StickAverageY = -1.0f;
                                NewController->IsAnalog = false;
                            }
                            if (Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) {
                                NewController->StickAverageY = -1.0f;
                                NewController->IsAnalog = false;
                            }
                            if (Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) {
                                NewController->StickAverageY = 1.0f;
                                NewController->IsAnalog = false;
                            }

                            real32 Threshold = 0.5f;
                            Win32ProcessXInputDigitalButton(
                                (NewController->StickAverageX < -Threshold) ? 1 : 0,
                                &OldController->MoveLeft, 1, &NewController->ActionDown);
                            Win32ProcessXInputDigitalButton(
                                (NewController->StickAverageX > Threshold) ? 1 : 0,
                                &OldController->MoveRight, 1, &NewController->ActionDown);
                            Win32ProcessXInputDigitalButton(
                                (NewController->StickAverageY < -Threshold) ? 1 : 0,
                                &OldController->MoveDown, 1, &NewController->ActionDown);
                            Win32ProcessXInputDigitalButton(
                                (NewController->StickAverageY > Threshold) ? 1 : 0,
                                &OldController->MoveUp, 1, &NewController->ActionDown);

                            Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                            &OldController->ActionDown, XINPUT_GAMEPAD_A,
                                                            &NewController->ActionDown);
                            Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                            &OldController->ActionRight, XINPUT_GAMEPAD_B,
                                                            &NewController->ActionRight);
                            Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                            &OldController->ActionLeft, XINPUT_GAMEPAD_X,
                                                            &NewController->ActionLeft);
                            Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                            &OldController->ActionUp, XINPUT_GAMEPAD_Y,
                                                            &NewController->ActionUp);
                            Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                            &OldController->LeftShoulder, XINPUT_GAMEPAD_LEFT_SHOULDER,
                                                            &NewController->LeftShoulder);
                            Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                            &OldController->RightShoulder, XINPUT_GAMEPAD_RIGHT_SHOULDER,
                                                            &NewController->RightShoulder);

                            Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                            &OldController->Back, XINPUT_GAMEPAD_BACK,
                                                            &NewController->Back);
                            Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                            &OldController->Start, XINPUT_GAMEPAD_START,
                                                            &NewController->Start);

                        } else {
                            // Controller not available
                            NewController->IsConnected = false;
                        }
                    }

                    //////////////////////////////////////////////////
                    // NOTE: COMPUTING SOUND AND WHERE TO PLAY NEXT //
                    //////////////////////////////////////////////////

                    DWORD ByteToLock = 0;
                    DWORD TargetCursor = 0;
                    DWORD BytesToWrite = 0;

                    if (SoundIsValid) {
                        ByteToLock =
                            ((SoundOutput.RunningSampleIndex * SoundOutput.BytesPerSample) %
                             SoundOutput.SecondaryBufferSize);
                        TargetCursor = ((LastPlayCursor + (SoundOutput.LatencySampleCount *
                                                           SoundOutput.BytesPerSample)) %
                                        SoundOutput.SecondaryBufferSize);
                        if (ByteToLock > TargetCursor) {
                            BytesToWrite = SoundOutput.SecondaryBufferSize - ByteToLock;
                            BytesToWrite += TargetCursor;
                        } else {
                            BytesToWrite = TargetCursor - ByteToLock;
                        }
                    }

                    game_sound_output_buffer SoundBuffer = {};
                    SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSeconds;
                    SoundBuffer.SampleCount = BytesToWrite / SoundOutput.BytesPerSample;
                    SoundBuffer.Samples = Samples;

                    game_offscreen_buffer Buffer = {};
                    Buffer.Memory = GlobalBackbuffer.Memory;
                    Buffer.Width = GlobalBackbuffer.Width;
                    Buffer.Height = GlobalBackbuffer.Height;
                    Buffer.Pitch = GlobalBackbuffer.Pitch;

                    GameUpdateAndRender(&GameMemory, NewInput, &Buffer, &SoundBuffer);

                    if (SoundIsValid) {
#ifdef HANDMADE_INTERNAL
                        DWORD PlayCursor = 0;
                        DWORD WriteCursor = 0;
                        GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor);

                        char TextBuffer[256];
                        _snprintf_s(TextBuffer, sizeof(TextBuffer),
                                    "PC: %u BTL: %u TC: %u WC: %u BTW: %u\n",
                                    LastPlayCursor, ByteToLock, TargetCursor, WriteCursor, BytesToWrite);
                        OutputDebugStringA(TextBuffer);
#endif
                        Win32FillSoundBuffer(&SoundOutput, ByteToLock, BytesToWrite, &SoundBuffer);
                    }

                    LARGE_INTEGER WorkCounter = Win32GetWallClock();
                    real32        WorkSecondsElapsed = Win32SecondsElapsed(LastCounter, WorkCounter);

                    real32 SecondsElapsedForFrame = WorkSecondsElapsed;
#define SleepDelta 4
                    if (SecondsElapsedForFrame < TargetSecondsPerFrame) {
                        if (SleepIsGranular) {
                            DWORD SleepMS = (DWORD)((1000.f * (TargetSecondsPerFrame - SecondsElapsedForFrame)));
                            if ((SleepMS - SleepDelta) > 0) {
                                // NOTE: Sleep is not as granular as we hoped, taking some away from it
                                // to ensure we don't hit the essert bellow
                                Sleep(SleepMS - SleepDelta);
                            }
                        }

                        real32 TestSecondsElapsedForFrame = Win32SecondsElapsed(LastCounter, Win32GetWallClock());

                        Assert(TestSecondsElapsedForFrame < TargetSecondsPerFrame);
                        while (SecondsElapsedForFrame < TargetSecondsPerFrame) {
                            LARGE_INTEGER CheckCounter = Win32GetWallClock();
                            SecondsElapsedForFrame = Win32SecondsElapsed(LastCounter, CheckCounter);
                        }
                    } else {
                        // MISSED FRAME RATE
                    }

                    LARGE_INTEGER EndCounter = Win32GetWallClock();
                    real32        MSPerFrame = 1000.0f * Win32SecondsElapsed(LastCounter, EndCounter);
                    LastCounter = EndCounter;

                    win32_window_dimension Dimension = Win32GetWindowDimension(Window);
#ifdef HANDMADE_INTERNAL
                    Win32DebugSyncDisplay(&GlobalBackbuffer, ArrayCount(DebugTimeMakers),
                                          DebugTimeMakers, &SoundOutput, TargetSecondsPerFrame);
#endif
                    Win32CopyBufferToWindow(&GlobalBackbuffer, DeviceContext, Dimension.Width,
                                            Dimension.Height);

                    DWORD PlayCursor = 0;
                    DWORD WriteCursor = 0;

                    if (GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor) == DS_OK) {
                        LastPlayCursor = PlayCursor;
                        if (!SoundIsValid) {
                            SoundOutput.RunningSampleIndex = WriteCursor / SoundOutput.BytesPerSample;
                            SoundIsValid = true;
                        }
                    } else {
                        SoundIsValid = false;
                    }

#ifdef HANDMADE_INTERNAL
                    {
                        win32_debug_time_marker *Marker = &DebugTimeMakers[DebugTimeMarkerIndex++];
                        // = PlayCursor;
                        if (DebugTimeMarkerIndex > ArrayCount(DebugTimeMakers)) {
                            DebugTimeMarkerIndex = 0;
                        }
                        Marker->PlayCursor = PlayCursor;
                        Marker->WriteCursor = WriteCursor;
                    }
#endif

#if 0
#endif

                    game_input *Temp = NewInput;
                    NewInput = OldInput;
                    OldInput = Temp;

                    int64  EndCycleCount = __rdtsc();
                    uint64 CyclesElapsed = EndCycleCount - LastCycleCount;
                    LastCycleCount = EndCycleCount;

                    real32 FPS = 0.f;
                    real32 MCPF = ((real32)CyclesElapsed /
                                   (1000.0f * 1000.0f)); // Mega cycles per frame

                    char FPSBuffer[256];
                    _snprintf_s(FPSBuffer, sizeof(FPSBuffer),
                                "%.02fms/f, %.02ff/s, %.02fmc/f\n",
                                MSPerFrame, FPS, MCPF);
                    OutputDebugStringA(FPSBuffer);
                }
            } else {
                // TODO: Logging
            }
        } else {
            // TODO: Logging
        }
    } else {
        // TODO: Logging
    };

    return 0;
}
