#include <windows.h>

#define local_persist static
#define global_variable static
#define internal static

global_variable bool Running;
global_variable BITMAPINFO BitmapInfo;
global_variable void *BitmapMemory;
global_variable HBITMAP BitmapHandle;
global_variable HDC BitmapDeviceContext;

internal void Win32ResizeDIBSection(int Width, int Height){
    // TODO: MAKE BETTER

    if(BitmapHandle){
        DeleteObject(BitmapHandle);
    }

    if(!BitmapDeviceContext){
        // Should this be recreated in certain context
        BitmapDeviceContext = CreateCompatibleDC(0);
    }

    BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader) ;
    BitmapInfo.bmiHeader.biWidth = Width;
    BitmapInfo.bmiHeader.biHeight = Height;
    BitmapInfo.bmiHeader.biPlanes = 1;
    BitmapInfo.bmiHeader.biBitCount = 32;
    BitmapInfo.bmiHeader.biCompression = BI_RGB;
    BitmapInfo.bmiHeader.biSizeImage = 0;
    BitmapInfo.bmiHeader.biXPelsPerMeter = 0;
    BitmapInfo.bmiHeader.biYPelsPerMeter = 0;
    BitmapInfo.bmiHeader.biClrUsed = 0;
    BitmapInfo.bmiHeader.biClrImportant = 0;


    BitmapHandle = CreateDIBSection(BitmapDeviceContext,
                     &BitmapInfo,
                     DIB_RGB_COLORS,
                     &BitmapMemory,
                     0, 0);

}

internal void Win32UpdateWindow(HDC DeviceContext, int X, int Y, int Width, int Height){
    StretchDIBits(DeviceContext,
                  X, Y, Width, Height, // src
                  X, Y, Width, Height, // dest
                  &BitmapMemory,
                  &BitmapInfo,
                  DIB_RGB_COLORS, // Colour type, either pallet or RGB, in this case RGB
                  SRCCOPY); // hows to rasterise when upscalling, we just want to copy

}

LRESULT CALLBACK Win32MainWindowCallback(
  HWND Window,
  UINT Message,
  WPARAM WParam,
  LPARAM LParam
){
    LRESULT Result = 0;
    switch (Message) {
        case WM_SIZE: {
            RECT ClientRect;
            GetClientRect(Window, &ClientRect);
            int Width = ClientRect.right - ClientRect.left;
            int Height = ClientRect.bottom - ClientRect.top;
            Win32ResizeDIBSection(Width, Height);
            OutputDebugString("Sizing\n");
        } break;

        case WM_DESTROY: {
            // TODO Handle this with an error
            Running = false;
        } break;

        case WM_CLOSE: {
            // TODO Handle with message
            Running = false;
            OutputDebugString("Close\n");
        } break;

        case WM_ACTIVATEAPP: {
            OutputDebugString("Active\n");
        } break;

        case WM_PAINT: {
            PAINTSTRUCT Paint;
            HDC  DeviceContext = BeginPaint(Window, &Paint);
            int X = Paint.rcPaint.left;
            int Y = Paint.rcPaint.top;
            int Width = Paint.rcPaint.right - Paint.rcPaint.left;
            int Height = Paint.rcPaint.bottom - Paint.rcPaint.top;
            Win32UpdateWindow(DeviceContext, X, Y, Width, Height);
            EndPaint(Window, &Paint);
        } break;

        default: {
            // OutputDebugString("Default\n");
            Result = DefWindowProc(Window, Message, WParam, LParam);
        } break;
    }

    return Result;
}


int WinMain(HINSTANCE Instance,
                      HINSTANCE PrevInstance,
                      LPSTR CmdLine,
                      int ShowCode){
    WNDCLASS WindowClass = {};

        WindowClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        WindowClass.lpfnWndProc = Win32MainWindowCallback;
        WindowClass.hInstance = Instance;
        // HICON     hIcon,
        WindowClass.lpszClassName = "Handmade Hero";

        if(RegisterClass(&WindowClass)){
            HWND WindowHandle =
                CreateWindowExA(0, WindowClass.lpszClassName, "Handmade hero",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                CW_USEDEFAULT, 0, 0, Instance, 0 );

            if(WindowHandle){
                MSG Message;
                Running = true;
                while(Running){
                    BOOL MessageResult = GetMessage(&Message, 0, 0, 0);
                    if(MessageResult > 0){
                        TranslateMessage(&Message);
                        DispatchMessage(&Message);
                    } else {
                        break;
                    }
                }
            } else {
                // TODO: Logging
            }
        } else {
            // TODO: Logging
        };

    return 0;
}
