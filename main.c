// Acik Dunya Surus - PSP portu (PSPSDK / pspdev). Web surumunun sade hali.
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("AcikDunya", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define BW 512
#define SW 480
#define SH 272
#define CS 80.0f
#define PI 3.14159265f
#define RGB(r,g,b) (0xff000000u|((b)<<16)|((g)<<8)|(r))

static unsigned int __attribute__((aligned(16))) list[262144];
typedef struct { unsigned int c; float x, y, z; } V;

static unsigned vramOff = 0;
static void *vramAlloc(unsigned w, unsigned h, int psm) {
    unsigned s = w * h * (psm == GU_PSM_4444 ? 2 : 4);
    void *r = (void *)vramOff; vramOff += s; return r;
}

static int running = 1;
static int exitCb(int a, int b, void *c) { running = 0; return 0; }
static int cbThread(SceSize args, void *argp) {
    int cb = sceKernelCreateCallback("exit", exitCb, NULL);
    sceKernelRegisterExitCallback(cb);
    sceKernelSleepThreadCB();
    return 0;
}

static const unsigned pal[6] = {
    RGB(217,138,108), RGB(127,166,201), RGB(201,179,127),
    RGB(157,140,196), RGB(143,196,160), RGB(224,196,196) };

static unsigned shade(unsigned c, float k) {
    unsigned r = (unsigned)((c & 255) * k), g = (unsigned)(((c >> 8) & 255) * k), b = (unsigned)(((c >> 16) & 255) * k);
    return 0xff000000u | (b << 16) | (g << 8) | r;
}

// Merkezi (x,y,z) olan kutu cizer.
static void box(float x, float y, float z, float w, float h, float d, unsigned col) {
    static const float cx[8] = {-1,1,1,-1,-1,1,1,-1}, cy[8] = {-1,-1,1,1,-1,-1,1,1}, cz[8] = {-1,-1,-1,-1,1,1,1,1};
    static const int fi[6][4] = {{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0}};
    static const float fs[6] = {.8f,.8f,.65f,.65f,1.0f,.5f};
    V *v = (V *)sceGuGetMemory(36 * sizeof(V));
    float hx = w / 2, hy = h / 2, hz = d / 2; int n = 0;
    for (int f = 0; f < 6; f++) {
        unsigned c = shade(col, fs[f]);
        int a = fi[f][0], b = fi[f][1], cc = fi[f][2], dd = fi[f][3], idx[6] = {a,b,cc,a,cc,dd};
        for (int k = 0; k < 6; k++) {
            int i = idx[k];
            v[n].c = c; v[n].x = x + cx[i] * hx; v[n].y = y + cy[i] * hy; v[n].z = z + cz[i] * hz; n++;
        }
    }
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 36, 0, v);
}

static void rect(float x, float y, float w, float h, unsigned col) {
    V *v = (V *)sceGuGetMemory(2 * sizeof(V));
    v[0].c = col; v[0].x = x; v[0].y = y; v[0].z = 0;
    v[1].c = col; v[1].x = x + w; v[1].y = y + h; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
}

static unsigned hsh(int a, int b, int c) {
    unsigned x = (unsigned)a * 73856093u ^ (unsigned)b * 19349663u ^ (unsigned)c * 83492791u;
    x ^= x >> 13; x *= 0x5bd1e995u; x ^= x >> 15; return x;
}
// Blok (i,j) icindeki q. bina (varsa).
static int bld(int i, int j, int q, float *x, float *z, float *w, float *d, float *h, unsigned *col) {
    unsigned a = hsh(i, j, q);
    if (a % 6 == 0) return 0;
    *w = 9 + (a >> 3) % 13; *d = 9 + (a >> 8) % 13;
    *h = 6 + (float)((a >> 13) % 48) * (((a >> 20) % 3 == 0) ? 2.0f : 1.0f);
    *x = i * CS + 11 + (q & 1) * 29 + 14.5f; *z = j * CS + 11 + (q >> 1) * 29 + 14.5f;
    *col = pal[a % 6]; return 1;
}

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// Oyuncu
static float px = 0, pz = 0, h = 0, ch = 0, v = 0, lv = 0, s = 0, alt = 0, vy = 0;
static int fly = 0, hits = 0, chaos = 0, tcN = 16, td = 0;
static unsigned fcount = 0;

