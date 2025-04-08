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

win32_window_dimension Win32GetWindowDimension(HWND Window) {
	win32_window_dimension Result;

	RECT ClientRect;
	GetClientRect(Window, &ClientRect);

	Result.Width = ClientRect.right - ClientRect.left;
	Result.Height = ClientRect.bottom - ClientRect.top;

	return Result;
}

internal void RenderGradient(win32_offscreen_buffer Buffer, int XOffset,
							 int YOffset) {
	uint8 *Row = (uint8 *)Buffer.Memory;

	for (int Y = 0; Y < Buffer.Height; ++Y) {
		uint32 *Pixel = (uint32 *)Row;
		for (int X = 0; X < Buffer.Width; ++X) {
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
		Row += Buffer.Pitch;
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

internal void Win32CopyBufferToWindow(HDC DeviceContext,
									  int WindowWidth, int WindowHeight,
									  win32_offscreen_buffer Buffer) {

	// TODO: Aspect ratio correction
	StretchDIBits(
		DeviceContext,
		// X, Y, Width, Height, // dest
		// X, Y, Width, Height, // src
		0, 0, WindowWidth, WindowHeight, 0, 0, Buffer.Width, Buffer.Height,
		Buffer.Memory, &Buffer.Info,
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

		case WM_ACTIVATEAPP: {
			OutputDebugStringA("Active\n");
		} break;

		case WM_PAINT: {
			PAINTSTRUCT Paint;
			HDC DeviceContext = BeginPaint(Window, &Paint);
			win32_window_dimension Dimension = Win32GetWindowDimension(Window);
			Win32CopyBufferToWindow(DeviceContext, Dimension.Width, Dimension.Height,
									GlobalBackbuffer);
			EndPaint(Window, &Paint);
		} break;

		default: {
			// OutputDebugString("Default\n");
			Result = DefWindowProc(Window, Message, WParam, LParam);
		} break;
	}

	return Result;
}

int WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CmdLine,
			int ShowCode) {
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

			CreateWindowExA(
				0, WindowClass.lpszClassName, "Handmade Hero",
				WS_OVERLAPPEDWINDOW | WS_VISIBLE,
				CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
				0, 0, Instance, 0);

			int XOffset = 0;
			int YOffset = 0;
			GlobalRunning = true;

			while (GlobalRunning) {
				MSG Message;
				while (PeekMessage(&Message, 0, 0, 0, PM_REMOVE)) {
					if (Message.message == WM_QUIT) {
						GlobalRunning = false;
					}

					TranslateMessage(&Message);
					DispatchMessage(&Message);
				}

				// TODO Should this be polled more often
				for (DWORD ControllerIndex = 0; 
					ControllerIndex < XUSER_MAX_COUNT;
					ControllerIndex++) {

					XINPUT_STATE = ControllerState;
					if (XInputGetState(ControllerIndex, &ControllerState)) {
						// Controller plugged in
					} else {
						// Controller not available
					}
				}

				RenderGradient(GlobalBackbuffer, XOffset, YOffset);

				win32_window_dimension Dimension = Win32GetWindowDimension(Window);
				Win32CopyBufferToWindow(DeviceContext, Dimension.Width,
										Dimension.Height, GlobalBackbuffer);
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
