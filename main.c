/*
 * Dark Corridor Demo — этапы 1-5
 *
 * Первое лицо, коридор + комната + запертая дверь, руки и фонарик,
 * настоящее освещение (GU spotlight), процедурные текстуры, туман,
 * процедурный звук, интерактивные объекты, мини-шрифт для HUD.
 * Все ресурсы собственные и генерируются в коде.
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspaudio.h>
#include <psppower.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

PSP_MODULE_INFO("DarkCorridor", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);

#define BUF_WIDTH  512
#define SCR_WIDTH  480
#define SCR_HEIGHT 272
#define PI_F       3.14159265f
#define TWO_PI     6.28318531f

#define COL(r, g, b)     (0xff000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))
#define COLA(r, g, b, a) (((u32)(a) << 24) | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))

/* ================================================================== */
/* Утилиты                                                             */
/* ================================================================== */
static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static u32 g_rng = 12345u;
static u32 rnd_u32(void) { g_rng = g_rng * 1664525u + 1013904223u; return g_rng; }
static float rnd01(void) { return (float)(rnd_u32() >> 8) / 16777216.0f; }

/* ================================================================== */
/* Системные колбэки                                                   */
/* ================================================================== */
static int exit_callback(int a1, int a2, void *common)
{
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, 0);
}

/* ================================================================== */
/* АУДИО (процедурные звуки, свой микшер в отдельном потоке)           */
/* ================================================================== */
#define SR            44100
#define AUDIO_SAMPLES 1024
#define MAXV          8

typedef struct {
    const short *d;
    volatile int len;
    volatile int pos;
    float vol, pan;
} Voice;

static Voice g_voices[MAXV];
static volatile int g_audioRun = 1;
static u32 g_arng = 777u;

static float arnd(void)
{
    g_arng = g_arng * 1664525u + 1013904223u;
    return ((float)(g_arng >> 8) / 8388608.0f) - 1.0f;   /* -1..1 */
}

static short to16(float v) { return (short)(clampf(v, -1.0f, 1.0f) * 30000.0f); }

#define N_CLICK 2205
#define N_STEP  6615
#define N_DRIP  22050
#define N_CREAK 66150
#define N_THUD  33075
#define N_PICK  13230

static short sClick[N_CLICK];
static short sStepA[N_STEP];
static short sStepB[N_STEP];
static short sDrip[N_DRIP];
static short sCreak[N_CREAK];
static short sThud[N_THUD];
static short sPick[N_PICK];

static float drip_dry(float t)
{
    if (t < 0.0f) return 0.0f;
    float ph = TWO_PI * (900.0f * t + 500.0f * (1.0f - expf(-60.0f * t)) / 60.0f);
    return sinf(ph) * expf(-t * 45.0f) * 0.7f;
}

static void gen_sounds(void)
{
    float lp;
    for (int i = 0; i < N_CLICK; i++) {
        float t = (float)i / SR;
        float s = (arnd() * 0.5f + sinf(TWO_PI * 2200.0f * t) * 0.5f) * expf(-t * 140.0f);
        sClick[i] = to16(s * 0.8f);
    }
    lp = 0.0f;
    for (int i = 0; i < N_STEP; i++) {
        float t = (float)i / SR;
        lp += (arnd() - lp) * 0.18f;
        float s = (lp * 1.6f + sinf(TWO_PI * 65.0f * t) * 0.9f) * expf(-t * 28.0f);
        sStepA[i] = to16(s * 0.8f);
    }
    lp = 0.0f;
    for (int i = 0; i < N_STEP; i++) {
        float t = (float)i / SR;
        lp += (arnd() - lp) * 0.25f;
        float s = (lp * 1.4f + sinf(TWO_PI * 55.0f * t) * 1.0f) * expf(-t * 25.0f);
        sStepB[i] = to16(s * 0.8f);
    }
    for (int i = 0; i < N_DRIP; i++) {
        float t = (float)i / SR;
        float s = drip_dry(t) + 0.35f * drip_dry(t - 0.13f) + 0.15f * drip_dry(t - 0.27f);
        sDrip[i] = to16(s);
    }
    {
        float ph = 0.0f, rough = 0.0f;
        for (int i = 0; i < N_CREAK; i++) {
            float t = (float)i / SR;
            float f = 170.0f + 60.0f * sinf(TWO_PI * 0.9f * t) + 40.0f * t;
            ph += TWO_PI * f / SR;
            float saw = fmodf(ph / TWO_PI, 1.0f) * 2.0f - 1.0f;
            rough += (arnd() - rough) * 0.02f;
            float env = sinf(PI_F * t / 1.5f);
            sCreak[i] = to16((saw * 0.35f + rough * 2.0f) * env * 0.6f);
        }
    }
    lp = 0.0f;
    for (int i = 0; i < N_THUD; i++) {
        float t = (float)i / SR;
        lp += (arnd() - lp) * 0.05f;
        float s = (sinf(TWO_PI * 48.0f * t) * 0.9f + lp * 1.5f) * expf(-t * 5.0f) * (1.0f - expf(-t * 200.0f));
        sThud[i] = to16(s);
    }
    for (int i = 0; i < N_PICK; i++) {
        float t = (float)i / SR;
        float s = (sinf(TWO_PI * 660.0f * t) + 0.5f * sinf(TWO_PI * 990.0f * t)) * expf(-t * 9.0f) * (1.0f - expf(-t * 400.0f));
        sPick[i] = to16(s * 0.4f);
    }
}

static void play_sound(const short *d, int len, float vol, float pan)
{
    for (int i = 0; i < MAXV; i++) {
        Voice *v = &g_voices[i];
        if (v->pos >= v->len) {
            v->pos = v->len;       /* неактивен на время записи */
            v->d = d;
            v->vol = vol;
            v->pan = pan;
            v->len = len;
            v->pos = 0;
            return;
        }
    }
}

static int audio_thread(SceSize args, void *argp)
{
    static short buf[AUDIO_SAMPLES * 2] __attribute__((aligned(64)));
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0) return 0;

    float ph1 = 0.0f, ph2 = 0.0f, lfo = 0.0f, wind = 0.0f;

    while (g_audioRun) {
        for (int i = 0; i < AUDIO_SAMPLES; i++) {
            ph1 += TWO_PI * 52.0f / SR;   if (ph1 > TWO_PI) ph1 -= TWO_PI;
            ph2 += TWO_PI * 53.7f / SR;   if (ph2 > TWO_PI) ph2 -= TWO_PI;
            lfo += TWO_PI * 0.07f / SR;   if (lfo > TWO_PI * 10.0f) lfo -= TWO_PI * 10.0f;
            wind += (arnd() - wind) * 0.004f;

            float amb = (sinf(ph1) * 0.09f + sinf(ph2) * 0.08f) * (0.7f + 0.3f * sinf(lfo))
                      + wind * 0.9f * (0.4f + 0.6f * (0.5f + 0.5f * sinf(lfo * 1.7f)));
            float L = amb, R = amb;

            for (int k = 0; k < MAXV; k++) {
                Voice *v = &g_voices[k];
                if (v->pos < v->len) {
                    float s = (float)v->d[v->pos] / 32768.0f * v->vol;
                    v->pos = v->pos + 1;
                    float gl = v->pan > 0.0f ? 1.0f - v->pan : 1.0f;
                    float gr = v->pan < 0.0f ? 1.0f + v->pan : 1.0f;
                    L += s * gl;
                    R += s * gr;
                }
            }
            buf[i * 2]     = to16(L);
            buf[i * 2 + 1] = to16(R);
        }
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, buf);
    }
    return 0;
}

