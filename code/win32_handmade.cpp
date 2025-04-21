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

#include "HandmadeDef.h"

#include "handmade.cpp"
#include "handmade.h"

#include <dsound.h>
#include <malloc.h>
#include <stdio.h>
#include <windef.h> // including as I'm using my personal emacs config on WLS and this removes a lot of the squigly reds when .ccls is configured properly
#include <windows.h>
#include <xinput.h>

#include "win32_handmade.h"

global_variable bool32 GlobalRunning;
global_variable LPDIRECTSOUNDBUFFER GlobalSecondaryBuffer;
global_variable win32_offscreen_buffer GlobalBackbuffer;

#define MapRange(InMin, InMax, OutMin, OutMax, Val)                                      \
    (OutMin + (Val - InMin) * (OutMax - OutMin) / (InMax - InMin))

#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub) { return ERROR_DEVICE_NOT_CONNECTED; }
global_variable x_input_get_state *XInputGetState_ = XInputGetStateStub;

#define X_INPUT_SET_STATE(name)                                                          \
    DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub) { return ERROR_DEVICE_NOT_CONNECTED; }
global_variable x_input_set_state *XInputSetState_ = XInputSetStateStub;

#define XInputGetState XInputGetState_
#define XInputSetState XInputSetState_

internal debug_read_file_result DEBUGPlatformReadEntireFile(char *FileName) {
    debug_read_file_result Result = {};

    HANDLE FileHandle =
        CreateFileA(FileName, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (FileHandle != INVALID_HANDLE_VALUE) {

        LARGE_INTEGER FileSize;
        if (GetFileSizeEx(FileHandle, &FileSize)) {
            Assert(FileSize.QuadPart <= 0xffffffff);
            Result.ContentsSize = SafeTruncateUInt64(FileSize.QuadPart);
            Result.Contents = VirtualAlloc(0, Result.ContentsSize,
                                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (Result.Contents) {
                DWORD BytesRead;
                if (ReadFile(FileHandle, Result.Contents, Result.ContentsSize, &BytesRead,
                             0) &&
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
internal void DEBUGPlatformFreeFileMemory(void *BitmapMemory) {
    VirtualFree(BitmapMemory, 0, MEM_RELEASE);
}

internal bool32 DEBUGPlatformWriteEntireFile(char *FileName, uint32 MemorySize,
                                             void *Memory) {

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

internal void Win32LoadXInput() {
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
        XInputGetState =
            (x_input_get_state *)GetProcAddress(XInputLibrary, "XInputGetState");
        XInputSetState =
            (x_input_set_state *)GetProcAddress(XInputLibrary, "XInputSetState");
    }
}

#define DIRECT_SOUND_CREATE(name)                                                        \
    HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(direct_sound_create);

internal void Win32InitDSound(HWND Window, int32 SamplesPerSecond, int32 BufferSize) {
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
            WaveFormat.nBlockAlign =
                (WaveFormat.nChannels * WaveFormat.wBitsPerSample) / 8;
            WaveFormat.nAvgBytesPerSec =
                WaveFormat.nSamplesPerSec * WaveFormat.nBlockAlign;
            WaveFormat.cbSize = 0;

            if (SUCCEEDED(DirectSound->SetCooperativeLevel(Window, DSSCL_PRIORITY))) {
                // Create a primary buffer
                // TODO: DSBCAPS_GLOBALFOCUS?
                DSBUFFERDESC BufferDescription = {};
                BufferDescription.dwSize = sizeof(BufferDescription);
                BufferDescription.dwFlags = 0;
                LPDIRECTSOUNDBUFFER PrimaryBuffer;

                if (SUCCEEDED(DirectSound->CreateSoundBuffer(&BufferDescription,
                                                             &PrimaryBuffer, 0))) {
                    HRESULT Error = PrimaryBuffer->SetFormat(&WaveFormat);
                    if (SUCCEEDED(Error)) {
                        OutputDebugStringA("Primary buffer ready");
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
            BufferDescription.dwFlags = 0;
            BufferDescription.dwBufferBytes = BufferSize;
            BufferDescription.lpwfxFormat = &WaveFormat;

            if (SUCCEEDED(DirectSound->CreateSoundBuffer(&BufferDescription,
                                                         &GlobalSecondaryBuffer, 0))) {
                OutputDebugStringA("Secondary buffer created");
            }

            // Start playing

        } else {
            // TODO Error handling
        }
    } else {
        // TODO Error handling
    }
}

win32_window_dimension Win32GetWindowDimension(HWND Window) {
    win32_window_dimension Result;

    RECT ClientRect;
    GetClientRect(Window, &ClientRect);

    Result.Width = ClientRect.right - ClientRect.left;
    Result.Height = ClientRect.bottom - ClientRect.top;

    return Result;
}

internal void Win32ResizeDIBSection(win32_offscreen_buffer *Buffer, int Width,
                                    int Height) {
    // TODO: MAKE BETTER

    if (Buffer->Memory) {
        VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
    }

    Buffer->Width = Width;
    Buffer->Height = Height;

    int BytesPerPixel = 4;

    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Width;
    Buffer->Info.bmiHeader.biHeight = -Height;
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32;
    Buffer->Info.bmiHeader.biCompression = BI_RGB;

    int BitmapMemorySize = (Buffer->Width * Buffer->Height) * BytesPerPixel;
    Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);
    Buffer->Pitch = Buffer->Width * BytesPerPixel; // Size of Row

    // TODO Clear to black, maybe
}

internal void Win32CopyBufferToWindow(win32_offscreen_buffer *Buffer, HDC DeviceContext,
                                      int WindowWidth, int WindowHeight) {

    // TODO: Aspect ratio correction
    StretchDIBits(DeviceContext,
                  // X, Y, Width, Height, // dest
                  0, 0, WindowWidth, WindowHeight,
                  // X, Y, Width, Height, // src
                  0, 0, Buffer->Width, Buffer->Height, Buffer->Memory, &Buffer->Info,
                  DIB_RGB_COLORS, // Colour type, either pallet or RGB, in this case RGB
                  SRCCOPY); // hows to rasterise when upscalling, we just want to copy
}

LRESULT CALLBACK Win32MainWindowCallback(HWND Window, UINT Message, WPARAM WParam,
                                         LPARAM LParam) {
    LRESULT Result = 0;

    switch (Message) {
        case WM_SIZE: {
        } break;

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
            uint32 VKCode = WParam;
            bool32 WasDown = (LParam & (1 << 30)) != 0;
            bool32 IsDown = ((LParam & (1 << 31)) == 0);

            if (WasDown != IsDown) {
                if (VKCode == 'W') {
                } else if (VKCode == 'A') {
                } else if (VKCode == 'S') {
                } else if (VKCode == 'D') {
                } else if (VKCode == 'Q') {
                } else if (VKCode == 'E') {
                } else if (VKCode == VK_UP) {
                } else if (VKCode == VK_DOWN) {
                } else if (VKCode == VK_LEFT) {
                } else if (VKCode == VK_RIGHT) {
                } else if (VKCode == VK_ESCAPE) {
                    OutputDebugStringA("Escape\n");
                    if (IsDown) {
                        OutputDebugStringA("is down\n");
                    }
                    if (WasDown) {
                        OutputDebugStringA("was down\n");
                    }
                } else if (VKCode == VK_SPACE) {
                }

                bool32 AltKeyWasDown = (LParam & (1 << 29));
                if ((VKCode == VK_F4) && AltKeyWasDown) {
                    GlobalRunning = false;
                }
            }
        } break;

        case WM_ACTIVATEAPP: {
            OutputDebugStringA("Active\n");
        } break;

        case WM_PAINT: {
            PAINTSTRUCT Paint;
            HDC DeviceContext = BeginPaint(Window, &Paint);
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

internal void Win32ClearSoundBuffer(win32_sound_output *SoundOutput) {
    VOID *Region1;
    DWORD Region1Size;
    VOID *Region2;
    DWORD Region2Size;
    HRESULT SecondaryBufferLockResult =
        GlobalSecondaryBuffer->Lock(0, SoundOutput->SecondaryBufferSize, &Region1,
                                    &Region1Size, &Region2, &Region2Size, 0);
    if (SUCCEEDED(SecondaryBufferLockResult)) {
        int8 *DestSamples = (int8 *)Region1;
        DWORD ByteIndex;
        for (ByteIndex = 0; ByteIndex < Region1Size; ++ByteIndex) {
            *DestSamples++ = 0;
        }

        DestSamples = (int8 *)Region2;
        for (ByteIndex = 0; ByteIndex < Region2Size; ++ByteIndex) {
            *DestSamples++ = 0;
        }
        HRESULT UnllockStatus =
            GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
    }
}

internal void Win32FillSoundBuffer(win32_sound_output *SoundOutput, DWORD ByteToLock,
                                   DWORD BytesToWrite,
                                   game_sound_output_buffer *SourceBuffer) {
    VOID *Region1;
    DWORD Region1Size;
    VOID *Region2;
    DWORD Region2Size;

    HRESULT SecondaryBufferLockResult = GlobalSecondaryBuffer->Lock(
        ByteToLock, BytesToWrite, &Region1, &Region1Size, &Region2, &Region2Size, 0);
    if (SUCCEEDED(SecondaryBufferLockResult)) {
        // TODO: Assert that Region1Size and Region2Size are valid
        DWORD Region1SampleCount = Region1Size / SoundOutput->BytesPerSample;
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

internal void Win32ProcessXInputDigitalButton(DWORD XInputButtonState,
                                              game_button_state *OldState,
                                              DWORD ButtonBit,
                                              game_button_state *NewState) {
    NewState->EndedDown = (XInputButtonState & ButtonBit) == ButtonBit;
    NewState->HalfTransitionCount = (OldState->EndedDown != NewState->EndedDown) ? 1 : 0;
}

int WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CmdLine, int ShowCode) {
    LARGE_INTEGER PerfCountFrequencyResult;
    QueryPerformanceFrequency(&PerfCountFrequencyResult);
    uint64 PerfCountFrequency = PerfCountFrequencyResult.QuadPart;

    Win32LoadXInput();

    WNDCLASS WindowClass = {};

    Win32ResizeDIBSection(&GlobalBackbuffer, 1280, 720);

    WindowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    WindowClass.lpfnWndProc = Win32MainWindowCallback;
    WindowClass.hInstance = Instance;
    WindowClass.lpszClassName = "Handmade Hero";

    if (RegisterClass(&WindowClass)) {
        HWND Window = CreateWindowExA(0, WindowClass.lpszClassName, "Handmade hero",
                                      WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                                      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
                                      Instance, 0);

        if (Window) {
            HDC DeviceContext = GetDC(Window);

            // TODO: Delete these
            int XOffset = 0;
            int YOffset = 0;

            win32_sound_output SoundOutput = {};
            SoundOutput.SamplesPerSeconds = 48000;
            SoundOutput.BytesPerSample = sizeof(int16) * 2;
            SoundOutput.SecondaryBufferSize =
                SoundOutput.SamplesPerSeconds * SoundOutput.BytesPerSample;
            SoundOutput.LatencySampleCount = SoundOutput.SamplesPerSeconds / 15;

            Win32InitDSound(Window, SoundOutput.SamplesPerSeconds,
                            SoundOutput.SecondaryBufferSize);
            Win32ClearSoundBuffer(&SoundOutput);
            GlobalSecondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);

            GlobalRunning = true;

            int16 *Samples =
                (int16 *)VirtualAlloc(0, SoundOutput.SecondaryBufferSize,
                                      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

#if HANDMADE_INTERNAL
            LPVOID BaseAddress = (LPVOID)Terabytes((uint64)2);
#else
            LPVOID BaseAddress = 0;
#endif

            game_memory GameMemory = {};
            GameMemory.PermanentStorageSize = Megabytes(64);
            GameMemory.TransientStorageSize = Gigabytes(4);
            uint64 TotalSize =
                GameMemory.PermanentStorageSize + GameMemory.TransientStorageSize;

            GameMemory.PermanentStorage = VirtualAlloc(
                BaseAddress, TotalSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            GameMemory.TransientStorage =
                ((uint8 *)GameMemory.PermanentStorage + GameMemory.PermanentStorageSize);

            if (Samples && GameMemory.PermanentStorage && GameMemory.TransientStorage) {
                game_input Input[2] = {};
                game_input *NewInput = &Input[0];
                game_input *OldInput = &Input[0];

                LARGE_INTEGER LastCounter;
                QueryPerformanceCounter(&LastCounter);
                uint64 LastCycleCount = __rdtsc();
                while (GlobalRunning) {
                    LARGE_INTEGER BeginCounter;
                    QueryPerformanceCounter(&BeginCounter);

                    MSG Message;
                    while (PeekMessageA(&Message, 0, 0, 0, PM_REMOVE)) {
                        if (Message.message == WM_QUIT) {
                            GlobalRunning = false;
                        }

                        TranslateMessage(&Message);
                        DispatchMessageA(&Message);
                    }

                    // TODO Should this be polled more often
                    int MaxControllerCount = XUSER_MAX_COUNT;
                    if (MaxControllerCount < ArrayCount(NewInput->Controllers)) {
                        MaxControllerCount = ArrayCount(NewInput->Controllers);
                    }
                    for (DWORD ControllerIndex = 0; ControllerIndex < MaxControllerCount;
                         ControllerIndex++) {

                        game_controller_input *OldController =
                            &OldInput->Controllers[ControllerIndex];
                        game_controller_input *NewController =
                            &NewInput->Controllers[ControllerIndex];
                        XINPUT_STATE ControllerState;
                        if (XInputGetState(ControllerIndex, &ControllerState) ==
                            ERROR_SUCCESS) {
                            // Controller plugged in
                            // TODO: See if increments too rapidly \/
                            // ControllerState.dwPacketNumber;
                            XINPUT_GAMEPAD *Pad = &ControllerState.Gamepad;

                            bool32 Up = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP);
                            bool32 Down = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
                            bool32 Left = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
                            bool32 Right = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);

                            // int16 LStickX = Pad->sThumbLX;
                            // int16 LStickY = Pad->sThumbLY;

                            NewController->IsAnalog = true;
                            NewController->StartX = OldController->EndX;
                            NewController->StartY = OldController->EndY;

                            real32 x;
                            if (Pad->sThumbLX < 0) {
                                x = (real32)Pad->sThumbLX / 32768.0f;
                            } else {
                                x = (real32)Pad->sThumbLX / 32767.0f;
                            }

                            NewController->MinX = OldController->MaxX =
                                NewController->EndX = x;

                            real32 y;
                            if (Pad->sThumbLY < 0) {
                                y = (real32)Pad->sThumbLY / 32768.0f;
                            } else {
                                y = (real32)Pad->sThumbLY / 32767.0f;
                            }

                            NewController->MinY = OldController->MaxY =
                                NewController->EndY = y;

                            Win32ProcessXInputDigitalButton(
                                Pad->wButtons, &OldController->Down, XINPUT_GAMEPAD_A,
                                &NewController->Down);
                            Win32ProcessXInputDigitalButton(
                                Pad->wButtons, &OldController->Right, XINPUT_GAMEPAD_B,
                                &NewController->Right);
                            Win32ProcessXInputDigitalButton(
                                Pad->wButtons, &OldController->Left, XINPUT_GAMEPAD_X,
                                &NewController->Left);
                            Win32ProcessXInputDigitalButton(
                                Pad->wButtons, &OldController->Up, XINPUT_GAMEPAD_Y,
                                &NewController->Up);
                            Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                            &OldController->LeftShoulder,
                                                            XINPUT_GAMEPAD_LEFT_SHOULDER,
                                                            &NewController->LeftShoulder);
                            Win32ProcessXInputDigitalButton(
                                Pad->wButtons, &OldController->RightShoulder,
                                XINPUT_GAMEPAD_RIGHT_SHOULDER,
                                &NewController->RightShoulder);

                            bool32 Start = (Pad->wButtons & XINPUT_GAMEPAD_START);
                            bool32 Back = (Pad->wButtons & XINPUT_GAMEPAD_BACK);
                            bool32 LeftShoulder =
                                (Pad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
                            bool32 RightShoulder =
                                (Pad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
                            bool32 AButton = (Pad->wButtons & XINPUT_GAMEPAD_A);
                            bool32 BButton = (Pad->wButtons & XINPUT_GAMEPAD_B);
                            bool32 XButton = (Pad->wButtons & XINPUT_GAMEPAD_X);
                            bool32 YButton = (Pad->wButtons & XINPUT_GAMEPAD_Y);

                            // int16 RStickX = (real32)Pad->sThumbRX;
                            // int16 RStickY = (real32)Pad->sThumbRY;

                        } else {
                            // Controller not available
                        }
                    }

                    DWORD ByteToLock = 0;
                    DWORD PlayCursor = 0;
                    DWORD WriteCurser = 0;
                    DWORD TargetCursor = 0;
                    DWORD BytesToWrite = 0;
                    bool32 SoundIsValid =
                        SUCCEEDED(GlobalSecondaryBuffer->GetCurrentPosition(
                            &PlayCursor, &WriteCurser));

                    if (SoundIsValid) {
                        ByteToLock = ((SoundOutput.RunningSampleIndex *
                                       SoundOutput.BytesPerSample) %
                                      SoundOutput.SecondaryBufferSize);
                        TargetCursor = ((PlayCursor + (SoundOutput.LatencySampleCount *
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

                    // NOTE: DIRECTSOUND ouput test
                    if (SoundIsValid) {
                        Win32FillSoundBuffer(&SoundOutput, ByteToLock, BytesToWrite,
                                             &SoundBuffer);
                    }

                    win32_window_dimension Dimension = Win32GetWindowDimension(Window);
                    Win32CopyBufferToWindow(&GlobalBackbuffer, DeviceContext,
                                            Dimension.Width, Dimension.Height);

                    LARGE_INTEGER EndCounter;
                    QueryPerformanceCounter(&EndCounter);

                    // TODO: Display value here
                    int64 EndCycleCount = __rdtsc();
                    int64 CyclesEllapsed = EndCycleCount - LastCycleCount;

                    int64 CounterEllapsed = EndCounter.QuadPart - LastCounter.QuadPart;
                    real32 MSPerFrame = (real32)((1000.0f * (real32)CounterEllapsed) /
                                                 (real32)PerfCountFrequency);
                    real32 FPS = (real32)PerfCountFrequency / (real32)CounterEllapsed;
                    real32 MCPF = (real32)((real32)CyclesEllapsed /
                                           (1000.0f * 1000.0f)); // Mega cycles per frame

#if 0
                    char Buffer[256];
                    sprintf(Buffer, "%.02fms/f, %.02ff/s, %.02fmc/f\n", MSPerFrame, FPS,
                            MCPF);
                    OutputDebugStringA(Buffer);
#endif

                    LastCycleCount = EndCycleCount;
                    LastCounter = EndCounter;

                    game_input *Temp = NewInput;
                    NewInput = OldInput;
                    OldInput = Temp;
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
