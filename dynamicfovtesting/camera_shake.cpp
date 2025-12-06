// camera_shake.cpp
#include "pch.h"
#include "camera_shake.h"
#include <windows.h>
#include <cmath>

#define CAM_BASE_ADDR     0x00B1D520
#define CAM_MATRIX_OFFSET 0x0

struct bMatrix4 {
    float m[4][4];
};

static bool saved_original_valid = false;
static bMatrix4 saved_original_matrix;

static inline void read_matrix_volatile(volatile bMatrix4* src, bMatrix4* dst) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            dst->m[r][c] = src->m[r][c];
}

static inline void write_matrix_volatile(volatile bMatrix4* dst, const bMatrix4* src) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            dst->m[r][c] = src->m[r][c];
}

static double now_seconds() {
    static LARGE_INTEGER freq = { 0 };
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart;
}

static inline float linear_map(float v, float a, float b) {
    if (v <= a) return 0.0f;
    if (v >= b) return 1.0f;
    return (v - a) / (b - a);
}

void update_camera_shake(float speed, bool enabled)
{
    volatile bMatrix4* camMat = (volatile bMatrix4*)(CAM_BASE_ADDR + CAM_MATRIX_OFFSET);
    if (!camMat) return;

    if (!enabled) {
        if (saved_original_valid) {
            write_matrix_volatile(camMat, &saved_original_matrix);
            saved_original_valid = false;
        }
        return;
    }

    bMatrix4 cur;
    read_matrix_volatile(camMat, &cur);

    if (!saved_original_valid) {
        saved_original_matrix = cur;
        saved_original_valid = true;
    }

    float t = linear_map(speed, 60.0f, 80.0f);
    if (t <= 0.0f) {
        write_matrix_volatile(camMat, &saved_original_matrix);
        saved_original_valid = false;
        return;
    }
    if (t > 1.0f) t = 1.0f;

    float amplitude = 0.0000003f * t;
    float freq = 10.0f;

    double ts = now_seconds();
    float ox = sinf(ts * freq) * amplitude;
    float oy = sinf(ts * freq * 1.37f) * amplitude * 0.7f;
    float oz = cosf(ts * freq * 0.73f) * amplitude * 0.4f;

    bMatrix4 out = cur;
    out.m[3][0] = cur.m[3][0] + ox;
    out.m[3][1] = cur.m[3][1] + oy;
    out.m[3][2] = cur.m[3][2] + oz;

    write_matrix_volatile(camMat, &out);
}