static void start_audio(void)
{
    gen_sounds();
    for (int i = 0; i < MAXV; i++) { g_voices[i].pos = 0; g_voices[i].len = 0; }
    int thid = sceKernelCreateThread("audio", audio_thread, 0x12, 0x10000, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
}

/* ================================================================== */
/* ТЕКСТУРЫ (процедурные, 64x64, тайлятся)                              */
/* ================================================================== */
#define TEX_SIZE 64
enum { T_WALL, T_FLOOR, T_CEIL, T_WOOD, T_METAL, T_PAPER, T_COUNT };
static u32 g_tex[T_COUNT][TEX_SIZE * TEX_SIZE] __attribute__((aligned(16)));

static u32 hash2(int x, int y)
{
    u32 h = (u32)x * 374761393u + (u32)y * 668265263u + 0x9e3779b9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float lat(int x, int y, int px, int py)
{
    x = ((x % px) + px) % px;
    y = ((y % py) + py) % py;
    return (float)(hash2(x, y) & 0xffff) / 65535.0f;
}

static float noise_p(float x, float y, int px, int py)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    float a = lat(xi, yi, px, py),     b = lat(xi + 1, yi, px, py);
    float c = lat(xi, yi + 1, px, py), d = lat(xi + 1, yi + 1, px, py);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}

static float fbm_t(float u, float v)
{
    return noise_p(u * 4.0f, v * 4.0f, 4, 4) * 0.5f
         + noise_p(u * 8.0f, v * 8.0f, 8, 8) * 0.3f
         + noise_p(u * 16.0f, v * 16.0f, 16, 16) * 0.2f;
}

static u32 pack_rgb(float r, float g, float b)
{
    int R = (int)clampf(r, 0.0f, 255.0f);
    int G = (int)clampf(g, 0.0f, 255.0f);
    int B = (int)clampf(b, 0.0f, 255.0f);
    return COL(R, G, B);
}

static void gen_textures(void)
{
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            float u = (float)x / TEX_SIZE, v = (float)y / TEX_SIZE;
            float n = fbm_t(u, v);
            float fine = noise_p(u * 32.0f, v * 32.0f, 32, 32);
            float r, g, b;

            /* --- стена: грязный бетон с потёками --- */
            {
                float streak = clampf((noise_p(u * 16.0f, v * 2.0f, 16, 2) - 0.5f) * 2.0f, 0.0f, 1.0f);
                float stain = clampf((0.45f - n) * 4.0f, 0.0f, 1.0f);
                float k = (0.55f + 0.7f * n) * (1.0f - 0.45f * streak) * (0.9f + 0.2f * fine);
                r = 100.0f * k; g = 94.0f * k; b = 82.0f * k;
                r = lerpf(r, 52.0f, stain * 0.6f);
                g = lerpf(g, 40.0f, stain * 0.6f);
                b = lerpf(b, 26.0f, stain * 0.6f);
                g_tex[T_WALL][y * TEX_SIZE + x] = pack_rgb(r, g, b);
            }
            /* --- пол: плитка 16px с затёртыми швами --- */
            {
                int tx = x % 16, ty = y % 16;
                float var = lat(x / 16, y / 16, 4, 4);
                float k = (0.6f + 0.5f * n) * (0.9f + 0.2f * fine);
                r = (78.0f + var * 34.0f) * k;
                g = (74.0f + var * 30.0f) * k;
                b = (66.0f + var * 26.0f) * k;
                if (tx == 0 || ty == 0) { r = 22.0f; g = 20.0f; b = 18.0f; }
                g_tex[T_FLOOR][y * TEX_SIZE + x] = pack_rgb(r, g, b);
            }
            /* --- потолок: тёмные панели --- */
            {
                float k = (0.5f + 0.7f * n) * (0.9f + 0.2f * fine);
                r = 70.0f * k; g = 68.0f * k; b = 62.0f * k;
                if ((x % 32) == 0 || (y % 32) == 0) { r *= 0.35f; g *= 0.35f; b *= 0.35f; }
                g_tex[T_CEIL][y * TEX_SIZE + x] = pack_rgb(r, g, b);
            }
            /* --- дерево: горизонтальные доски --- */
            {
                float grain = noise_p(u * 4.0f, v * 32.0f, 4, 32);
                float k = 0.6f + 0.6f * grain;
                r = 118.0f * k; g = 80.0f * k; b = 46.0f * k;
                if ((y % 16) == 0) { r = 25.0f; g = 16.0f; b = 8.0f; }
                g_tex[T_WOOD][y * TEX_SIZE + x] = pack_rgb(r, g, b);
            }
            /* --- металл с ржавчиной (двери) --- */
            {
                float rust = clampf((noise_p(u * 6.0f, v * 6.0f, 6, 6) - 0.55f) * 3.0f, 0.0f, 1.0f);
                float k = (0.6f + 0.5f * n) * (0.9f + 0.2f * fine);
                r = 74.0f * k; g = 78.0f * k; b = 80.0f * k;
                r = lerpf(r, 125.0f * k, rust);
                g = lerpf(g, 64.0f * k, rust);
                b = lerpf(b, 30.0f * k, rust);
                if (x < 3 || x > 60 || y < 3 || y > 60 || y == 32) { r *= 0.4f; g *= 0.4f; b *= 0.4f; }
                g_tex[T_METAL][y * TEX_SIZE + x] = pack_rgb(r, g, b);
            }
            /* --- бумага с «текстом» --- */
            {
                float k = 0.85f + 0.15f * n;
                r = 210.0f * k; g = 200.0f * k; b = 165.0f * k;
                if ((y % 6) == 3 && x > 6 && x < 58 && (hash2(x / 3, y / 6) & 3u) != 0u) {
                    r = 40.0f; g = 38.0f; b = 36.0f;
                }
                g_tex[T_PAPER][y * TEX_SIZE + x] = pack_rgb(r, g, b);
            }
        }
    }
    sceKernelDcacheWritebackAll();
}

/* ================================================================== */
/* ГЕОМЕТРИЯ                                                           */
/* ================================================================== */
typedef struct { float u, v; u32 color; float nx, ny, nz; float x, y, z; } LVertex;
typedef struct { u32 color; float x, y, z; } CVertex;

#define LV_FLAGS    (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
#define CV_FLAGS    (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
#define CV2D_FLAGS  (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)

typedef struct { LVertex *v; int n; int cap; } Mesh;

static LVertex vWall[7000]  __attribute__((aligned(16)));
static LVertex vFloor[3000] __attribute__((aligned(16)));
static LVertex vCeil[3000]  __attribute__((aligned(16)));
static LVertex vWood[600]   __attribute__((aligned(16)));
static LVertex vMetal[1200] __attribute__((aligned(16)));
static LVertex vPaper[60]   __attribute__((aligned(16)));
static LVertex vDoor[120]   __attribute__((aligned(16)));

static Mesh mWall  = { vWall,  0, 7000 };
static Mesh mFloor = { vFloor, 0, 3000 };
static Mesh mCeil  = { vCeil,  0, 3000 };
static Mesh mWood  = { vWood,  0, 600 };
static Mesh mMetal = { vMetal, 0, 1200 };
static Mesh mPaper = { vPaper, 0, 60 };
static Mesh mDoor  = { vDoor,  0, 120 };

#define WALL_H 2.8f
#define TILE   1.0f
#define TILE_V 0.7f

static u32 tint(void)
{
    int b = 190 + (int)(rnd_u32() % 60u);
    return COL(b, b, b);
}

