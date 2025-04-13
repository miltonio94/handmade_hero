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

#include "handmade.h"
#include "handmade.cpp"

#include <dsound.h>
#include <math.h>
#include <stdio.h>
#include <windef.h> // including as I'm using my personal emacs config on WLS and this removes a lot of the squigly reds when .ccls is configured properly
#include <windows.h>
#include <xinput.h>

struct win32_offscreen_buffer {
  // NOTE: Pixels are alwasy 32-bits wide, Memory Order BB GG RR XX
  BITMAPINFO Info;
  void *Memory;
  int Width;
  int Height;
  int Pitch;
};

struct win32_window_dimension {
  int Width;
  int Height;
};

global_variable bool32 GlobalRunning;
global_variable LPDIRECTSOUNDBUFFER GlobalSecondaryBuffer;
global_variable win32_offscreen_buffer GlobalBackbuffer;

#define MapRange(InMin, InMax, OutMin, OutMax, Val)                            \
  (OutMin + (Val - InMin) * (OutMax - OutMin) / (InMax - InMin))

#define X_INPUT_GET_STATE(name)                                                \
  DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub) { return ERROR_DEVICE_NOT_CONNECTED; }
global_variable x_input_get_state *XInputGetState_ = XInputGetStateStub;

#define X_INPUT_SET_STATE(name)                                                \
  DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub) { return ERROR_DEVICE_NOT_CONNECTED; }
global_variable x_input_set_state *XInputSetState_ = XInputSetStateStub;

#define XInputGetState XInputGetState_
#define XInputSetState XInputSetState_

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

#define DIRECT_SOUND_CREATE(name)                                              \
  HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS,               \
                      LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(direct_sound_create);

internal void Win32InitDSound(HWND Window, int32 SamplesPerSecond,
                              int32 BufferSize) {
  // NOTE Load lib
  HMODULE DSoundLibrary = LoadLibrary("dsound.dll");
  if (DSoundLibrary) {

    // NOTE Get direct sound object
    direct_sound_create *DirectSoundCreate =
        (direct_sound_create *)GetProcAddress(DSoundLibrary,
                                              "DirectSoundCreate");
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

      if (SUCCEEDED(DirectSound->CreateSoundBuffer(
              &BufferDescription, &GlobalSecondaryBuffer, 0))) {
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

  int BitmapMemorySize = (Width * Height) * BytesPerPixel;
  Buffer->Memory =
      VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);
  Buffer->Pitch = Buffer->Width * BytesPerPixel; // Size of Row

  // TODO Clear to black, maybe
}

internal void Win32CopyBufferToWindow(win32_offscreen_buffer *Buffer,
                                      HDC DeviceContext, int WindowWidth,
                                      int WindowHeight) {

  // TODO: Aspect ratio correction
  StretchDIBits(
      DeviceContext,
      // X, Y, Width, Height, // dest
      // X, Y, Width, Height, // src
      0, 0, WindowWidth, WindowHeight, 0, 0, Buffer->Width, Buffer->Height,
      Buffer->Memory, &Buffer->Info,
      DIB_RGB_COLORS, // Colour type, either pallet or RGB, in this case RGB
      SRCCOPY);       // hows to rasterise when upscalling, we just want to copy
}

LRESULT CALLBACK Win32MainWindowCallback(HWND Window, UINT Message,
                                         WPARAM WParam, LPARAM LParam) {
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
    // OutputDebugStringA("Default\n");
    Result = DefWindowProcA(Window, Message, WParam, LParam);
  } break;
  }

  return Result;
}
struct win32_sound_output {
  int SamplesPerSeconds;
  int ToneHz;
  int ToneVolume;
  uint32 RunningSampleIndex;
  int WavePeriod;
  int BytesPerSample;
  int SecondaryBufferSize;
  real32 tSine;
  int LatencySampleCount;
};

internal void Win32FillSoundBuffer(win32_sound_output *SoundBuffer,
                                   DWORD ByteToLock, DWORD BytesToWrite) {
  VOID *Region1;
  DWORD Region1Size;
  VOID *Region2;
  DWORD Region2Size;

  HRESULT SecondaryBufferLockResult =
      GlobalSecondaryBuffer->Lock(ByteToLock, BytesToWrite, &Region1,
                                  &Region1Size, &Region2, &Region2Size, 0);
  if (SUCCEEDED(SecondaryBufferLockResult)) {
    // TODO: Assert that Region1Size and Region2Size are valid
    DWORD Region1SampleCount = Region1Size / SoundBuffer->BytesPerSample;
    int16 *SampleOut = (int16 *)Region1;
    DWORD SampleIndex;
    int16 SampleValue;
    real32 SineValue;
    real32 t;
    for (SampleIndex = 0; SampleIndex < Region1SampleCount; ++SampleIndex) {
      SineValue = sinf(SoundBuffer->tSine);
      SampleValue = (int16)(SineValue * SoundBuffer->ToneVolume);
      *SampleOut++ = SampleValue;
      *SampleOut++ = SampleValue;

      SoundBuffer->tSine +=
          ((2.0f * Pi32 * 1.0f) / (real32)SoundBuffer->WavePeriod);
      ++SoundBuffer->RunningSampleIndex;
    }
    DWORD Region2SampleCount = Region2Size / SoundBuffer->BytesPerSample;
    SampleOut = (int16 *)Region2;
    for (SampleIndex = 0; SampleIndex < Region2SampleCount; ++SampleIndex) {
      SineValue = sinf(SoundBuffer->tSine);
      SampleValue = (int16)(SineValue * SoundBuffer->ToneVolume);
      *SampleOut++ = SampleValue;
      *SampleOut++ = SampleValue;

      SoundBuffer->tSine +=
          ((2.0f * Pi32 * 1.0f) / (real32)SoundBuffer->WavePeriod);
      ++SoundBuffer->RunningSampleIndex;
    }
    GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
  }
}

int WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CmdLine,
            int ShowCode) {

  LARGE_INTEGER PerfCountFrequencyResult;
  QueryPerformanceFrequency(&PerfCountFrequencyResult);
  uint64 PerfCountFrequency = PerfCountFrequencyResult.QuadPart;

  Win32LoadXInput();

  WNDCLASS WindowClass = {};

  Win32ResizeDIBSection(&GlobalBackbuffer, 1280, 720);

  WindowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  WindowClass.lpfnWndProc = Win32MainWindowCallback;
  WindowClass.hInstance = Instance;
  // HICON     hIcon,
  WindowClass.lpszClassName = "Handmade Hero";

  if (RegisterClass(&WindowClass)) {
    HWND Window = CreateWindowExA(0, WindowClass.lpszClassName, "Handmade hero",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                  CW_USEDEFAULT, 0, 0, Instance, 0);

    if (Window) {
      HDC DeviceContext = GetDC(Window);

      // GRAPHICS TEST
      int XOffset = 0;
      int YOffset = 0;

      // NOTE: SOUND TEST
      win32_sound_output SoundOutput = {};
      SoundOutput.SamplesPerSeconds = 48000;
      SoundOutput.ToneHz = 256;
      SoundOutput.ToneVolume = 3000;
      SoundOutput.WavePeriod =
          SoundOutput.SamplesPerSeconds / SoundOutput.ToneHz;
      SoundOutput.BytesPerSample = sizeof(int16) * 2;
      SoundOutput.SecondaryBufferSize =
          SoundOutput.SamplesPerSeconds * SoundOutput.BytesPerSample;
      SoundOutput.LatencySampleCount = SoundOutput.SamplesPerSeconds / 15;

      Win32InitDSound(Window, SoundOutput.SamplesPerSeconds,
                      SoundOutput.SecondaryBufferSize);
      Win32FillSoundBuffer(&SoundOutput, 0, SoundOutput.LatencySampleCount);
      GlobalSecondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);

      GlobalRunning = true;

      LARGE_INTEGER LastCounter;
      QueryPerformanceCounter(&LastCounter);

      int64 LastCycleCount = __rdtsc();

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
        for (DWORD ControllerIndex = 0; ControllerIndex < XUSER_MAX_COUNT;
             ControllerIndex++) {

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

            int16 LStickX = Pad->sThumbLX;
            int16 LStickY = Pad->sThumbLY;
            int16 RStickX = Pad->sThumbRX;
            int16 RStickY = Pad->sThumbRY;

            SoundOutput.ToneHz =
                512 + (int)(256.0f * ((real32)RStickY / 30000.0f));
            SoundOutput.WavePeriod =
                SoundOutput.SamplesPerSeconds / SoundOutput.ToneHz;

            XOffset += -(LStickX / 4096);
            YOffset += (LStickY / 4096);

            if (Up) {
              ++YOffset;
            }
            if (Down) {
              --YOffset;
            }
            if (Left) {
              ++XOffset;
            }
            if (Right) {
              --XOffset;
            }
          } else {
            // Controller not available
          }
        }

        XINPUT_VIBRATION Vibration;
        Vibration.wLeftMotorSpeed = 6;
        Vibration.wRightMotorSpeed = 6;
        XInputSetState(0, &Vibration);

        game_offscreen_buffer Buffer = {};
        Buffer.Memory = GlobalBackbuffer.Memory;
        Buffer.Width = GlobalBackbuffer.Width;
        Buffer.Height = GlobalBackbuffer.Height;
        Buffer.Pitch = GlobalBackbuffer.Pitch;
        GameUpdateAndRender(&Buffer, XOffset, YOffset);

        // NOTE: DIRECTSOUND ouput test
        DWORD PlayCursor;
        DWORD WriteCurser;
        DWORD TargetCursor;
        HRESULT SecondaryBufferGetCurrentPositionStatus =
            GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor,
                                                      &WriteCurser);
        if (SUCCEEDED(SecondaryBufferGetCurrentPositionStatus)) {
          DWORD ByteToLock =
              ((SoundOutput.RunningSampleIndex * SoundOutput.BytesPerSample) %
               SoundOutput.SecondaryBufferSize);
          TargetCursor = ((PlayCursor + (SoundOutput.LatencySampleCount *
                                         SoundOutput.BytesPerSample)) %
                          SoundOutput.SecondaryBufferSize);
          DWORD BytesToWrite = 0;
          if (ByteToLock > TargetCursor) {
            BytesToWrite = SoundOutput.SecondaryBufferSize - ByteToLock;
            BytesToWrite += TargetCursor;
          } else {

            BytesToWrite = TargetCursor - ByteToLock;
          }

          Win32FillSoundBuffer(&SoundOutput, ByteToLock, BytesToWrite);
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
      }
    } else {
      // TODO: Logging
    }
  } else {
    // TODO: Logging
  };

  return 0;
}
