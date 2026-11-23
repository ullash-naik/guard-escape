/*
 * audio.c - High-quality procedural sound effects & adaptive music for Guard Escape.
 *
 * All audio is synthesized in memory into 44.1kHz 16-bit PCM waves, loaded
 * directly through raylib's audio device. Zero external asset dependencies.
 *
 * Features:
 *   - Soothing, ambient multi-chord homescreen music on the menus
 *   - Rhythmic footsteps with randomized pitch variations when running
 *   - Dynamic danger music & accelerating heartbeat when guards are close/chasing
 *   - Guard detection alert sting
 *   - Golden key pickup chime & triumphant exit unlock fanfare
 *   - Caught & victory stingers
 */
#include "audio.h"
#include "game.h"
#include "raylib.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 44100
#ifndef PI
#define PI 3.14159265358979323846f
#endif

static Sound sndMenuMusic;
static Sound sndFootsteps[4];
static Sound sndSprintSteps[4];
static Sound sndHeartbeat;
static Sound sndDangerDrone;
static Sound sndAlert;
static Sound sndKey;
static Sound sndAllKeys;
static Sound sndCaught;
static Sound sndWin;
static Sound sndClick;
static Sound sndCoin;
static Sound sndLocker;
static Sound sndSpotlight;
static Sound sndExhausted;
static Sound sndDartFire;
static Sound sndDartHit;
static Sound sndFlash;
static Sound sndPickup;

static int audioReady = 0;
static float stepTimer = 0.0f;
static int stepIndex = 0;
static float heartTimer = 0.0f;
static float menuMusicFade = 1.0f;

/* ------------------------------------------------------------------ */
/* Waveform synthesis helpers                                          */
/* ------------------------------------------------------------------ */
static Wave create_wave(int frames)
{
    Wave w = { 0 };
    w.frameCount = (unsigned int)frames;
    w.sampleRate = SAMPLE_RATE;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = (short *)calloc((size_t)frames, sizeof(short));
    return w;
}

static inline float frand(void)
{
    return (float)rand() / (float)RAND_MAX;
}