/* p[i] = { x, y, z, u, v } */
static void mesh_quad(Mesh *m, const float p[4][5], float nx, float ny, float nz, u32 col)
{
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    if (m->n + 6 > m->cap) return;
    for (int i = 0; i < 6; i++) {
        const float *q = p[idx[i]];
        LVertex *o = &m->v[m->n++];
        o->u = q[3]; o->v = q[4];
        o->color = col;
        o->nx = nx; o->ny = ny; o->nz = nz;
        o->x = q[0]; o->y = q[1]; o->z = q[2];
    }
}

/* Вертикальная стена, разбитая на тайлы (для вершинного освещения) */
static void wall_tiles(Mesh *m, float xa, float za, float xb, float zb,
                       float y0, float y1, float nx, float nz)
{
    float dx = xb - xa, dz = zb - za;
    float len = sqrtf(dx * dx + dz * dz);
    int nu = (int)(len / TILE + 0.5f); if (nu < 1) nu = 1;
    int nv = (int)((y1 - y0) / TILE_V + 0.5f); if (nv < 1) nv = 1;
    int alongX = fabsf(dx) > fabsf(dz);

    for (int i = 0; i < nu; i++) {
        for (int j = 0; j < nv; j++) {
            float s0 = (float)i / nu, s1 = (float)(i + 1) / nu;
            float t0 = (float)j / nv, t1 = (float)(j + 1) / nv;
            float x0 = xa + dx * s0, z0 = za + dz * s0;
            float x1 = xa + dx * s1, z1 = za + dz * s1;
            float ya = y0 + (y1 - y0) * t0, yb = y0 + (y1 - y0) * t1;
            float u0 = (alongX ? x0 : z0) * 0.5f, u1 = (alongX ? x1 : z1) * 0.5f;
            float p[4][5] = {
                { x0, ya, z0, u0, ya * 0.5f },
                { x1, ya, z1, u1, ya * 0.5f },
                { x1, yb, z1, u1, yb * 0.5f },
                { x0, yb, z0, u0, yb * 0.5f },
            };
            mesh_quad(m, p, nx, 0.0f, nz, tint());
        }
    }
}

/* Горизонтальная плоскость (пол/потолок) */
static void plane_tiles(Mesh *m, float x0, float z0, float x1, float z1, float y, float ny)
{
    int nu = (int)((x1 - x0) / TILE + 0.5f); if (nu < 1) nu = 1;
    int nw = (int)((z1 - z0) / TILE + 0.5f); if (nw < 1) nw = 1;
    for (int i = 0; i < nu; i++) {
        for (int j = 0; j < nw; j++) {
            float xa = x0 + (x1 - x0) * i / nu,       xb = x0 + (x1 - x0) * (i + 1) / nu;
            float za = z0 + (z1 - z0) * j / nw,       zb = z0 + (z1 - z0) * (j + 1) / nw;
            float p[4][5] = {
                { xa, y, za, xa * 0.5f, za * 0.5f },
                { xb, y, za, xb * 0.5f, za * 0.5f },
                { xb, y, zb, xb * 0.5f, zb * 0.5f },
                { xa, y, zb, xa * 0.5f, zb * 0.5f },
            };
            mesh_quad(m, p, 0.0f, ny, 0.0f, tint());
        }
    }
}

static void box3d(Mesh *m, float x0, float y0, float z0, float x1, float y1, float z1)
{
    u32 c = tint();
    float q1[4][5] = {
        { x1, y0, z0, z0 * 0.5f, y0 * 0.5f }, { x1, y0, z1, z1 * 0.5f, y0 * 0.5f },
        { x1, y1, z1, z1 * 0.5f, y1 * 0.5f }, { x1, y1, z0, z0 * 0.5f, y1 * 0.5f } };
    float q2[4][5] = {
        { x0, y0, z1, z1 * 0.5f, y0 * 0.5f }, { x0, y0, z0, z0 * 0.5f, y0 * 0.5f },
        { x0, y1, z0, z0 * 0.5f, y1 * 0.5f }, { x0, y1, z1, z1 * 0.5f, y1 * 0.5f } };
    float q3[4][5] = {
        { x0, y0, z1, x0 * 0.5f, y0 * 0.5f }, { x1, y0, z1, x1 * 0.5f, y0 * 0.5f },
        { x1, y1, z1, x1 * 0.5f, y1 * 0.5f }, { x0, y1, z1, x0 * 0.5f, y1 * 0.5f } };
    float q4[4][5] = {
        { x1, y0, z0, x1 * 0.5f, y0 * 0.5f }, { x0, y0, z0, x0 * 0.5f, y0 * 0.5f },
        { x0, y1, z0, x0 * 0.5f, y1 * 0.5f }, { x1, y1, z0, x1 * 0.5f, y1 * 0.5f } };
    float q5[4][5] = {
        { x0, y1, z0, x0 * 0.5f, z0 * 0.5f }, { x1, y1, z0, x1 * 0.5f, z0 * 0.5f },
        { x1, y1, z1, x1 * 0.5f, z1 * 0.5f }, { x0, y1, z1, x0 * 0.5f, z1 * 0.5f } };
    mesh_quad(m, q1,  1.0f, 0.0f,  0.0f, c);
    mesh_quad(m, q2, -1.0f, 0.0f,  0.0f, c);
    mesh_quad(m, q3,  0.0f, 0.0f,  1.0f, c);
    mesh_quad(m, q4,  0.0f, 0.0f, -1.0f, c);
    mesh_quad(m, q5,  0.0f, 1.0f,  0.0f, c);
}

/* ------------------------------------------------------------------ */
/* Уровень                                                             */
/*   Коридор:  x[-1.5,1.5]  z[-24,0]                                   */
/*   Проход:   x[1.5,2.0]   z[-11.7,-10.3]                             */
/*   Комната:  x[2.0,6.5]   z[-14,-8]  (ящик, записка)                 */
/*   Дверь в конце коридора z=-24 (нужен ключ) -> коридор x[-0.9,0.9]  */
/*   z[-30,-24] -> конец демо                                          */
/* ------------------------------------------------------------------ */
typedef struct { float x0, z0, x1, z1; int active; } Rect;

static Rect g_walk[] = {
    { -1.5f, -24.0f, 1.5f,   0.0f, 1 },
    {  1.5f, -11.7f, 2.0f, -10.3f, 1 },
    {  2.0f, -14.0f, 6.5f,  -8.0f, 1 },
    { -0.9f, -30.0f, 0.9f, -24.0f, 0 },   /* за дверью */
};
#define NUM_WALK ((int)(sizeof(g_walk) / sizeof(g_walk[0])))

static const Rect g_solid[] = {
    { 4.8f, -13.4f, 5.8f, -12.4f, 1 },   /* ящик */
};
#define NUM_SOLID ((int)(sizeof(g_solid) / sizeof(g_solid[0])))

