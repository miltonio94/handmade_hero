// -*- lsst-c++ -*-

#include "HandmadeDef.h"

#ifndef HANDMADE_H
#define HANDMADE_H

/*
** NOTE:
** HANDMADE_INTERNAL:
**  0 - Release build
**  1 - Dev build
**
** HANDMADE_SLOW:
**  0 - No slow code
**  1 - Slow code okay
*/

#ifdef HANDMADE_INTERNAL
// IMPORTANT:
// These are not to be shipped
// write blockign wont protect file agians data loss
struct debug_read_file_result {
    uint32 ContentsSize;
    void  *Contents;
};
internal debug_read_file_result
DEBUGPlatformReadEntireFile(char *Filename);
internal void
DEBUGPlatformFreeFileMemory(void *BitmapMemory);

internal bool32
DEBUGPlatformWriteEntireFile(char *Filename, uint32 MemorySize, void *Memory);
#endif

#if HANDMADE_SLOW
#define Assert(Expression) \
    if (!(Expression)) {   \
        *(int *)0 = 0;     \
    }
#else
#define Assert(Expression)
#endif

#define Kilobytes(Value) ((Value) * 1024LL)
#define Megabytes(Value) (Kilobytes(Value) * 1024LL)
#define Gigabytes(Value) (Megabytes(Value) * 1024LL)
#define Terabytes(Value) (Gigabytes(Value) * 1024LL)
#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))

inline uint32
SafeTruncateUInt64(uint64 value)
{
    Assert(value <= 0xffffffff);
    return (uint32)value;
}

// TODO: Services that the platform layer provides to the game

/* NOTE: Services that the game provides to the platform layer
 * (this may expand in the future - sound on separate thread etc.)
 */
struct game_sound_output_buffer {
    int   SamplesPerSecond;
    int   SampleCount;
    void *Samples;
};

struct game_offscreen_buffer {
    // NOTE: Pixels are alwasy 32-bits wide, Memory Order BB GG RR XX
    void *Memory;
    int   Width;
    int   Height;
    int   Pitch;
};

struct game_button_state {
    int    HalfTransitionCount;
    bool32 EndedDown;
};

struct game_controller_input {
    bool32 IsAnalog;
    bool32 IsConnected;

    real32 StickAverageX;
    real32 StickAverageY;

    union {
        game_button_state Buttons[12];
        struct {
            game_button_state MoveUp;
            game_button_state MoveDown;
            game_button_state MoveLeft;
            game_button_state MoveRight;

            game_button_state ActionUp;
            game_button_state ActionDown;
            game_button_state ActionLeft;
            game_button_state ActionRight;

            game_button_state LeftShoulder;
            game_button_state RightShoulder;

            game_button_state Back;
            game_button_state Start;

            // NOTE DO NOT ADD BUTTON AFTER THIS LINE!!!
            game_button_state HastaLaVistaBaby;
        };
    };
};

struct game_input {
    game_controller_input Controllers[5];
};

inline game_controller_input *
GetController(game_input *Input, uint32 ControllerIndex)
{
    Assert(ControllerIndex < ArrayCount(Input->Controllers));
    return &Input->Controllers[ControllerIndex];
}

struct game_memory {
    bool32 IsInitialized;
    uint64 PermanentStorageSize;
    void  *PermanentStorage; // REQUIRED to be cleared to 0 at startup
    uint64 TransientStorageSize;
    void  *TransientStorage; // REQUIRED to be cleared to 0 at startup
};

/**
 * Update and render the game
 *
 * @param[out] Buffer to render into
 * @param[out] Sound Sound buffer to fill
 */
internal void
GameUpdateAndRender(game_memory *Memory, game_input *Input, game_offscreen_buffer *Buffer,
                    game_sound_output_buffer *SoundBuffer);

struct game_state {
    int ToneHz;
    int XOffset;
    int YOffset;
};

#endif // HANDMADE_H