static inline float clamp_f(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ------------------------------------------------------------------ */
/* 1. Homescreen Soothing Ambient Music                                */
/* Warm, relaxing ambient synth pad in Dm9 -> G9 -> Cmaj9 -> Am9      */
/* ------------------------------------------------------------------ */
static Sound gen_menu_music(void)
{
    float totalSec = 8.0f;
    int frames = (int)(SAMPLE_RATE * totalSec);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    /* 4 chord sections, 2.0s each */
    static const float chords[4][5] = {
        { 146.83f, 174.61f, 220.00f, 261.63f, 329.63f }, /* Dm9: D3, F3, A3, C4, E4 */
        { 174.61f, 220.00f, 261.63f, 329.63f, 392.00f }, /* G9/F: F3, A3, C4, E4, G4 */
        { 130.81f, 164.81f, 196.00f, 246.94f, 293.66f }, /* Cmaj9: C3, E3, G3, B3, D4 */
        { 110.00f, 164.81f, 196.00f, 261.63f, 329.63f }  /* Am9: A2, E3, G3, C4, E4 */
    };

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float chordPos = t / 2.0f;
        int cIdx = (int)chordPos % 4;
        int nextIdx = (cIdx + 1) % 4;
        float blend = chordPos - (float)((int)chordPos);
        /* Smooth cosine crossfade between chords */
        float wNext = 0.5f * (1.0f - cosf(blend * PI));
        float wCur = 1.0f - wNext;

        float sample = 0.0f;
        for (int n = 0; n < 5; n++) {
            float fCur = chords[cIdx][n];
            float fNext = chords[nextIdx][n];

            /* Gentle chorus detuning */
            float sCur = sinf(2.0f * PI * fCur * t) * 0.7f + sinf(2.0f * PI * (fCur * 1.003f) * t) * 0.3f;
            float sNext = sinf(2.0f * PI * fNext * t) * 0.7f + sinf(2.0f * PI * (fNext * 1.003f) * t) * 0.3f;

            sample += (sCur * wCur + sNext * wNext) * 0.20f;
        }

        /* Gentle slow stereo-like shimmer LFO */
        float lfo = 0.88f + 0.12f * sinf(2.0f * PI * 0.35f * t);
        sample *= lfo;

        /* Crossfade beginning and end to guarantee seamless looping without clicks */
        float loopFadeTime = 0.4f;
        if (t < loopFadeTime) {
            float k = t / loopFadeTime;
            sample *= 0.5f * (1.0f - cosf(k * PI));
        } else if (t > totalSec - loopFadeTime) {
            float k = (totalSec - t) / loopFadeTime;
            sample *= 0.5f * (1.0f - cosf(k * PI));
        }

        buf[i] = (short)(clamp_f(sample, -1.0f, 1.0f) * 16000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 2. Footsteps on Terrazzo Floor                                      */
/* ------------------------------------------------------------------ */
static Sound gen_footstep(int variant)
{
    float dur = 0.10f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    float baseFreq = 85.0f + variant * 8.0f;
    float noiseFreq = 1800.0f + variant * 220.0f;

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        /* Rapid exponential decay */
        float env = expf(-t * 42.0f);
        /* Low thump (heel impact) */
        float thump = sinf(2.0f * PI * baseFreq * t) * expf(-t * 55.0f);
        /* Sole friction / scuff (filtered noise) */
        float noise = (frand() * 2.0f - 1.0f) * sinf(2.0f * PI * noiseFreq * t) * expf(-t * 36.0f);

        float s = (thump * 0.7f + noise * 0.3f) * env;
        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 22000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 3. Heartbeat (Double Thump)                                         */
/* ------------------------------------------------------------------ */
static Sound gen_heartbeat(void)
{
    float dur = 0.28f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float s = 0.0f;

        /* First beat (lub) at t = 0 */
        if (t < 0.10f) {
            float env = sinf(t / 0.10f * PI);
            s += sinf(2.0f * PI * 68.0f * t) * env * 0.9f;
        }
        /* Second beat (dub) at t = 0.12s */
        if (t >= 0.12f && t < 0.25f) {
            float t2 = t - 0.12f;
            float env = sinf(t2 / 0.13f * PI);
            s += sinf(2.0f * PI * 54.0f * t2) * env * 0.75f;
        }

        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 28000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 4. Danger Sub-Bass Drone                                            */
/* ------------------------------------------------------------------ */
static Sound gen_danger_drone(void)
{
    float dur = 3.0f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        /* Ominous low dissonant beating: 55Hz (A1) + 58.5Hz (Bb1) */
        float b1 = sinf(2.0f * PI * 55.0f * t);
        float b2 = sinf(2.0f * PI * 58.5f * t) * 0.7f;
        float sub = sinf(2.0f * PI * 27.5f * t) * 0.5f;
        float s = (b1 + b2 + sub) * 0.35f;

        /* Seamless loop envelope */
        float fade = 0.25f;
        if (t < fade) s *= t / fade;
        else if (t > dur - fade) s *= (dur - t) / fade;

        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 20000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 5. Guard Alert Sting                                                */
/* ------------------------------------------------------------------ */
static Sound gen_alert(void)
{
    float dur = 0.50f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = expf(-t * 6.5f);
        /* Sharp dissonant tritone sting */
        float s = sinf(2.0f * PI * 466.16f * t) * 0.45f +   /* Bb4 */
                  sinf(2.0f * PI * 659.25f * t) * 0.40f +   /* E5 (tritone) */
                  sinf(2.0f * PI * 932.33f * t) * 0.25f;    /* Bb5 */
        /* Initial noise transient */
        if (t < 0.04f) s += (frand() * 2.0f - 1.0f) * 0.4f * (1.0f - t / 0.04f);

        buf[i] = (short)(clamp_f(s * env, -1.0f, 1.0f) * 26000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 6. Key Collected Chime                                              */
/* ------------------------------------------------------------------ */
static Sound gen_key(void)
{
    float dur = 0.42f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        /* First bell (C6) + Second bell (G6) staggered slightly */
        float env1 = expf(-t * 11.0f);
        float env2 = t > 0.06f ? expf(-(t - 0.06f) * 9.0f) : 0.0f;

        float bell1 = sinf(2.0f * PI * 1046.50f * t) + 0.35f * sinf(2.0f * PI * 2093.00f * t);
        float bell2 = t > 0.06f ? sinf(2.0f * PI * 1567.98f * (t - 0.06f)) + 0.3f * sinf(2.0f * PI * 3135.96f * (t - 0.06f)) : 0.0f;

        float s = bell1 * env1 * 0.5f + bell2 * env2 * 0.6f;
        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 24000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 7. All Keys Collected (Triumphant Fanfare)                          */
/* ------------------------------------------------------------------ */
static Sound gen_all_keys(void)
{
    float dur = 1.30f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    static const float notes[4] = { 523.25f, 659.25f, 783.99f, 1046.50f }; /* C5, E5, G5, C6 */

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float s = 0.0f;

        for (int n = 0; n < 4; n++) {
            float start = n * 0.12f;
            if (t >= start) {
                float dt = t - start;
                float env = expf(-dt * (n == 3 ? 3.0f : 6.0f));
                float f = notes[n];
                s += (sinf(2.0f * PI * f * dt) + 0.3f * sinf(2.0f * PI * f * 2.0f * dt)) * env * 0.28f;
            }
        }

        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 26000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 8. Caught / Slam                                                    */
/* ------------------------------------------------------------------ */
static Sound gen_caught(void)
{
    float dur = 0.85f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = expf(-t * 4.5f);
        /* Pitch slide down from 150Hz to 40Hz */
        float freq = 40.0f + 110.0f * expf(-t * 9.0f);
        float bass = sinf(2.0f * PI * freq * t);
        /* Metallic clang */
        float clang = sinf(2.0f * PI * 220.0f * t) * expf(-t * 12.0f);
        /* Heavy transient impact */
        float noise = t < 0.05f ? (frand() * 2.0f - 1.0f) * (1.0f - t / 0.05f) : 0.0f;

        float s = (bass * 0.7f + clang * 0.3f + noise * 0.5f) * env;
        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 28000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 9. Level Clear Victory                                              */
/* ------------------------------------------------------------------ */
static Sound gen_win(void)
{
    float dur = 1.6f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    static const float notes[5] = { 523.25f, 659.25f, 783.99f, 1046.50f, 1318.51f }; /* C5, E5, G5, C6, E6 */

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float s = 0.0f;

        for (int n = 0; n < 5; n++) {
            float start = n * 0.10f;
            if (t >= start) {
                float dt = t - start;
                float env = expf(-dt * (n == 4 ? 2.2f : 5.5f));
                float f = notes[n];
                s += (sinf(2.0f * PI * f * dt) + 0.35f * sinf(2.0f * PI * f * 2.0f * dt)) * env * 0.24f;
            }
        }

        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 25000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 10. Menu Click                                                      */
/* ------------------------------------------------------------------ */
static Sound gen_click(void)
{
    float dur = 0.04f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = 1.0f - (float)i / frames;
        float s = sinf(2.0f * PI * 1350.0f * t) * env;
        buf[i] = (short)(s * 18000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 11. Sprint Footstep (punchy strike)                                */
/* ------------------------------------------------------------------ */
static Sound gen_sprint_step(int variant)
{
    float dur = 0.09f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    float baseFreq = 70.0f + variant * 10.0f;
    float noiseFreq = 1600.0f + variant * 260.0f;

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = expf(-t * 48.0f);
        float thump = sinf(2.0f * PI * baseFreq * t) * expf(-t * 60.0f);
        float noise = (frand() * 2.0f - 1.0f) * sinf(2.0f * PI * noiseFreq * t) * expf(-t * 40.0f);
        float s = (thump * 0.82f + noise * 0.38f) * env;
        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 26000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 12. Coin Throw & Clink on Floor                                     */
/* ------------------------------------------------------------------ */
static Sound gen_coin(void)
{
    float dur = 0.14f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        /* Crisp, quick coin click & short metal body resonance, decays rapidly */
        float env = expf(-t * 62.0f);
        float click = (t < 0.015f) ? (frand() * 2.0f - 1.0f) * sinf(t / 0.015f * PI) * 0.35f : 0.0f;
        float body = (sinf(2.0f * PI * 1420.0f * t) * 0.65f + sinf(2.0f * PI * 2150.0f * t) * 0.35f) * env;
        float s = (click + body) * 0.38f;
        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 15000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 13. Locker Door Open / Latch                                        */
/* ------------------------------------------------------------------ */
static Sound gen_locker(void)
{
    float dur = 0.32f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        /* Hollow metal resonance */
        float hollow = sinf(2.0f * PI * 115.0f * t) * expf(-t * 14.0f) * 0.6f;
        /* Metal latch click at start */
        float click = (t < 0.04f) ? sinf(2.0f * PI * 920.0f * t) * expf(-t * 90.0f) * 0.7f : 0.0f;
        /* Low creak */
        float creak = sinf(2.0f * PI * (320.0f + 140.0f * sinf(t * 80.0f)) * t) * expf(-t * 18.0f) * 0.35f;

        float s = hollow + click + creak;
        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 26000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 14. Spotlight Vision Alert Whoosh                                   */
/* ------------------------------------------------------------------ */
static Sound gen_spotlight(void)
{
    float dur = 0.35f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float freq = 280.0f + 980.0f * (t / dur);
        float env = sinf(t / dur * PI);
        float s = sinf(2.0f * PI * freq * t) * env * 0.8f;
        s += (frand() * 2.0f - 1.0f) * env * 0.2f;
        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 24000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 15. Stamina Exhausted Warning                                       */
/* ------------------------------------------------------------------ */
static Sound gen_exhausted(void)
{
    float dur = 0.25f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;

    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = sinf(t / dur * PI);
        float s = (frand() * 2.0f - 1.0f) * env * 0.45f + sinf(2.0f * PI * 180.0f * t) * env * 0.35f;
        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 18000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 16. Stun Dart Fire (pneumatic whoosh)                               */
/* ------------------------------------------------------------------ */
static Sound gen_dart_fire(void)
{
    float dur = 0.12f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;
    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = expf(-t * 35.0f);
        float whoosh = (frand() * 2.0f - 1.0f) * sinf(2.0f * PI * (800.0f - 400.0f * (t / dur)) * t) * env;
        float s = whoosh * 0.7f;
        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 20000.0f);
    }
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 17. Stun Dart Hit (electric zap / stun)                             */
/* ------------------------------------------------------------------ */
static Sound gen_dart_hit(void)
{
    float dur = 0.22f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;
    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = expf(-t * 22.0f);
        float zap = sinf(2.0f * PI * (1200.0f + 600.0f * sinf(t * 320.0f)) * t) * env;
        float buzz = (frand() * 2.0f - 1.0f) * 0.4f * env;
        float s = (zap * 0.7f + buzz) * 0.8f;
        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 24000.0f);
    }
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 18. Flash Grenade (concussive flash explosion & ringing)           */
/* ------------------------------------------------------------------ */
static Sound gen_flash(void)
{
    float dur = 0.45f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;
    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float thump = sinf(2.0f * PI * (180.0f * expf(-t * 18.0f)) * t) * expf(-t * 10.0f);
        float bang = (frand() * 2.0f - 1.0f) * expf(-t * 16.0f);
        float ring = sinf(2.0f * PI * 2400.0f * t) * expf(-t * 6.0f) * 0.35f;
        float s = (thump * 0.6f + bang * 0.5f + ring) * 0.8f;
        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 28000.0f);
    }
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* 19. Ammo / Weapon Pickup Chime                                      */
/* ------------------------------------------------------------------ */
static Sound gen_pickup(void)
{
    float dur = 0.24f;
    int frames = (int)(SAMPLE_RATE * dur);
    Wave w = create_wave(frames);
    short *buf = (short *)w.data;
    for (int i = 0; i < frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        float f = (t < 0.10f) ? 587.33f : 880.00f; /* D5 -> A5 */
        float env = expf(-fmodf(t, 0.10f) * 28.0f) * (1.0f - t / dur);
        float s = sinf(2.0f * PI * f * t) * env * 0.6f;
        buf[i] = (short)(clamp_f(s, -1.0f, 1.0f) * 20000.0f);
    }
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
void audio_init(void)
{
    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        audioReady = 0;
        return;
    }
    audioReady = 1;

    sndMenuMusic = gen_menu_music();
    for (int i = 0; i < 4; i++) {
        sndFootsteps[i] = gen_footstep(i);
        sndSprintSteps[i] = gen_sprint_step(i);
    }
    sndHeartbeat = gen_heartbeat();
    sndDangerDrone = gen_danger_drone();
    sndAlert = gen_alert();
    sndKey = gen_key();
    sndAllKeys = gen_all_keys();
    sndCaught = gen_caught();
    sndWin = gen_win();
    sndClick = gen_click();
    sndCoin = gen_coin();
    sndLocker = gen_locker();
    sndSpotlight = gen_spotlight();
    sndExhausted = gen_exhausted();
    sndDartFire = gen_dart_fire();
    sndDartHit = gen_dart_hit();
    sndFlash = gen_flash();
    sndPickup = gen_pickup();
}

void audio_shutdown(void)
{
    if (!audioReady)
        return;
    UnloadSound(sndMenuMusic);
    for (int i = 0; i < 4; i++) {
        UnloadSound(sndFootsteps[i]);
        UnloadSound(sndSprintSteps[i]);
    }
    UnloadSound(sndHeartbeat);
    UnloadSound(sndDangerDrone);
    UnloadSound(sndAlert);
    UnloadSound(sndKey);
    UnloadSound(sndAllKeys);
    UnloadSound(sndCaught);
    UnloadSound(sndWin);
    UnloadSound(sndClick);
    UnloadSound(sndCoin);
    UnloadSound(sndLocker);
    UnloadSound(sndSpotlight);
    UnloadSound(sndExhausted);
    UnloadSound(sndDartFire);
    UnloadSound(sndDartHit);
    UnloadSound(sndFlash);
    UnloadSound(sndPickup);
    CloseAudioDevice();
    audioReady = 0;
}

void audio_play_footstep(void)
{
    if (!audioReady)
        return;
    Sound s = sndFootsteps[stepIndex];
    stepIndex = (stepIndex + 1) % 4;
    SetSoundPitch(s, 0.94f + frand() * 0.12f);
    SetSoundVolume(s, 0.65f + frand() * 0.15f);
    PlaySound(s);
}

void audio_play_sprint_step(void)
{
    if (!audioReady)
        return;
    Sound s = sndSprintSteps[stepIndex];
    stepIndex = (stepIndex + 1) % 4;
    SetSoundPitch(s, 0.95f + frand() * 0.10f);
    SetSoundVolume(s, 0.85f + frand() * 0.15f);
    PlaySound(s);
}

void audio_play_coin(void)
{
    if (!audioReady) return;
    SetSoundPitch(sndCoin, 0.98f + frand() * 0.06f);
    SetSoundVolume(sndCoin, 0.45f);
    PlaySound(sndCoin);
}

void audio_play_locker(void)
{
    if (!audioReady) return;
    SetSoundPitch(sndLocker, 0.95f + frand() * 0.10f);
    SetSoundVolume(sndLocker, 0.90f);
    PlaySound(sndLocker);
}

void audio_play_spotlight_alert(void)
{
    if (!audioReady) return;
    SetSoundVolume(sndSpotlight, 0.85f);
    PlaySound(sndSpotlight);
}

void audio_play_exhausted(void)
{
    if (!audioReady) return;
    SetSoundVolume(sndExhausted, 0.70f);
    PlaySound(sndExhausted);
}

void audio_play_key(void)
{
    if (!audioReady) return;
    SetSoundVolume(sndKey, 0.85f);
    PlaySound(sndKey);
}

void audio_play_all_keys(void)
{
    if (!audioReady) return;
    SetSoundVolume(sndAllKeys, 0.95f);
    PlaySound(sndAllKeys);
}

void audio_play_alert(void)
{
    if (!audioReady) return;
    SetSoundVolume(sndAlert, 0.95f);
    PlaySound(sndAlert);
}

void audio_play_caught(void)
{
    if (!audioReady) return;
    SetSoundVolume(sndCaught, 1.0f);
    PlaySound(sndCaught);
}

void audio_play_level_clear(void)
{
    if (!audioReady) return;
    SetSoundVolume(sndWin, 0.95f);
    PlaySound(sndWin);
}

void audio_play_menu_click(void)
{
    if (!audioReady) return;
    SetSoundVolume(sndClick, 0.6f);
    PlaySound(sndClick);
}

void audio_play_dart_fire(void)
{
    if (!audioReady) return;
    SetSoundPitch(sndDartFire, 0.95f + frand() * 0.10f);
    SetSoundVolume(sndDartFire, 0.80f);
    PlaySound(sndDartFire);
}

void audio_play_dart_hit(void)
{
    if (!audioReady) return;
    SetSoundPitch(sndDartHit, 0.95f + frand() * 0.10f);
    SetSoundVolume(sndDartHit, 0.90f);
    PlaySound(sndDartHit);
}

void audio_play_flash(void)
{
    if (!audioReady) return;
    SetSoundVolume(sndFlash, 0.95f);
    PlaySound(sndFlash);
}

void audio_play_pickup(void)
{
    if (!audioReady) return;
    SetSoundPitch(sndPickup, 0.98f + frand() * 0.05f);
    SetSoundVolume(sndPickup, 0.75f);
    PlaySound(sndPickup);
}

void audio_update(float dt)
{
    if (!audioReady)
        return;

    /* 1. Homescreen ambient soothing music */
    int isMenu = (G.state == ST_MENU || G.state == ST_HELP || G.state == ST_LEADERBOARD);
    if (isMenu) {
        if (menuMusicFade < 1.0f)
            menuMusicFade += dt * 2.0f;
        if (menuMusicFade > 1.0f)
            menuMusicFade = 1.0f;

        SetSoundVolume(sndMenuMusic, 0.38f * menuMusicFade);
        if (!IsSoundPlaying(sndMenuMusic))
            PlaySound(sndMenuMusic);

        /* Stop any danger sound */
        if (IsSoundPlaying(sndDangerDrone))
            StopSound(sndDangerDrone);
        return;
    }

    /* In game: fade out menu music */
    if (menuMusicFade > 0.0f) {
        menuMusicFade -= dt * 3.0f;
        if (menuMusicFade <= 0.0f) {
            menuMusicFade = 0.0f;
            StopSound(sndMenuMusic);
        } else {
            SetSoundVolume(sndMenuMusic, 0.38f * menuMusicFade);
        }
    }

    if (G.state != ST_PLAY || G.paused) {
        if (IsSoundPlaying(sndDangerDrone))
            StopSound(sndDangerDrone);
        return;
    }

    /* 2. Footsteps when moving */
    float dx = G.playerDraw.x - G.player.x, dy = G.playerDraw.y - G.player.y;
    int moving = (G.moveTimer > 0.0f || (dx * dx + dy * dy > 0.0004f)) && !G.waveActive && !G.inLocker;
    if (moving) {
        stepTimer += dt;
        float interval = G.isSprinting ? 0.155f : 0.23f;
        if (stepTimer >= interval) {
            stepTimer = 0.0f;
            if (G.isSprinting)
                audio_play_sprint_step();
            else
                audio_play_footstep();
        }
    } else {
        stepTimer = 0.20f;   /* immediate footstep upon starting to walk */
    }

    /* 3. Dynamic Danger Music & Heartbeat when guard is close or chasing */
    if (G.inLocker) {
        if (IsSoundPlaying(sndDangerDrone))
            StopSound(sndDangerDrone);
        heartTimer = 0.0f;
        return;
    }
    int minDist = 999;
    int anyChase = 0;
    for (int i = 0; i < G.guardCount; i++) {
        Guard *g = &G.guards[i];
        if (g->mode == G_CHASE)
            anyChase = 1;
        int d = G.field.dist[g->pos.y][g->pos.x];
        if (d >= 0 && d < minDist)
            minDist = d;
    }

    int inDanger = anyChase || (minDist <= DETECT_DIST);
    if (inDanger) {
        /* Subtle ominous background drone */
        if (!IsSoundPlaying(sndDangerDrone))
            PlaySound(sndDangerDrone);
        float droneVol = anyChase ? 0.50f : clamp_f(1.0f - (float)minDist / DETECT_DIST, 0.15f, 0.40f);
        SetSoundVolume(sndDangerDrone, droneVol);

        /* Heartbeat tempo & volume scale with proximity */
        float beatInterval;
        float beatVol;
        if (minDist <= 2) {
            beatInterval = 0.35f;
            beatVol = 0.95f;
        } else if (minDist <= 4) {
            beatInterval = 0.55f;
            beatVol = 0.75f;
        } else {
            beatInterval = 0.85f;
            beatVol = 0.50f;
        }

        heartTimer += dt;
        if (heartTimer >= beatInterval) {
            heartTimer = 0.0f;
            SetSoundVolume(sndHeartbeat, beatVol);
            PlaySound(sndHeartbeat);
        }
    } else {
        if (IsSoundPlaying(sndDangerDrone))
            StopSound(sndDangerDrone);
        heartTimer = 0.0f;
    }
}