static void build_level(void)
{
    /* коридор */
    wall_tiles(&mWall, -1.5f,   0.0f, -1.5f, -24.0f, 0.0f, WALL_H,  1.0f, 0.0f);   /* запад */
    wall_tiles(&mWall,  1.5f,   0.0f,  1.5f, -10.3f, 0.0f, WALL_H, -1.0f, 0.0f);   /* восток, до проёма */
    wall_tiles(&mWall,  1.5f, -11.7f,  1.5f, -24.0f, 0.0f, WALL_H, -1.0f, 0.0f);   /* восток, после проёма */
    wall_tiles(&mWall, -1.5f,   0.0f,  1.5f,   0.0f, 0.0f, WALL_H,  0.0f, -1.0f);  /* задняя */
    /* торец с дверным проёмом */
    wall_tiles(&mWall, -1.5f, -24.0f, -0.9f, -24.0f, 0.0f, WALL_H,  0.0f, 1.0f);
    wall_tiles(&mWall,  0.9f, -24.0f,  1.5f, -24.0f, 0.0f, WALL_H,  0.0f, 1.0f);
    wall_tiles(&mWall, -0.9f, -24.0f,  0.9f, -24.0f, 2.2f, WALL_H,  0.0f, 1.0f);
    plane_tiles(&mFloor, -1.5f, -24.0f, 1.5f, 0.0f, 0.0f,    1.0f);
    plane_tiles(&mCeil,  -1.5f, -24.0f, 1.5f, 0.0f, WALL_H, -1.0f);

    /* проход в комнату */
    wall_tiles(&mWall, 1.5f, -11.7f, 2.0f, -11.7f, 0.0f, WALL_H, 0.0f,  1.0f);
    wall_tiles(&mWall, 1.5f, -10.3f, 2.0f, -10.3f, 0.0f, WALL_H, 0.0f, -1.0f);
    plane_tiles(&mFloor, 1.5f, -11.7f, 2.0f, -10.3f, 0.0f,    1.0f);
    plane_tiles(&mCeil,  1.5f, -11.7f, 2.0f, -10.3f, WALL_H, -1.0f);

    /* комната */
    plane_tiles(&mFloor, 2.0f, -14.0f, 6.5f, -8.0f, 0.0f,    1.0f);
    plane_tiles(&mCeil,  2.0f, -14.0f, 6.5f, -8.0f, WALL_H, -1.0f);
    wall_tiles(&mWall, 2.0f, -14.0f, 2.0f, -11.7f, 0.0f, WALL_H,  1.0f, 0.0f);
    wall_tiles(&mWall, 2.0f, -10.3f, 2.0f,  -8.0f, 0.0f, WALL_H,  1.0f, 0.0f);
    wall_tiles(&mWall, 6.5f, -14.0f, 6.5f,  -8.0f, 0.0f, WALL_H, -1.0f, 0.0f);
    wall_tiles(&mWall, 2.0f, -14.0f, 6.5f, -14.0f, 0.0f, WALL_H,  0.0f,  1.0f);
    wall_tiles(&mWall, 2.0f,  -8.0f, 6.5f,  -8.0f, 0.0f, WALL_H,  0.0f, -1.0f);

    /* коридор за дверью */
    wall_tiles(&mWall, -0.9f, -24.0f, -0.9f, -30.0f, 0.0f, WALL_H,  1.0f, 0.0f);
    wall_tiles(&mWall,  0.9f, -24.0f,  0.9f, -30.0f, 0.0f, WALL_H, -1.0f, 0.0f);
    wall_tiles(&mWall, -0.9f, -30.0f,  0.9f, -30.0f, 0.0f, WALL_H,  0.0f,  1.0f);
    plane_tiles(&mFloor, -0.9f, -30.0f, 0.9f, -24.0f, 0.0f,    1.0f);
    plane_tiles(&mCeil,  -0.9f, -30.0f, 0.9f, -24.0f, WALL_H, -1.0f);

    /* ящик */
    box3d(&mWood, 4.8f, 0.0f, -13.4f, 5.8f, 0.8f, -12.4f);

    /* декоративные закрытые двери */
    {
        const float zs[4] = { -5.0f, -8.5f, -17.0f, -20.5f };
        for (int i = 0; i < 4; i++)
            box3d(&mMetal, -1.5f, 0.0f, zs[i] - 0.5f, -1.44f, 2.1f, zs[i] + 0.5f);
        box3d(&mMetal, 1.44f, 0.0f, -17.5f, 1.5f, 2.1f, -16.5f);
        box3d(&mMetal, 1.44f, 0.0f, -21.0f, 1.5f, 2.1f, -20.0f);
    }

    /* записка на стене комнаты (z = -8, интерьер смотрит в -z) */
    {
        float p[4][5] = {
            { 3.45f, 1.30f, -8.01f, 0.0f, 1.0f },
            { 3.00f, 1.30f, -8.01f, 1.0f, 1.0f },
            { 3.00f, 1.75f, -8.01f, 1.0f, 0.0f },
            { 3.45f, 1.75f, -8.01f, 0.0f, 0.0f },
        };
        mesh_quad(&mPaper, p, 0.0f, 0.0f, -1.0f, COL(255, 255, 255));
    }

    /* запертая дверь (двигается вверх при открытии) */
    box3d(&mDoor, -0.9f, 0.0f, -24.0f, 0.9f, 2.2f, -23.92f);

    sceKernelDcacheWritebackAll();
}

/* ================================================================== */
/* ВЬЮМОДЕЛЬ: руки и фонарик (только свои low-poly «трубки»)            */
/* ================================================================== */
typedef struct { CVertex *v; int n; int cap; } CMesh;
static CVertex vmBody[5000] __attribute__((aligned(16)));
static CMesh mVm = { vmBody, 0, 5000 };

static void cm_vert(CMesh *m, u32 c, float x, float y, float z)
{
    if (m->n < m->cap) {
        CVertex *o = &m->v[m->n++];
        o->color = c; o->x = x; o->y = y; o->z = z;
    }
}

static u32 shade_rgb(int r, int g, int b, float f)
{
    int R = (int)clampf((float)r * f, 0.0f, 255.0f);
    int G = (int)clampf((float)g * f, 0.0f, 255.0f);
    int B = (int)clampf((float)b * f, 0.0f, 255.0f);
    return COL(R, G, B);
}

static void ring_pt(float *o, float cx, float cy, float cz,
                    const float *u, const float *w, float c, float s, float r)
{
    o[0] = cx + (u[0] * c + w[0] * s) * r;
    o[1] = cy + (u[1] * c + w[1] * s) * r;
    o[2] = cz + (u[2] * c + w[2] * s) * r;
}

static void tube(CMesh *m, float ax, float ay, float az, float bx, float by, float bz,
                 float ra, float rb, int sides, int cr, int cg, int cb, float br)
{
    float d[3] = { bx - ax, by - ay, bz - az };
    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1e-5f) return;
    d[0] /= len; d[1] /= len; d[2] /= len;

    float ref[3] = { 0.0f, 1.0f, 0.0f };
    if (fabsf(d[1]) > 0.95f) { ref[0] = 1.0f; ref[1] = 0.0f; }

    float u[3] = { d[1] * ref[2] - d[2] * ref[1], d[2] * ref[0] - d[0] * ref[2], d[0] * ref[1] - d[1] * ref[0] };
    float ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    u[0] /= ul; u[1] /= ul; u[2] /= ul;
    float w[3] = { d[1] * u[2] - d[2] * u[1], d[2] * u[0] - d[0] * u[2], d[0] * u[1] - d[1] * u[0] };

    const float L[3] = { -0.25f, 0.55f, 0.80f };   /* направление «света» в пространстве камеры */

    float capA = 0.40f + 0.60f * clampf(-(d[0] * L[0] + d[1] * L[1] + d[2] * L[2]), 0.0f, 1.0f);
    float capB = 0.40f + 0.60f * clampf(  d[0] * L[0] + d[1] * L[1] + d[2] * L[2], 0.0f, 1.0f);
    u32 cA = shade_rgb(cr, cg, cb, capA * br);
    u32 cB = shade_rgb(cr, cg, cb, capB * br);

    for (int i = 0; i < sides; i++) {
        float t0 = TWO_PI * (float)i / sides, t1 = TWO_PI * (float)(i + 1) / sides;
        float tm = (t0 + t1) * 0.5f;
        float c0 = cosf(t0), s0 = sinf(t0), c1 = cosf(t1), s1 = sinf(t1);
        float a0[3], a1[3], b0[3], b1[3];
        ring_pt(a0, ax, ay, az, u, w, c0, s0, ra);
        ring_pt(a1, ax, ay, az, u, w, c1, s1, ra);
        ring_pt(b0, bx, by, bz, u, w, c0, s0, rb);
        ring_pt(b1, bx, by, bz, u, w, c1, s1, rb);

        float nx = u[0] * cosf(tm) + w[0] * sinf(tm);
        float ny = u[1] * cosf(tm) + w[1] * sinf(tm);
        float nz = u[2] * cosf(tm) + w[2] * sinf(tm);
        float f = 0.40f + 0.60f * clampf(nx * L[0] + ny * L[1] + nz * L[2], 0.0f, 1.0f);
        u32 col = shade_rgb(cr, cg, cb, f * br);

        cm_vert(m, col, a0[0], a0[1], a0[2]); cm_vert(m, col, a1[0], a1[1], a1[2]); cm_vert(m, col, b1[0], b1[1], b1[2]);
        cm_vert(m, col, a0[0], a0[1], a0[2]); cm_vert(m, col, b1[0], b1[1], b1[2]); cm_vert(m, col, b0[0], b0[1], b0[2]);

        cm_vert(m, cA, ax, ay, az); cm_vert(m, cA, a1[0], a1[1], a1[2]); cm_vert(m, cA, a0[0], a0[1], a0[2]);
        cm_vert(m, cB, bx, by, bz); cm_vert(m, cB, b0[0], b0[1], b0[2]); cm_vert(m, cB, b1[0], b1[1], b1[2]);
    }
}