// --- Yikilabilir binalar ---
#define MAXB 256
static int brkTab[MAXB][3], nbrk = 0;
static float brkT[MAXB];
static float isBroken(int i, int j, int q) {
    for (int k = 0; k < MAXB; k++) if (brkT[k] > 0 && brkTab[k][0] == i && brkTab[k][1] == j && brkTab[k][2] == q) return brkT[k];
    return 0;
}
typedef struct { float x, y, z, vx, vy, vz, sz, t; unsigned col; int dust; } D;
#define MAXD 220
static D deb[MAXD]; static int nd = 0; static float shake = 0;
static float rf(void) { return (rand() % 1000) / 1000.0f; }
static void addD(float x, float y, float z, float vx, float vy, float vz, float sz, float t, unsigned col, int dust) {
    D *d = &deb[nd++ % MAXD];
    d->x = x; d->y = y; d->z = z; d->vx = vx; d->vy = vy; d->vz = vz; d->sz = sz; d->t = t; d->col = col; d->dust = dust;
}
static void breakB(int i, int j, int q, float bx, float bz, float bw, float bd, float bh, unsigned bc, float pvx, float pvz, float pw) {
    int slot = nbrk++ % MAXB; brkTab[slot][0] = i; brkTab[slot][1] = j; brkTab[slot][2] = q; brkT[slot] = chaos ? 25.0f : 1e9f;
    float sz0 = (bh > 14 ? 1.6f : .8f) * (.8f + pw * .4f), sp = .35f + pw * .35f;
    int n = 14 + (int)(pw * 8);
    for (int k = 0; k < n; k++)
        addD(bx + (rf() - .5f) * bw, rf() * bh * (pw > 1 ? 1.0f : .5f), bz + (rf() - .5f) * bd, pvx * sp + (rf() - .5f) * (10 + pw * 10), 3 + rf() * (8 + pw * 8), pvz * sp + (rf() - .5f) * (10 + pw * 10), .4f + rf() * sz0, 4, bc, 0);
    for (int k = 0; k < 10; k++)
        addD(bx + (rf() - .5f) * bw, 1 + rf() * bh * .5f, bz + (rf() - .5f) * bd, (rf() - .5f) * 6, 1 + rf() * 4, (rf() - .5f) * 6, 1.5f, .7f, RGB(190,180,160), 1);
    if (pw > 1.4f) for (int k = 0; k < 3; k++)
        addD(bx + (rf() - .5f) * bw, 2 + rf() * bh * .3f, bz + (rf() - .5f) * bd, (rf() - .5f) * 8, 2 + rf() * 5, (rf() - .5f) * 8, 2.2f, .5f, RGB(255,140,40), 1);
    shake = fmaxf(shake, .6f + pw * 1.1f); hits++;
}
static void updDebris(float dt) {
    shake = fmaxf(0, shake - dt * 3);
    for (int k = 0; k < MAXB; k++) if (brkT[k] > 0 && brkT[k] < 1e8f) brkT[k] -= dt;
    for (int k = 0; k < MAXD; k++) {
        D *d = &deb[k]; if (d->t <= 0) continue; d->t -= dt;
        if (d->dust) { d->sz += dt * 9; d->x += d->vx * dt; d->y += d->vy * dt; d->z += d->vz * dt; continue; }
        d->vy -= 25 * dt; d->x += d->vx * dt; d->y += d->vy * dt; d->z += d->vz * dt;
        if (d->y < d->sz * .5f) { d->y = d->sz * .5f; d->vy *= -.3f; d->vx *= .7f; d->vz *= .7f; }
    }
}
static void drawDebris(void) {
    sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();
    for (int k = 0; k < MAXD; k++) {
        D *d = &deb[k]; if (d->t <= 0) continue;
        box(d->x, d->y, d->z, d->sz, d->dust ? d->sz * .7f : d->sz * .6f, d->sz, d->col);
    }
}

// Trafik
#define TCN 40
typedef struct { int ax, d, st; float x, y, z, sp, vx, vy, vz, rx, rz, t, age; unsigned col; } T;
static T tc[TCN];
static void tplace(T *t, int init) {
    t->ax = rand() & 1; t->d = (rand() & 1) ? 1 : -1; t->sp = (14 + rand() % 24) * (chaos ? 2.2f : 1.0f); t->st = 0; t->y = 0; t->rx = t->rz = 0; t->age = 0;
    float off = ((rand() & 1) ? -1 : 1) * (init ? 40 + rand() % (chaos ? 100 : 130) : (chaos ? 100 : 130) + rand() % 30);
    int k = rand() % 3 - 1; float ln = t->d * 3.6f;
    if (t->ax == 0) { t->x = px + off; t->z = (floorf(pz / CS + .5f) + k) * CS + ln; }
    else { t->x = (floorf(px / CS + .5f) + k) * CS - ln; t->z = pz + off; }
    t->col = pal[rand() % 6];
}
static float tyaw(T *t) { return t->ax == 0 ? -t->d * PI / 2 : (t->d > 0 ? PI : 0); }

