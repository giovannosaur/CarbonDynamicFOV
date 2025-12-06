// --- camera_shake.cpp (paste ke dllmain.cpp atau include) ---
#include <windows.h>
#include <stdint.h>
#include "camerashake.h"
#include <pch.h>
#include <cmath> 

#define CAM_BASE_ADDR     0x00B1D520
#define CAM_MATRIX_OFFSET 0x0      // jika offset matrix beda, ubah di sini

// configurable lewat ini kalo mau
float cfg_shake_min_speed = 60.0f;
float cfg_shake_max_speed = 80.0f;
float cfg_shake_max_trans = 0.000001f; // max translation (meter/unit). sesuaikan kalau terlalu besar/kecil
float cfg_shake_freq = 20.0f;      // frequency in Hz of shake oscillation

// matrix struct (4x4 floats)
struct bMatrix4 {
    float m[4][4];
};

// helper: safecopy with volatile source/dest
static inline void read_matrix_volatile(volatile bMatrix4* src, bMatrix4* dst) {
    // copy element by element to avoid weird compiler optimizations on volatile
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            dst->m[r][c] = src->m[r][c];
}

static inline void write_matrix_volatile(volatile bMatrix4* dst, const bMatrix4* src) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            dst->m[r][c] = src->m[r][c];
}

// state
bool shake_enabled = true; // master switch for shake; set false kalau mau matiin
bMatrix4 saved_original_matrix; // untuk restore ketika effect mati atau dimatikan
bool saved_original_valid = false;

// seed untuk variasi
uint32_t shake_seed = 0xC0FFEE;

// time helper (seconds, high-res)
static double now_seconds() {
    static LARGE_INTEGER freq = { 0 };
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart;
}

// map value linear from [a,b] -> [0,1]
static inline float linear_map(float v, float a, float b) {
    if (v <= a) return 0.0f;
    if (v >= b) return 1.0f;
    return (v - a) / (b - a);
}

// the camera shake update; call every frame when effectEnabled (or in separate thread)
void UpdateCameraShake(float speed)
{
    volatile bMatrix4* camMat = (volatile bMatrix4*)(CAM_BASE_ADDR + CAM_MATRIX_OFFSET);

    // safety: if pointer null, bail
    if (!camMat) return;

    // if effect global off, restore original once and return
    if (!effectEnabled || !shake_enabled) {
        if (saved_original_valid) {
            write_matrix_volatile(camMat, &saved_original_matrix);
            saved_original_valid = false;
        }
        return;
    }

    // read current matrix (game might be writing it every frame)
    bMatrix4 cur;
    read_matrix_volatile(camMat, &cur);

    // save original the first time (so we can restore original when mod disabled)
    if (!saved_original_valid) {
        saved_original_matrix = cur;
        saved_original_valid = true;
    }

    // compute t in [0..1] for shake intensity based on speed
    float t = linear_map(speed, cfg_shake_min_speed, cfg_shake_max_speed); // 0..1
    if (t <= 0.0f) {
        // no shake: restore original if we had modified it before
        if (saved_original_valid) {
            write_matrix_volatile(camMat, &saved_original_matrix);
            saved_original_valid = false;
        }
        return;
    }

    // clamp
    if (t > 1.0f) t = 1.0f;

    // amplitude scales linearly with t
    float amplitude = cfg_shake_max_trans * t;

    // time-based oscillation
    double time_s = now_seconds();
    // multiple sin/cos for richer shake
    float phase1 = (float)(time_s * cfg_shake_freq + (shake_seed & 0xFF));
    float phase2 = (float)(time_s * (cfg_shake_freq * 1.37) + ((shake_seed >> 8) & 0xFF));
    float phase3 = (float)(time_s * (cfg_shake_freq * 0.73) + ((shake_seed >> 16) & 0xFF));

    float ox = sinf(phase1) * amplitude;           // x offset
    float oy = sinf(phase2) * (amplitude * 0.7f);  // y offset (slightly smaller)
    float oz = cosf(phase3) * (amplitude * 0.4f);  // z offset (even smaller)

    // apply translation to the camera matrix position element (typically m[3][0..2] for row-major)
    // NOTE: if the matrix layout is column-major or different, adjust indices.
    bMatrix4 newMat = cur;
    newMat.m[3][0] = cur.m[3][0] + ox;
    newMat.m[3][1] = cur.m[3][1] + oy;
    newMat.m[3][2] = cur.m[3][2] + oz;

    // write back
    write_matrix_volatile(camMat, &newMat);
}