/* br — общая яркость (фонарик выключен -> почти не видно рук) */
static void build_viewmodel(float br)
{
    mVm.n = 0;
    const int SKr = 168, SKg = 122, SKb = 98;     /* кожа */
    const int SLr = 46,  SLg = 42,  SLb = 38;     /* рукав куртки */

    /* --- фонарик (правая рука) --- */
    float a[3] = { 0.215f, -0.255f, -0.30f };
    float b[3] = { 0.190f, -0.205f, -0.70f };
    float c[3] = { 0.186f, -0.198f, -0.76f };
    tube(&mVm, a[0], a[1], a[2], b[0], b[1], b[2], 0.028f, 0.030f, 8, 30, 30, 34, br);
    tube(&mVm, b[0], b[1], b[2], c[0], c[1], c[2], 0.032f, 0.048f, 8, 44, 44, 50, br);
    /* кнопка */
    tube(&mVm, 0.215f, -0.225f, -0.40f, 0.215f, -0.215f, -0.40f, 0.008f, 0.008f, 6, 120, 20, 20, br);

    /* ладонь вокруг корпуса */
    tube(&mVm, 0.215f, -0.240f, -0.36f, 0.240f, -0.275f, -0.20f, 0.050f, 0.052f, 8, SKr, SKg, SKb, br);
    /* пальцы поверх корпуса */
    for (int i = 0; i < 4; i++) {
        float t = 0.12f + 0.10f * (float)i;
        float px = lerpf(a[0], b[0], t), py = lerpf(a[1], b[1], t), pz = lerpf(a[2], b[2], t);
        tube(&mVm, px - 0.050f, py + 0.036f, pz, px + 0.035f, py + 0.032f, pz,
             0.0135f, 0.0125f, 6, SKr, SKg, SKb, br);
    }
    /* большой палец */
    {
        float p0x = lerpf(a[0], b[0], 0.08f) - 0.045f, p0y = lerpf(a[1], b[1], 0.08f), p0z = lerpf(a[2], b[2], 0.08f);
        float p1x = lerpf(a[0], b[0], 0.28f) - 0.038f, p1y = lerpf(a[1], b[1], 0.28f) + 0.02f, p1z = lerpf(a[2], b[2], 0.28f);
        tube(&mVm, p0x, p0y, p0z, p1x, p1y, p1z, 0.014f, 0.011f, 6, SKr, SKg, SKb, br);
    }
    /* рукав */
    tube(&mVm, 0.240f, -0.275f, -0.20f, 0.265f, -0.310f, -0.15f, 0.062f, 0.062f, 8, 70, 66, 60, br);
    tube(&mVm, 0.265f, -0.310f, -0.15f, 0.440f, -0.550f, 0.22f, 0.058f, 0.078f, 8, SLr, SLg, SLb, br);

    /* --- левая рука (расслаблена) --- */
    tube(&mVm, -0.260f, -0.300f, -0.28f, -0.280f, -0.330f, -0.15f, 0.048f, 0.050f, 8, SKr, SKg, SKb, br);
    for (int i = 0; i < 4; i++) {
        float x = -0.295f + 0.020f * (float)i;
        tube(&mVm, x, -0.290f, -0.30f, x + 0.005f, -0.300f, -0.44f, 0.012f, 0.009f, 6, SKr, SKg, SKb, br);
    }
    tube(&mVm, -0.225f, -0.300f, -0.27f, -0.210f, -0.310f, -0.37f, 0.013f, 0.010f, 6, SKr, SKg, SKb, br);
    tube(&mVm, -0.280f, -0.330f, -0.15f, -0.300f, -0.355f, -0.12f, 0.062f, 0.062f, 8, 70, 66, 60, br);
    tube(&mVm, -0.300f, -0.355f, -0.12f, -0.460f, -0.580f, 0.25f, 0.058f, 0.078f, 8, SLr, SLg, SLb, br);

    sceKernelDcacheWritebackRange(vmBody, sizeof(CVertex) * (unsigned)mVm.n);
}

/* ================================================================== */
/* HUD: мини-шрифт 3x5                                                 */
/* ================================================================== */
typedef struct { char c; unsigned char r[5]; } Glyph;
static const Glyph g_font[] = {
    {'A',{2,5,7,5,5}}, {'B',{6,5,6,5,6}}, {'C',{3,4,4,4,3}}, {'D',{6,5,5,5,6}},
    {'E',{7,4,6,4,7}}, {'F',{7,4,6,4,4}}, {'G',{3,4,5,5,3}}, {'H',{5,5,7,5,5}},
    {'I',{7,2,2,2,7}}, {'J',{1,1,1,5,2}}, {'K',{5,5,6,5,5}}, {'L',{4,4,4,4,7}},
    {'M',{5,7,7,5,5}}, {'N',{6,5,5,5,5}}, {'O',{2,5,5,5,2}}, {'P',{6,5,6,4,4}},
    {'Q',{2,5,5,7,3}}, {'R',{6,5,6,5,5}}, {'S',{3,4,2,1,6}}, {'T',{7,2,2,2,2}},
    {'U',{5,5,5,5,7}}, {'V',{5,5,5,5,2}}, {'W',{5,5,7,7,5}}, {'X',{5,5,2,5,5}},
    {'Y',{5,5,2,2,2}}, {'Z',{7,1,2,4,7}},
    {'0',{7,5,5,5,7}}, {'1',{2,6,2,2,7}}, {'2',{6,1,2,4,7}}, {'3',{6,1,2,1,6}},
    {'4',{5,5,7,1,1}}, {'5',{7,4,6,1,6}}, {'6',{3,4,7,5,7}}, {'7',{7,1,2,2,2}},
    {'8',{7,5,7,5,7}}, {'9',{7,5,7,1,6}},
    {'.',{0,0,0,0,2}}, {'!',{2,2,2,0,2}}, {'?',{6,1,2,0,2}}, {'-',{0,0,7,0,0}},
    {':',{0,2,0,2,0}}, {',',{0,0,0,2,4}}, {' ',{0,0,0,0,0}},
};
#define NUM_GLYPHS ((int)(sizeof(g_font) / sizeof(g_font[0])))

