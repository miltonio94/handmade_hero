#include "HandmadeDef.h"

#if !defined(HANDMADE_H)
#define HANDMADE_H

// TODO: Services that the platform layer provides to the game

/* NOTE: Services that the game provides to the platform layer
 * (this may expand in the future - sound on separate thread etc.)
 */
struct game_sound_output_buffer {
    int SamplesPerSecond;
    int SampleCount;
    void *Samples;
};

struct game_offscreen_buffer {
    // NOTE: Pixels are alwasy 32-bits wide, Memory Order BB GG RR XX
    void *Memory;
    int Width;
    int Height;
    int Pitch;
};

struct game_button_state {
    int HalfTransitionCount;
    bool32 EndedDown;
};

struct game_controller_input {
    bool32 IsAnalog;

    real32 StartX;
    real32 StartY;

    real32 MinX;
    real32 MinY;

    real32 MaxX;
    real32 MaxY;

    real32 EndX;
    real32 EndY;
    union {
        game_button_state Buttons[6];
        struct {
            game_button_state Up;
            game_button_state Down;
            game_button_state Left;
            game_button_state Right;
            game_button_state LeftShoulder;
            game_button_state RightShoulder;
        };
    };
};

struct game_memory {
    bool32 IsInitialized;
    uint64 PermanentStorageSize;
    void *PermanentStorage; // REQUIRED to be cleared to 0 at startup
    uint64 TransientStorageSize;
    void *TransientStorage; // REQUIRED to be cleared to 0 at startup
};

struct game_input {
    game_controller_input Controllers[4];
};
/**
 * Update and render the game
 *
 * @param[out] Buffer to render into
 * @param[out] Sound Sound buffer to fill
 */
internal void GameUpdateAndRender(game_memory *Memory, game_input *Input,
                                  game_offscreen_buffer *Buffer,
                                  game_sound_output_buffer *SoundBuffer);

struct game_state {
    int ToneHz;
    int XOffset;
    int YOffset;
};

#endif // HANDMADE_H
