#include "HandmadeDef.h"

#if !defined(HANDMADE_H)
#define HANDMADE_H

// TODO: Services that the platform layer provides to the game

/* NOTE: Services that the game provides to the platform layer
 * (this may expand in the future - sound on separate thread etc.)
 */

/**
 * Update and render, takes four things
 *
 * @param[in] Timing
 * @param[in] Input Controller/KeyboardInput
 * @param[out] Bitmap the thing to draw to
 * @param[out] Sound Sound buffer to fill
 */
struct game_offscreen_buffer {
  // NOTE: Pixels are alwasy 32-bits wide, Memory Order BB GG RR XX
  void *Memory;
  int Width;
  int Height;
  int Pitch;
};
internal void GameUpdateAndRender(game_offscreen_buffer *Buffer, int XOffset,
                                  int YOffset);

#endif // HANDMADE_H
