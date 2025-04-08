#include <stdint.h>
#include <windows.h>
#include <xinput.h>

#define local_persist static
#define global_variable static
#define internal static

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

global_variable bool GlobalRunning;

struct win32_offscreen_buffer {
	// NOTE(casey): Pixels are alwasy 32-bits wide, Memory Order BB GG RR XX
	BITMAPINFO Info;
	void *Memory;
	int Width;
	int Height;
	int Pitch;
	int BytesPerPixel;
};
global_variable win32_offscreen_buffer GlobalBackbuffer;

struct win32_window_dimension {
	int Width;
	int Height;
};

#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE* pState ) 
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub) 
{
	return 0;
}
global_variable x_input_get_state *XInputGetState_ = XInputGetStateStub;

#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration )
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub) {
	return 0;
}
global_variable x_input_set_state *XInputSetState_ = XInputSetStateStub;


#define XInputGetState XInputGetState_
#define XInputSetState XInputSetState_

internal void Win32LoadXInput() {
	HMODULE XInputLibrary = LoadLibraryA("xinput1_3.dll");
	if (XInputLibrary) {
		XInputGetState = (x_input_get_state *) GetProcAddress(XInputLibrary, "XInputGetState");
		XInputSetState = (x_input_set_state *) GetProcAddress(XInputLibrary, "XInputSetState");
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

internal void RenderGradient(win32_offscreen_buffer *Buffer, int XOffset,
							 int YOffset) {
	uint8 *Row = (uint8 *)Buffer->Memory;

	for (int Y = 0; Y < Buffer->Height; ++Y) {
		uint32 *Pixel = (uint32 *)Row;
		for (int X = 0; X < Buffer->Width; ++X) {
			/*
         LITTLE ENDIAN ARCHITECTURE

         0xBBGGRRxx

       */

			uint8 Blue = ((uint8)X) + XOffset;
			uint8 Green = (uint8)Y + YOffset;
			uint8 Red =
				(((uint8)X + (uint8)XOffset) * ((uint8)Y + (uint8)YOffset)) % 255;

			*Pixel = ((Red << 16) | (Green << 8) | Blue);
			++Pixel;
		}
		Row += Buffer->Pitch;
	}
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
	Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);
	Buffer->Pitch = Buffer->Width * BytesPerPixel; // Size of Row

	// TODO Clear to black, maybe
}

internal void Win32CopyBufferToWindow(win32_offscreen_buffer *Buffer, HDC DeviceContext,
									  int WindowWidth, int WindowHeight ) {

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
			bool WasDown = ( LParam & (1 << 30)) != 0;
			bool IsDown = ((LParam & (1 << 31)) == 0);

			if (WasDown != IsDown) {
				if (VKCode == 'W') {
				}
				if (VKCode == 'A') {
				}
				if (VKCode == 'S') {
				}
				if (VKCode == 'D') {
				}
				if (VKCode == 'Q') {
				}
				if (VKCode == 'E') {
				}
				if (VKCode == VK_UP) {
				}
				if (VKCode == VK_DOWN) {
				}
				if (VKCode == VK_LEFT) {
				}
				if (VKCode == VK_RIGHT) {
				}
				if (VKCode == VK_ESCAPE) {
					OutputDebugStringA("Escape\n");
					if (IsDown) {
						OutputDebugStringA("is down\n");
					}
					if (WasDown) {
						OutputDebugStringA("was down\n");
					}
				}
				if (VKCode == VK_SPACE) {
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
			Win32CopyBufferToWindow(&GlobalBackbuffer, DeviceContext, 
									Dimension.Width, Dimension.Height );
			EndPaint(Window, &Paint);
		} break;

		default: {
			// OutputDebugStringA("Default\n");
			Result = DefWindowProcA(Window, Message, WParam, LParam);
		} break;
	}

	return Result;
}

int WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CmdLine,
			int ShowCode) {

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

			int XOffset = 0;
			int YOffset = 0;
			GlobalRunning = true;

			while (GlobalRunning) {
				MSG Message;
				while (PeekMessageA(&Message, 0, 0, 0, PM_REMOVE)) {
					if (Message.message == WM_QUIT) {
						GlobalRunning = false;
					}

					TranslateMessage(&Message);
					DispatchMessageA(&Message);
				}

				// TODO Should this be polled more often
				for (DWORD ControllerIndex = 0; 
					ControllerIndex < XUSER_MAX_COUNT;
					ControllerIndex++) {

					XINPUT_STATE  ControllerState;
					if (XInputGetState(ControllerIndex, &ControllerState)) {
						// Controller plugged in
						// TODO: See if increments too rapidly \/
						// ControllerState.dwPacketNumber;
						XINPUT_GAMEPAD *Pad = &ControllerState.Gamepad;

						bool Up = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP);
						bool Down = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
						bool Left = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
						bool Right = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
						bool Start = (Pad->wButtons & XINPUT_GAMEPAD_START);
						bool Back = (Pad->wButtons & XINPUT_GAMEPAD_BACK);
						bool LeftShoulder = (Pad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
						bool RightShoulder = (Pad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
						bool AButton = (Pad->wButtons & XINPUT_GAMEPAD_A);
						bool BButton = (Pad->wButtons & XINPUT_GAMEPAD_B);
						bool XButton = (Pad->wButtons & XINPUT_GAMEPAD_X);
						bool YButton = (Pad->wButtons & XINPUT_GAMEPAD_Y);

						int16 StickX = Pad->sThumbLX;
						int16 StickY = Pad->sThumbLY;

						if (AButton) {
							++YOffset;
						}
					} else {
						// Controller not available
					}
				}

				XINPUT_VIBRATION Vibration;
				Vibration.wLeftMotorSpeed = 65500;
				Vibration.wRightMotorSpeed = 65500;
				XInputSetState(0, &Vibration);

				RenderGradient(&GlobalBackbuffer, XOffset, YOffset);

				win32_window_dimension Dimension = Win32GetWindowDimension(Window);
				Win32CopyBufferToWindow(&GlobalBackbuffer, DeviceContext, 
										Dimension.Width, Dimension.Height);
				++XOffset;
			}
		} else {
			// TODO: Logging
		}
	} else {
		// TODO: Logging
	};

	return 0;
}
