// -*- lsst-c++ -*-

#include "handmade.h"

#include <math.h>

#include "HandmadeDef.h"

internal void
RenderGradient(game_offscreen_buffer *Buffer, int XOffset, int YOffset) {
    uint8 *Row = (uint8 *)Buffer->Memory;

    for (int Y = 0; Y < Buffer->Height; ++Y) {
        uint32 *Pixel = (uint32 *)Row;
        for (int X = 0; X < Buffer->Width; ++X) {
            /*
            LITTLE ENDIAN ARCHITECTURE
            0xBBGGRRxx
            */
            uint8 Blue = (uint8)(((uint8)X) + XOffset);
            uint8 Green = (uint8)((uint8)Y + YOffset);
            uint8 Red = (uint8)((((uint8)X + (uint8)XOffset) * ((uint8)Y + (uint8)YOffset)) % 255);

            *Pixel = ((Red << 16) | (Green << 8) | Blue);
            ++Pixel;
        }
        Row += Buffer->Pitch;
    }
}

internal void
GameOutputSound(game_sound_output_buffer *SoundBuffer, int ToneHz) {
    local_persist real32 tSine;
    int                  SampleIndex;
    int16                ToneVolume = 3000;
    int                  WavePeriod = SoundBuffer->SamplesPerSecond / ToneHz;

    int16 *SampleOut = (int16 *)SoundBuffer->Samples;
    for (SampleIndex = 0; SampleIndex < SoundBuffer->SampleCount; ++SampleIndex) {
        real32 SineValue = sinf(tSine);
        int16  SampleValue = (int16)(SineValue * ToneVolume);
        *SampleOut++ = SampleValue;
        *SampleOut++ = SampleValue;
        tSine += ((2.0f * Pi32 * 1.0f) / (real32)WavePeriod);
    }
}

internal void
GameUpdateAndRender(game_memory *Memory, game_input *Input, game_offscreen_buffer *Buffer,
                    game_sound_output_buffer *SoundBuffer) {
    Assert((&Input->Controllers[0].HastaLaVistaBaby - &Input->Controllers[0].Buttons[0]) ==
           (ArrayCount(Input->Controllers[0].Buttons)));
    Assert(sizeof(game_state) <= Memory->PermanentStorageSize);

    game_state *GameState = (game_state *)Memory->PermanentStorage;
    if (!Memory->IsInitialized) {
        char                  *FileName = __FILE__;
        debug_read_file_result File = DEBUGPlatformReadEntireFile(FileName);
        if (File.Contents) {
            DEBUGPlatformWriteEntireFile("w:/data/test.out", File.ContentsSize, File.Contents);
            DEBUGPlatformFreeFileMemory(File.Contents);
        }

        GameState->XOffset = 0;
        GameState->YOffset = 0;
        GameState->ToneHz = 256;

        Memory->IsInitialized = true;
    }

    for (int ControllerIndex = 0; ControllerIndex < ArrayCount(Input->Controllers);
         ++ControllerIndex) {
        game_controller_input *Controller = &Input->Controllers[ControllerIndex];
        if (Controller->IsAnalog) {
            // Tune to analog movement
            GameState->ToneHz = 256 + (int)(128.0f * Controller->StickAverageX);
            GameState->YOffset += (int)(4.0f * (Controller->StickAverageY));
        } else {
            // Tune to digital movement
        }

        if (Controller->ActionDown.EndedDown) {
            GameState->XOffset++;
        }
    }

    GameOutputSound(SoundBuffer, GameState->ToneHz);
    RenderGradient(Buffer, GameState->XOffset, GameState->YOffset);
}