// Kaos modu: ucan arabalar diger arabalara ve binalara carpar.
static void flyChaos(T *t, int self) {
    float hs = sqrtf(t->vx * t->vx + t->vz * t->vz);
    if (t->y < 5 && hs > 10) for (int k = 0; k < tcN; k++) {
        T *o = &tc[k]; if (k == self || o->st != 0) continue;
        float dx = o->x - t->x, dz = o->z - t->z;
        if (dx * dx + dz * dz < 20 && fabsf(o->y - t->y) < 4) {
            o->st = 1; o->t = 5.5f; o->age = 0;
            o->vx = t->vx * .85f + (rand() % 160 - 80) / 10.0f; o->vz = t->vz * .85f + (rand() % 160 - 80) / 10.0f;
            o->vy = 12 + (rand() % 140) / 10.0f + hs * .12f;
            o->rx = (rand() % 100 - 50) / 50.0f * 40; o->rz = (rand() % 100 - 50) / 50.0f * 40;
            t->vx *= .7f; t->vz *= .7f; td++; hits++; shake = fmaxf(shake, .9f); break;
        }
    }
    if (hs > 14) {
        int ci = (int)floorf(t->x / CS), cj = (int)floorf(t->z / CS);
        for (int i = ci - 1; i <= ci + 1; i++) for (int j = cj - 1; j <= cj + 1; j++) for (int q = 0; q < 4; q++) {
            float bx, bz, bw, bd, bh; unsigned bc;
            if (!bld(i, j, q, &bx, &bz, &bw, &bd, &bh, &bc) || t->y > bh || isBroken(i, j, q)) continue;
            if (fabsf(t->x - bx) < bw / 2 + 1.6f && fabsf(t->z - bz) < bd / 2 + 1.6f) {
                breakB(i, j, q, bx, bz, bw, bd, bh, bc, t->vx, t->vz, fminf(2.5f, hs / 30));
                t->vx *= .5f; t->vz *= .5f; if (t->vy < 7) t->vy = 7; return;
            }
        }
    }
}

static void carModel(unsigned body) {
    box(0, .7f, 0, 2, .6f, 4.3f, body);
    box(0, 1.25f, .3f, 1.7f, .5f, 2.2f, RGB(30,45,65));
    box(-1.05f, .45f, -1.35f, .35f, .9f, .9f, RGB(20,20,24)); box(1.05f, .45f, -1.35f, .35f, .9f, .9f, RGB(20,20,24));
    box(-1.05f, .45f, 1.35f, .35f, .9f, .9f, RGB(20,20,24));  box(1.05f, .45f, 1.35f, .35f, .9f, .9f, RGB(20,20,24));
    box(0, .8f, -2.2f, 1.6f, .12f, .1f, RGB(240,250,255));
    box(0, .8f, 2.2f, 1.8f, .12f, .1f, RGB(255,40,40));
}

static int onRoad(float x, float z) {
    float rx = fmodf(fmodf(x, CS) + CS, CS), rz = fmodf(fmodf(z, CS) + CS, CS);
    return rx < 7 || rx > CS - 7 || rz < 7 || rz > CS - 7;
}