#define HUD_CAP 9000
static CVertex g_hud[HUD_CAP] __attribute__((aligned(16)));
static int g_hudN = 0;

static void hud_quad(float x0, float y0, float x1, float y1, u32 c)
{
    if (g_hudN + 6 > HUD_CAP) return;
    CVertex *v = &g_hud[g_hudN];
    v[0] = (CVertex){ c, x0, y0, 0.0f };
    v[1] = (CVertex){ c, x1, y0, 0.0f };
    v[2] = (CVertex){ c, x1, y1, 0.0f };
    v[3] = (CVertex){ c, x0, y0, 0.0f };
    v[4] = (CVertex){ c, x1, y1, 0.0f };
    v[5] = (CVertex){ c, x0, y1, 0.0f };
    g_hudN += 6;
}

static void text_pass(const char *s, float x, float y, float sc, u32 col)
{
    for (; *s; s++) {
        char ch = *s;
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        const unsigned char *rows = NULL;
        for (int g = 0; g < NUM_GLYPHS; g++)
            if (g_font[g].c == ch) { rows = g_font[g].r; break; }
        if (rows) {
            for (int r = 0; r < 5; r++)
                for (int c = 0; c < 3; c++)
                    if ((rows[r] >> (2 - c)) & 1)
                        hud_quad(x + c * sc, y + r * sc, x + (c + 1) * sc, y + (r + 1) * sc, col);
        }
        x += 4.0f * sc;
    }
}

static void draw_text(const char *s, float x, float y, float sc, u32 col, int center)
{
    float w = (float)strlen(s) * 4.0f * sc - sc;
    if (center) x -= w * 0.5f;
    text_pass(s, x + sc, y + sc, sc, COLA(0, 0, 0, (col >> 24) & 0xff));
    text_pass(s, x, y, sc, col);
}

/* ================================================================== */
/* ИГРОВОЕ СОСТОЯНИЕ                                                   */
/* ================================================================== */
typedef struct {
    float x, z, yaw, pitch;
    float bobPhase, bobAmp;
    float fyaw, fpitch;      /* сглаженное направление фонарика */
    int lastStep;
} Player;

enum { ST_PLAY, ST_END };
enum { I_NONE, I_CRATE, I_DOOR, I_NOTE };

static Player g_player;
static int   g_state, g_paused;
static int   g_flashOn, g_hasKey, g_crateOpened, g_doorOpening;
static float g_doorT, g_endT, g_time;
static char  g_msg[80];
static float g_msgT;
static float g_dripT, g_thudT;
static float g_lamp, g_lampBurst, g_lampNext;

#define EYE_HEIGHT   1.6f
#define MOVE_SPEED   2.0f
#define RUN_MULT     1.7f
#define TURN_SPEED   2.2f
#define PITCH_SPEED  1.5f
#define PITCH_LIMIT  1.0f
#define DEADZONE     0.15f
#define PLAYER_R     0.25f

static void set_msg(const char *s, float t)
{
    strncpy(g_msg, s, sizeof(g_msg) - 1);
    g_msg[sizeof(g_msg) - 1] = 0;
    g_msgT = t;
}

static void reset_game(void)
{
    memset(&g_player, 0, sizeof(g_player));
    g_player.x = 0.0f; g_player.z = -1.5f;
    g_player.lastStep = -1;
    g_state = ST_PLAY; g_paused = 0;
    g_flashOn = 0; g_hasKey = 0; g_crateOpened = 0; g_doorOpening = 0;
    g_doorT = 0.0f; g_endT = 0.0f; g_time = 0.0f;
    g_msg[0] = 0; g_msgT = 0.0f;
    g_dripT = 3.0f; g_thudT = 20.0f;
    g_lamp = 0.5f; g_lampBurst = 0.0f; g_lampNext = 3.0f;
    g_walk[3].active = 0;
    build_viewmodel(0.10f);
}

/* ------------------------------------------------------------------ */
/* Столкновения                                                        */
/* ------------------------------------------------------------------ */
static int point_walkable(float x, float z)
{
    for (int i = 0; i < NUM_WALK; i++) {
        const Rect *r = &g_walk[i];
        if (r->active && x >= r->x0 && x <= r->x1 && z >= r->z0 && z <= r->z1)
            return 1;
    }
    return 0;
}

