#include "handmade.h"
#include "HandmadeDef.h"
#include <math.h>

internal void RenderGradient(game_offscreen_buffer *Buffer, int XOffset, int YOffset) {
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
            uint8 Red = (((uint8)X + (uint8)XOffset) * ((uint8)Y + (uint8)YOffset)) % 255;

            *Pixel = ((Red << 16) | (Green << 8) | Blue);
            ++Pixel;
        }
        Row += Buffer->Pitch;
    }
}

internal void GameOutputSound(game_sound_output_buffer *SoundBuffer, int ToneHz) {
    local_persist real32 tSine;
    int SampleIndex;
    int16 ToneVolume = 3000;
    int WavePeriod = SoundBuffer->SamplesPerSecond / ToneHz;

    int16 *SampleOut = (int16 *)SoundBuffer->Samples;
    for (SampleIndex = 0; SampleIndex < SoundBuffer->SampleCount; ++SampleIndex) {
        real32 SineValue = sinf(tSine);
        int16 SampleValue = (int16)(SineValue * ToneVolume);
        *SampleOut++ = SampleValue;
        *SampleOut++ = SampleValue;
        tSine += ((2.0f * Pi32 * 1.0f) / (real32)WavePeriod);
    }
}

internal void GameUpdateAndRender(game_memory *Memory, game_input *Input,
                                  game_offscreen_buffer *Buffer,
                                  game_sound_output_buffer *SoundBuffer) {
    game_state *GameState = (game_state *)Memory->PermanentStorage;
    if (!Memory->IsInitialized) {
        GameState->XOffset = 0;
        GameState->YOffset = 0;
        GameState->ToneHz = 256;

        Memory->IsInitialized = true;
    }

    game_controller_input *Input0 = &Input->Controllers[0];
    if (Input0->IsAnalog) {
        // Tune to analog movement
        GameState->ToneHz = 256 + (int)(128.0f * (Input0->EndX));
        GameState->YOffset += (int)4.0f * (Input0->EndY);
    } else {
        // Tune to digital movement
    }

    if (Input0->Down.EndedDown) {
        GameState->XOffset++;
    }

    GameOutputSound(SoundBuffer, GameState->ToneHz);
    RenderGradient(Buffer, GameState->XOffset, GameState->YOffset);
}