int main(void) {
    int th = sceKernelCreateThread("cb", cbThread, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    pspDebugScreenInit();
    srand(12345);

    void *fbp0 = vramAlloc(BW, SH, GU_PSM_8888), *fbp1 = vramAlloc(BW, SH, GU_PSM_8888), *zbp = vramAlloc(BW, SH, GU_PSM_4444);
    sceGuInit(); sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, BW); sceGuDispBuffer(SW, SH, fbp1, BW); sceGuDepthBuffer(zbp, BW);
    sceGuOffset(2048 - (SW / 2), 2048 - (SH / 2)); sceGuViewport(2048, 2048, SW, SH);
    sceGuDepthRange(65535, 0); sceGuScissor(0, 0, SW, SH); sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL); sceGuEnable(GU_DEPTH_TEST); sceGuDisable(GU_CULL_FACE); sceGuEnable(GU_CLIP_PLANES); sceGuShadeModel(GU_SMOOTH);
    sceGuFinish(); sceGuSync(0, 0); sceDisplayWaitVblankStart(); sceGuDisplay(GU_TRUE);

    sceCtrlSetSamplingCycle(0); sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    for (int i = 0; i < TCN; i++) tplace(&tc[i], 1);
    unsigned lastBtn = 0; const float dt = 1.0f / 60.0f;

    while (running) {
        SceCtrlData pad; sceCtrlPeekBufferPositive(&pad, 1);
        int gas = pad.Buttons & PSP_CTRL_CROSS, brk = pad.Buttons & PSP_CTRL_SQUARE, hb = pad.Buttons & PSP_CTRL_CIRCLE, nt = pad.Buttons & PSP_CTRL_RTRIGGER;
        if ((pad.Buttons & PSP_CTRL_TRIANGLE) && !(lastBtn & PSP_CTRL_TRIANGLE)) { fly = !fly; if (fly && alt < .1f) vy = 10; }
        if ((pad.Buttons & PSP_CTRL_START) && !(lastBtn & PSP_CTRL_START)) { chaos = !chaos; tcN = chaos ? 40 : 16; for (int i = 0; i < TCN; i++) tplace(&tc[i], 1); td = 0; }
        fcount++;
        lastBtn = pad.Buttons;
        float tgt = -((float)pad.Lx - 128.0f) / 128.0f; if (fabsf(tgt) < .15f) tgt = 0;
        if (pad.Buttons & PSP_CTRL_LEFT) tgt = 1; if (pad.Buttons & PSP_CTRL_RIGHT) tgt = -1;

        int on = onRoad(px, pz) || alt > 1;
        float mx = (on ? 62.0f : 26.0f) * (nt ? 1.85f : 1.0f);
        if (gas || nt) v += (nt ? 70 + fmaxf(0, v) * .35f : (v < mx ? 26 * (1 - fmaxf(0, v) / 70) : 0)) * dt;
        if (brk) { v -= (v > 1 ? 48 : 16) * dt; if (v < -16) v = -16; }
        if (!gas && !brk && !nt) v *= 1 - .7f * dt;
        if (v > mx && !nt) v -= (v - mx) * (on ? .15f : 1.2f) * dt;
        if (hb) v *= 1 - .25f * dt;
        s += (tgt - s) * fminf(1, dt * 8);
        float dh = s * (2.1f / (1 + fabsf(v) / 22)) * clampf(v / 8, -1, 1) * dt * ((hb && fabsf(v) > 8) ? 1.5f : 1);
        h += dh;
        { float c = cosf(dh), sn = sinf(dh), v2 = v * c - lv * sn; lv = v * sn + lv * c; v = v2; }
        int dr = hb || (fabsf(v) > 38 && fabsf(s) > .7f);
        lv *= expf(-(dr ? 1.4f : 9.0f) * dt);
        if (dr && fabsf(lv) > 3) v -= fabsf(lv) * .12f * dt;
        px += (-sinf(h) * v + cosf(h) * lv) * dt; pz += (-cosf(h) * v - sinf(h) * lv) * dt;

        if (fly) {
            float tv = ((pad.Buttons & PSP_CTRL_UP) ? 1.0f : 0.0f) - ((pad.Buttons & PSP_CTRL_DOWN) ? 1.0f : 0.0f);
            vy += (tv * 26 - vy) * fminf(1, dt * 5); if (alt > 1) v -= vy * .12f * dt;
            alt += vy * dt; if (alt < 0) { alt = 0; vy = 0; }
        } else if (alt > 0) { vy -= 35 * dt; alt += vy * dt; if (alt <= 0) { alt = 0; vy = 0; } }

        // Bina carpismasi (3x3 blok)
        int ci = (int)floorf(px / CS), cj = (int)floorf(pz / CS);
        for (int i = ci - 1; i <= ci + 1; i++) for (int j = cj - 1; j <= cj + 1; j++) for (int q = 0; q < 4; q++) {
            float bx, bz, bw, bd, bh; unsigned bc;
            if (!bld(i, j, q, &bx, &bz, &bw, &bd, &bh, &bc) || alt > bh + .8f || isBroken(i, j, q)) continue;
            float dx = px - bx, dz = pz - bz, ox = bw / 2 + 1.4f - fabsf(dx), oz = bd / 2 + 1.4f - fabsf(dz);
            if (ox > 0 && oz > 0) {
                if (fabsf(v) > 12) { float pvx0 = -sinf(h) * v + cosf(h) * lv, pvz0 = -cosf(h) * v - sinf(h) * lv; breakB(i, j, q, bx, bz, bw, bd, bh, bc, pvx0, pvz0, chaos ? fminf(2.5f, fabsf(v) / 28) : .8f); }
                else { if (ox < oz) px += dx > 0 ? ox : -ox; else pz += dz > 0 ? oz : -oz; if (fabsf(v) > 3) { v *= .5f; lv *= .5f; } }
            }
        }

        // Trafik
        float pvx = -sinf(h) * v + cosf(h) * lv, pvz = -cosf(h) * v - sinf(h) * lv;
        for (int i = 0; i < tcN; i++) {
            T *t = &tc[i];
            if (t->st == 0) {
                float dir = t->d * t->sp * dt; if (t->ax == 0) t->x += dir; else t->z += dir;
                float dx = t->x - px, dz = t->z - pz;
                if (fabsf(dx) > (chaos ? 150 : 190) || fabsf(dz) > (chaos ? 150 : 190)) { tplace(t, 0); continue; }
                float tvx = t->ax == 0 ? t->d * t->sp : 0, tvz = t->ax == 1 ? t->d * t->sp : 0;
                float rel = sqrtf((pvx - tvx) * (pvx - tvx) + (pvz - tvz) * (pvz - tvz));
                if (alt < 3 && dx * dx + dz * dz < 3.4f * 3.4f && rel > 2) {
                    t->st = 1; t->t = 4; hits++;
                    float K = chaos ? 2.6f : 1.0f, rs = chaos ? 44.0f : 10.0f;
                    t->vx = (pvx * 1.1f + tvx * .3f) * K + (chaos ? (rand() % 300 - 150) / 10.0f : 0); t->vz = (pvz * 1.1f + tvz * .3f) * K + (chaos ? (rand() % 300 - 150) / 10.0f : 0);
                    t->vy = (10 + (rand() % 80) / 10.0f + rel * .12f) * (chaos ? 2.0f : 1.0f);
                    t->rx = ((rand() % 100) - 50) / 50.0f * rs; t->rz = ((rand() % 100) - 50) / 50.0f * rs; t->age = 0;
                    if (chaos) { t->t = 5.5f; td++; shake = fmaxf(shake, 1.8f); }
                }
            } else {
                t->vy -= 25 * dt; t->x += t->vx * dt; t->y += t->vy * dt; t->z += t->vz * dt;
                if (t->y < 0) { t->y = 0; t->vy *= -.35f; t->vx *= .6f; t->vz *= .6f; t->rx *= .6f; t->rz *= .6f; }
                t->age += dt; if (chaos) flyChaos(t, i); t->t -= dt; if (t->t <= 0) tplace(t, 0);
            }
        }

        updDebris(dt);
        // Kamera
        float dd = atan2f(sinf(h - ch), cosf(h - ch)); ch += dd * fminf(1, dt * 3.5f);
        sceGuStart(GU_DIRECT, list);
        sceGuEnable(GU_DEPTH_TEST); sceGuClearColor(RGB(168,208,234)); sceGuClearDepth(0);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
        sceGumMatrixMode(GU_PROJECTION); sceGumLoadIdentity(); sceGumPerspective(68.0f + fminf(40.0f, fabsf(v) * .3f), 16.0f / 9.0f, .5f, 320.0f);
        sceGumMatrixMode(GU_VIEW); sceGumLoadIdentity();
        { ScePspFVector3 eye = { px + sinf(ch) * 9.5f + ((rand() % 100) - 50) * .01f * shake, 4.3f + alt + fabsf(v) * .03f + ((rand() % 100) - 50) * .01f * shake, pz + cosf(ch) * 9.5f };
          ScePspFVector3 ctr = { px - sinf(h) * 4, 1.3f + alt + vy * .2f, pz - cosf(h) * 4 };
          ScePspFVector3 up = { 0, 1, 0 }; sceGumLookAt(&eye, &ctr, &up); }
        sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();

        float gx0 = floorf(px / 60) * 60, gz0 = floorf(pz / 60) * 60;
        for (int a = -3; a <= 3; a++) for (int b = -3; b <= 3; b++) box(gx0 + a * 60 + 30, -.5f, gz0 + b * 60 + 30, 60, 1, 60, RGB(111,174,85));
        for (int i = ci - 3; i <= ci + 4; i++) {
            for (int b = -4; b <= 4; b++) box(i * CS, .05f, gz0 + b * 60 + 30, 14, .1f, 60, RGB(58,61,68));
            float z0 = floorf(pz / 20) * 20;
            for (int k = -5; k <= 5; k++) { float zc = z0 + k * 20, rz = fmodf(fmodf(zc, CS) + CS, CS); if (rz < 9 || rz > CS - 9) continue; box(i * CS, .12f, zc, .5f, .1f, 5, RGB(243,232,176)); }
        }
        for (int j = cj - 3; j <= cj + 4; j++) {
            for (int a = -4; a <= 4; a++) box(gx0 + a * 60 + 30, .06f, j * CS, 60, .1f, 14, RGB(58,61,68));
            float x0 = floorf(px / 20) * 20;
            for (int k = -5; k <= 5; k++) { float xc = x0 + k * 20, rx = fmodf(fmodf(xc, CS) + CS, CS); if (rx < 9 || rx > CS - 9) continue; box(xc, .13f, j * CS, 5, .1f, .5f, RGB(243,232,176)); }
        }
        for (int i = ci - 3; i <= ci + 3; i++) for (int j = cj - 3; j <= cj + 3; j++) for (int q = 0; q < 4; q++) {
            float bx, bz, bw, bd, bh; unsigned bc;
            if (bld(i, j, q, &bx, &bz, &bw, &bd, &bh, &bc)) {
                { float bt = isBroken(i, j, q);
                  if (bt > 0 && bt < 1.5f) { float kk = 1 - bt / 1.5f; box(bx, bh * kk / 2, bz, bw, bh * kk, bd, bc); }
                  else if (bt > 0) box(bx, .6f, bz, bw, 1.2f, bd, RGB(110,100,90));
                  else box(bx, bh / 2, bz, bw, bh, bd, bc); }
            }
        }
        for (int i = 0; i < tcN; i++) {
            T *t = &tc[i]; ScePspFVector3 p = { t->x, t->y, t->z };
            sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity(); sceGumTranslate(&p); sceGumRotateY(tyaw(t));
            if (t->st) { sceGumRotateX(t->rx * t->age * .3f); sceGumRotateZ(t->rz * t->age * .3f); }
            carModel(t->col);
        }
        drawDebris();
        { ScePspFVector3 p = { px, alt, pz };
          sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity(); sceGumTranslate(&p); sceGumRotateY(h);
          sceGumRotateZ(s * (alt > 1 ? .5f : .035f)); if (alt > .5f) sceGumRotateX(atan2f(vy, fmaxf(fabsf(v), 14)) * .9f);
          carModel(RGB(244,244,246)); }

        // Ekran gostergeleri (yazi yerine cubuklar)
        sceGuDisable(GU_DEPTH_TEST);
        rect(10, 250, 204, 10, RGB(30,30,30));
        { float f = clampf(fabsf(v) / 110.0f, 0, 1); rect(12, 252, 200 * f, 6, f < .6f ? RGB(80,220,90) : (f < .85f ? RGB(240,200,40) : RGB(240,60,40))); }
        if (nt) rect(10, 232, 60, 10, RGB(60,170,255));
        if (fly) { rect(10, 10, 14, 14, RGB(180,120,255)); rect(30, 10, 4 + clampf(alt, 0, 150), 14, RGB(120,200,255)); }
        for (int k = 0; k < hits && k < 24; k++) rect(466 - k * 10, 10, 8, 8, RGB(255,90,60));
        if (chaos) { rect(190, 4, 100, 6, ((fcount >> 3) & 1) ? RGB(255,60,40) : RGB(255,170,40)); for (int k = 0; k < td && k < 24; k++) rect(466 - k * 10, 22, 8, 8, RGB(255,210,40)); }

        sceGuFinish(); sceGuSync(0, 0);
        pspDebugScreenSetOffset((int)fbp0); pspDebugScreenSetXY(1, 1);
        pspDebugScreenSetTextColor(0xffffffff);
        pspDebugScreenPrintf("HIZ %d km/s  %s  CARPISMA %d", (int)(fabsf(v) * 3.6f), fly ? "UCUS" : "YER", hits);
        pspDebugScreenSetXY(1, 33);
        pspDebugScreenPrintf("X gaz  []fren  O drift  R nitro  /\\ ucus  UP/DOWN yuksel/alcal");
        sceDisplayWaitVblankStart();
        fbp0 = sceGuSwapBuffers();
    }
    sceGuTerm(); sceKernelExitGame();
    return 0;
}
