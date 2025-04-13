#include "handmade.h"

internal void RenderGradient(game_offscreen_buffer *Buffer, int XOffset,
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

internal void GameUpdateAndRender(game_offscreen_buffer *Buffer, int XOffset,
                                  int YOffset) {

  RenderGradient(Buffer, XOffset, YOffset);
}
