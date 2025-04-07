#include <windows.h>
#include <stdint.h>

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

global_variable bool Running;
global_variable BITMAPINFO BitmapInfo;
global_variable void *BitmapMemory;
global_variable int BitmapWidth;
global_variable int BitmapHeight;
global_variable int BytesPerPixel = 4;

internal void RenderGradient(int XOffset, int YOffset){
    int Width = BitmapWidth;
    int Height = BitmapHeight;
    int Pitch = Width * BytesPerPixel; // Size of Row
    uint8 *Row = (uint8 *)BitmapMemory;

    for(int Y = 0; Y < BitmapHeight; ++Y){
        uint32 *Pixel = (uint32 *) Row;
        for(int X = 0; X < BitmapWidth; ++X){
            /*
               LITTLE ENDIAN ARCHITECTURE

               0xBBGGRRxx

             */

            uint8 Blue = ((uint8)X )+ XOffset;
            uint8 Green = (uint8)Y + YOffset;
            uint8 Red = (((uint8)X + (uint8)XOffset ) * ((uint8)Y +  (uint8)YOffset)) % 255;

            *Pixel = ((Red << 16) | (Green << 8) | Blue);
            ++Pixel;

        }
        Row += Pitch;
    }
}

internal void Win32ResizeDIBSection(int Width, int Height){
    // TODO: MAKE BETTER

    if(BitmapMemory){
        VirtualFree(BitmapMemory, 0, MEM_RELEASE);
    }

    BitmapWidth = Width;
    BitmapHeight = Height;

    BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader) ;
    BitmapInfo.bmiHeader.biWidth = BitmapWidth;
    BitmapInfo.bmiHeader.biHeight = -BitmapHeight;
    BitmapInfo.bmiHeader.biPlanes = 1;
    BitmapInfo.bmiHeader.biBitCount = 32;
    BitmapInfo.bmiHeader.biCompression = BI_RGB;

    int BitmapMemorySize = (Width * Height) * BytesPerPixel;
    BitmapMemory = VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);

    // TODO Clear to black, maybe
}

internal void Win32UpdateWindow(HDC DeviceContext, RECT *ClientRect, int X, int Y, int Width, int Height){
    int WindowWidth =  ClientRect->right - ClientRect->left;
    int WindowHeight = ClientRect->bottom - ClientRect->top;

    StretchDIBits(DeviceContext,
                  // X, Y, Width, Height, // src
                  // X, Y, Width, Height, // dest
                  0, 0, BitmapWidth, BitmapHeight,
                  0, 0, WindowWidth, WindowHeight,
                  BitmapMemory,
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
        } break;

        case WM_DESTROY: {
            // TODO Handle this with an error
            Running = false;
        } break;

        case WM_CLOSE: {
            // TODO Handle with message
            Running = false;
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

            RECT ClientRect;
            GetClientRect(Window, &ClientRect);

            Win32UpdateWindow(DeviceContext, &ClientRect, X, Y, Width, Height);
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
            HWND Window =
                CreateWindowExA(0, WindowClass.lpszClassName, "Handmade hero",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                CW_USEDEFAULT, 0, 0, Instance, 0 );

            if(Window){
                int XOffset = 0;
                int YOffset = 0;
                Running = true;
                while(Running){
                    MSG Message;
                    while(PeekMessage(&Message, 0, 0, 0, PM_REMOVE)){
                        if(Message.message == WM_QUIT){
                            Running = false;
                        }

                        TranslateMessage(&Message);
                        DispatchMessage(&Message);
                    }
                    RenderGradient(XOffset, YOffset);

                    HDC DeviceContext = GetDC(Window);
                    RECT ClientRect;
                    GetClientRect(Window, &ClientRect);
                    int WindowWidth =  ClientRect.right - ClientRect.left;
                    int WindowHeight = ClientRect.bottom - ClientRect.top;
                    Win32UpdateWindow(DeviceContext, &ClientRect, 0, 0, WindowWidth, WindowHeight);
                    ReleaseDC(Window, DeviceContext);
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