static int can_stand(float x, float z)
{
    if (!(point_walkable(x - PLAYER_R, z - PLAYER_R) && point_walkable(x + PLAYER_R, z - PLAYER_R) &&
          point_walkable(x - PLAYER_R, z + PLAYER_R) && point_walkable(x + PLAYER_R, z + PLAYER_R)))
        return 0;
    for (int i = 0; i < NUM_SOLID; i++) {
        const Rect *r = &g_solid[i];
        if (x + PLAYER_R > r->x0 && x - PLAYER_R < r->x1 && z + PLAYER_R > r->z0 && z - PLAYER_R < r->z1)
            return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Взаимодействие                                                      */
/* ------------------------------------------------------------------ */
static int find_target(const Player *p)
{
    float fx = sinf(p->yaw), fz = -cosf(p->yaw);
    struct { int id; float x, z, r; } t[3] = {
        { I_CRATE, 5.3f, -12.9f, 1.9f },
        { I_DOOR,  0.0f, -23.95f, 1.9f },
        { I_NOTE,  3.2f,  -8.05f, 1.7f },
    };
    int best = I_NONE;
    float bestDot = 0.8f;
    for (int i = 0; i < 3; i++) {
        if (t[i].id == I_DOOR && g_doorOpening) continue;
        float dx = t[i].x - p->x, dz = t[i].z - p->z;
        float d = sqrtf(dx * dx + dz * dz);
        if (d > t[i].r || d < 0.001f) continue;
        float dot = (dx * fx + dz * fz) / d;
        if (dot > bestDot) { bestDot = dot; best = t[i].id; }
    }
    return best;
}

static const char *target_prompt(int id)
{
    switch (id) {
    case I_CRATE: return g_crateOpened ? "X - SEARCH" : "X - OPEN CRATE";
    case I_DOOR:  return g_hasKey ? "X - UNLOCK DOOR" : "X - TRY DOOR";
    case I_NOTE:  return "X - READ NOTE";
    }
    return "";
}

static void interact(int id)
{
    switch (id) {
    case I_CRATE:
        if (!g_crateOpened) {
            g_crateOpened = 1; g_hasKey = 1;
            set_msg("FOUND A KEY", 3.0f);
            play_sound(sPick, N_PICK, 0.8f, 0.0f);
        } else {
            set_msg("NOTHING ELSE HERE", 2.5f);
            play_sound(sClick, N_CLICK, 0.5f, 0.0f);
        }
        break;
    case I_DOOR:
        if (g_hasKey) {
            g_doorOpening = 1;
            set_msg("UNLOCKED", 2.5f);
            play_sound(sClick, N_CLICK, 0.9f, 0.0f);
            play_sound(sCreak, N_CREAK, 0.9f, 0.0f);
        } else {
            set_msg("LOCKED", 2.5f);
            play_sound(sThud, N_THUD, 0.35f, 0.0f);
        }
        break;
    case I_NOTE:
        set_msg("THE KEY IS IN THE CRATE. DO NOT LOOK BACK.", 5.0f);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Обновление                                                          */
/* ------------------------------------------------------------------ */
static float stick_axis(unsigned char raw)
{
    float v = ((float)raw - 128.0f) / 128.0f;
    float a = fabsf(v);
    if (a < DEADZONE) return 0.0f;
    a = (a - DEADZONE) / (1.0f - DEADZONE);
    return v < 0.0f ? -a : a;
}

static void update_player(Player *p, const SceCtrlData *pad, float dt)
{
    float sx = stick_axis(pad->Lx), sy = stick_axis(pad->Ly);
    float forward = -sy, strafe = 0.0f, turn = 0.0f;

    if (pad->Buttons & PSP_CTRL_LTRIGGER) strafe = sx; else turn = sx;
    if (pad->Buttons & PSP_CTRL_LEFT)  turn -= 1.0f;
    if (pad->Buttons & PSP_CTRL_RIGHT) turn += 1.0f;
    turn = clampf(turn, -1.0f, 1.0f);

    float speed = MOVE_SPEED;
    float bobRate = 9.0f;
    if (pad->Buttons & PSP_CTRL_SQUARE) { speed *= RUN_MULT; bobRate *= 1.3f; }

    p->yaw += turn * TURN_SPEED * dt;
    if (pad->Buttons & PSP_CTRL_UP)   p->pitch += PITCH_SPEED * dt;
    if (pad->Buttons & PSP_CTRL_DOWN) p->pitch -= PITCH_SPEED * dt;
    p->pitch = clampf(p->pitch, -PITCH_LIMIT, PITCH_LIMIT);

    float sn = sinf(p->yaw), cs = cosf(p->yaw);
    float dx = ( sn * forward + cs * strafe) * speed * dt;
    float dz = (-cs * forward + sn * strafe) * speed * dt;
    if (can_stand(p->x + dx, p->z)) p->x += dx;
    if (can_stand(p->x, p->z + dz)) p->z += dz;

    float mag = sqrtf(forward * forward + strafe * strafe);
    if (mag > 1.0f) mag = 1.0f;
    p->bobAmp += (mag - p->bobAmp) * clampf(dt * 8.0f, 0.0f, 1.0f);
    p->bobPhase += dt * mag * bobRate;

    /* шаги: в нижней точке покачивания */
    int step = (int)floorf((p->bobPhase - 1.5707963f) / PI_F);
    if (step != p->lastStep) {
        p->lastStep = step;
        if (mag > 0.3f) {
            if (step & 1) play_sound(sStepA, N_STEP, 0.55f, -0.1f);
            else          play_sound(sStepB, N_STEP, 0.55f,  0.1f);
        }
    }

    /* плавное следование фонарика/рук за камерой */
    float k = clampf(dt * 9.0f, 0.0f, 1.0f);
    p->fyaw   += (p->yaw - p->fyaw) * k;
    p->fpitch += (p->pitch - p->fpitch) * k;
}

static void update_world(float dt)
{
    g_time += dt;
    if (g_msgT > 0.0f) g_msgT -= dt;

    /* мерцающая лампа */
    g_lampNext -= dt;
    if (g_lampNext < 0.0f) { g_lampBurst = 0.3f + rnd01() * 0.5f; g_lampNext = 4.0f + rnd01() * 9.0f; }
    if (g_lampBurst > 0.0f) {
        g_lampBurst -= dt;
        g_lamp = (rnd01() < 0.5f) ? 0.05f : 0.95f;
    } else {
        g_lamp = 0.55f + 0.05f * sinf(g_time * 3.0f);
    }

    /* случайные звуки */
    g_dripT -= dt;
    if (g_dripT < 0.0f) {
        play_sound(sDrip, N_DRIP, 0.35f + rnd01() * 0.2f, rnd01() * 2.0f - 1.0f);
        g_dripT = 3.0f + rnd01() * 6.0f;
    }
    g_thudT -= dt;
    if (g_thudT < 0.0f) {
        play_sound(sThud, N_THUD, 0.45f, rnd01() * 2.0f - 1.0f);
        g_thudT = 18.0f + rnd01() * 25.0f;
    }

    /* дверь */
    if (g_doorOpening && g_doorT < 1.0f) {
        g_doorT += dt / 2.5f;
        if (g_doorT > 1.0f) g_doorT = 1.0f;
    }
    if (g_doorT >= 0.85f) g_walk[3].active = 1;
}

/* ================================================================== */
/* GU                                                                  */
/* ================================================================== */
static unsigned int g_list[262144] __attribute__((aligned(16)));

static void init_gu(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, g_list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void *)0x88000, BUF_WIDTH);
    sceGuDepthBuffer((void *)0x110000, BUF_WIDTH);
    sceGuOffset(2048 - (SCR_WIDTH / 2), 2048 - (SCR_HEIGHT / 2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CW);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void draw_mesh(int tex, const Mesh *m)
{
    if (m->n <= 0) return;
    sceGuTexImage(0, TEX_SIZE, TEX_SIZE, TEX_SIZE, g_tex[tex]);
    sceGumDrawArray(GU_TRIANGLES, LV_FLAGS, m->n, 0, m->v);
}

static void render(int target)
{
    Player *p = &g_player;

    sceGuStart(GU_DIRECT, g_list);
    sceGuClearColor(0xff000000);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    /* камера с покачиванием */
    float sway = cosf(p->bobPhase * 0.5f) * 0.025f * p->bobAmp;
    float cx = p->x + cosf(p->yaw) * sway;
    float cz = p->z + sinf(p->yaw) * sway;
    float cy = EYE_HEIGHT + (0.5f - fabsf(sinf(p->bobPhase))) * 0.07f * p->bobAmp;

    /* ---------------- мир ---------------- */
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_FOG);
    sceGuFog(1.5f, 16.0f, 0x00000000);
    sceGuEnable(GU_TEXTURE_2D);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(75.0f, 16.0f / 9.0f, 0.2f, 40.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumRotateX(p->pitch);
    sceGumRotateY(p->yaw);
    ScePspFVector3 tr = { -cx, -cy, -cz };
    sceGumTranslate(&tr);

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    /* освещение: вершинный цвет = материал */
    sceGuEnable(GU_LIGHTING);
    sceGuColorMaterial(GU_AMBIENT | GU_DIFFUSE);
    sceGuModelColor(0x00000000, 0xffffffff, 0xffffffff, 0xffffffff);
    sceGuAmbient(0xff1c1c1c);

    /* свет 0: фонарик (прожектор). Координаты — мировые, модельная матрица = единичная */
    {
        float cp = cosf(p->fpitch);
        ScePspFVector3 ld = { sinf(p->fyaw) * cp, sinf(p->fpitch), -cosf(p->fyaw) * cp };
        ScePspFVector3 lp = { cx + cosf(p->fyaw) * 0.22f + ld.x * 0.3f,
                              cy - 0.20f + ld.y * 0.3f,
                              cz + sinf(p->fyaw) * 0.22f + ld.z * 0.3f };
        if (g_flashOn) {
            sceGuEnable(GU_LIGHT0);
            sceGuLight(0, GU_SPOTLIGHT, GU_DIFFUSE, &lp);
            sceGuLightSpot(0, &ld, 6.0f, 0.82f);
            sceGuLightColor(0, GU_DIFFUSE, COL(255, 240, 208));
            sceGuLightAtt(0, 0.4f, 0.08f, 0.04f);
        } else {
            sceGuDisable(GU_LIGHT0);
        }
    }
    /* свет 1: сломанная лампа на потолке коридора */
    if (g_lamp > 0.1f) {
        ScePspFVector3 lp1 = { 0.0f, 2.6f, -14.0f };
        int r = (int)(110.0f * g_lamp), g = (int)(140.0f * g_lamp), b = (int)(100.0f * g_lamp);
        sceGuEnable(GU_LIGHT1);
        sceGuLight(1, GU_POINTLIGHT, GU_DIFFUSE, &lp1);
        sceGuLightColor(1, GU_DIFFUSE, COL(r, g, b));
        sceGuLightAtt(1, 0.5f, 0.10f, 0.12f);
    } else {
        sceGuDisable(GU_LIGHT1);
    }

    draw_mesh(T_WALL,  &mWall);
    draw_mesh(T_FLOOR, &mFloor);
    draw_mesh(T_CEIL,  &mCeil);
    draw_mesh(T_WOOD,  &mWood);
    draw_mesh(T_METAL, &mMetal);
    draw_mesh(T_PAPER, &mPaper);

    /* дверь уезжает вверх */
    {
        ScePspFVector3 dt3 = { 0.0f, g_doorT * 2.4f, 0.0f };
        sceGumMatrixMode(GU_MODEL);
        sceGumLoadIdentity();
        sceGumTranslate(&dt3);
        draw_mesh(T_METAL, &mDoor);
        sceGumLoadIdentity();
    }

    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_LIGHT0);
    sceGuDisable(GU_LIGHT1);
    sceGuDisable(GU_TEXTURE_2D);

    /* плафон сломанной лампы (самосветящийся) */
    {
        static CVertex lampV[6] __attribute__((aligned(16)));
        u32 lc = pack_rgb(190.0f * g_lamp, 225.0f * g_lamp, 175.0f * g_lamp);
        float y = WALL_H - 0.02f;
        lampV[0] = (CVertex){ lc, -0.4f, y, -14.12f };
        lampV[1] = (CVertex){ lc,  0.4f, y, -14.12f };
        lampV[2] = (CVertex){ lc,  0.4f, y, -13.88f };
        lampV[3] = (CVertex){ lc, -0.4f, y, -14.12f };
        lampV[4] = (CVertex){ lc,  0.4f, y, -13.88f };
        lampV[5] = (CVertex){ lc, -0.4f, y, -13.88f };
        sceKernelDcacheWritebackRange(lampV, sizeof(lampV));
        sceGumDrawArray(GU_TRIANGLES, CV_FLAGS, 6, 0, lampV);
    }

    /* ---------------- руки и фонарик ---------------- */
    sceGuDisable(GU_FOG);
    sceGuClearDepth(0);
    sceGuClear(GU_DEPTH_BUFFER_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(65.0f, 16.0f / 9.0f, 0.05f, 3.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    {
        float lagX = clampf((p->fyaw - p->yaw) * 0.35f, -0.08f, 0.08f);
        float lagY = clampf((p->fpitch - p->pitch) * 0.35f, -0.08f, 0.08f);
        float bx = lagX + cosf(p->bobPhase * 0.5f) * 0.010f * p->bobAmp;
        float by = lagY - fabsf(sinf(p->bobPhase)) * 0.014f * p->bobAmp + sinf(g_time * 1.6f) * 0.004f;
        ScePspFVector3 vt = { bx, by, 0.0f };
        sceGumTranslate(&vt);
    }
    if (mVm.n > 0)
        sceGumDrawArray(GU_TRIANGLES, CV_FLAGS, mVm.n, 0, mVm.v);

    /* ---------------- HUD ---------------- */
    g_hudN = 0;
    {
        float cxs = SCR_WIDTH * 0.5f, cys = SCR_HEIGHT * 0.5f;
        float cs = target ? 3.0f : 1.5f;
        if (g_state == ST_PLAY && !g_paused)
            hud_quad(cxs - cs, cys - cs, cxs + cs, cys + cs, COLA(220, 220, 220, target ? 200 : 110));

        if (g_state == ST_PLAY && !g_paused) {
            if (target) draw_text(target_prompt(target), cxs, cys + 22.0f, 2.0f, COLA(230, 230, 230, 230), 1);
            if (g_msgT > 0.0f && g_msg[0]) draw_text(g_msg, cxs, SCR_HEIGHT - 44.0f, 2.0f, COLA(220, 200, 150, 255), 1);
            if (g_hasKey) draw_text("KEY", 8.0f, SCR_HEIGHT - 16.0f, 2.0f, COLA(200, 180, 90, 230), 0);
        }
        if (g_paused && g_state == ST_PLAY) {
            hud_quad(0, 0, SCR_WIDTH, SCR_HEIGHT, COLA(0, 0, 0, 150));
            draw_text("PAUSED", cxs, cys - 20.0f, 4.0f, COLA(230, 230, 230, 255), 1);
            draw_text("START - RESUME", cxs, cys + 14.0f, 2.0f, COLA(180, 180, 180, 255), 1);
        }
        if (g_state == ST_END) {
            float a = clampf(g_endT / 2.5f, 0.0f, 1.0f);
            hud_quad(0, 0, SCR_WIDTH, SCR_HEIGHT, COLA(0, 0, 0, (int)(a * 255.0f)));
            if (g_endT > 3.0f) {
                draw_text("END OF DEMO", cxs, cys - 30.0f, 5.0f, COLA(220, 220, 220, 255), 1);
                draw_text("THANK YOU FOR PLAYING", cxs, cys + 10.0f, 2.0f, COLA(170, 170, 170, 255), 1);
            }
            if (g_endT > 5.0f)
                draw_text("X - RESTART", cxs, cys + 40.0f, 2.0f, COLA(150, 150, 150, 255), 1);
        }
    }
    if (g_hudN > 0) {
        sceKernelDcacheWritebackRange(g_hud, sizeof(CVertex) * (unsigned)g_hudN);
        sceGuDisable(GU_DEPTH_TEST);
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        sceGuDrawArray(GU_TRIANGLES, CV2D_FLAGS, g_hudN, 0, g_hud);
        sceGuDisable(GU_BLEND);
        sceGuEnable(GU_DEPTH_TEST);
    }

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

/* ================================================================== */
/* main                                                                */
/* ================================================================== */
int main(int argc, char *argv[])
{
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    gen_textures();
    build_level();
    init_gu();
    start_audio();
    reset_game();

    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    unsigned int oldButtons = 0;
    u32 lastTime = sceKernelGetSystemTimeLow();

    while (1) {
        u32 now = sceKernelGetSystemTimeLow();
        float dt = (float)(now - lastTime) / 1000000.0f;
        lastTime = now;
        if (dt > 0.1f) dt = 0.1f;

        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~oldButtons;
        oldButtons = pad.Buttons;

        int target = I_NONE;

        if (g_state == ST_PLAY) {
            if (pressed & PSP_CTRL_START) g_paused = !g_paused;

            if (!g_paused) {
                update_player(&g_player, &pad, dt);
                update_world(dt);

                if (pressed & PSP_CTRL_RTRIGGER) {
                    g_flashOn = !g_flashOn;
                    build_viewmodel(g_flashOn ? 0.85f : 0.10f);
                    play_sound(sClick, N_CLICK, 0.7f, 0.2f);
                }

                target = find_target(&g_player);
                if ((pressed & PSP_CTRL_CROSS) && target != I_NONE)
                    interact(target);

                if (g_walk[3].active && g_player.z < -27.5f) {
                    g_state = ST_END;
                    g_endT = 0.0f;
                }
            }
        } else {
            g_endT += dt;
            if (g_endT > 5.0f && (pressed & PSP_CTRL_CROSS))
                reset_game();
        }

        render(target);
    }

    g_audioRun = 0;
    sceKernelExitGame();
    return 0;
}
