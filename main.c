/*
 * Save The Shapes - PS2 fan demake (original code, no assets from the PC game)
 * Toolchain: PS2SDK + gsKit. Renderer: raycaster, one gsKit sprite per column.
 *
 * Controls: left stick / D-pad = move + turn. START = restart.
 * Goal: rescue all 5 shapes. Diamond only moves when you are NOT looking at it.
 */
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <math.h>
#include <stdlib.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>

#define MAPW 16
#define MAPH 16
#define MAX_COLS 320
#define NSHAPES 5
#define LUT 1024
#define PLANE 0.66f

static const char *map[MAPH] = {
    "################",
    "#......#.......#",
    "#......#.......#",
    "#..............#",
    "#......#.......#",
    "###.####..######",
    "#......#.......#",
    "#......#.......#",
    "#..............#",
    "#......#.......#",
    "####.###..######",
    "#......#.......#",
    "#..............#",
    "#......#.......#",
    "#......#.......#",
    "################"};

typedef struct { float x, y; int type; int alive; } Shape;

static GSGLOBAL *gs;
static float sinT[LUT], cosT[LUT], camX[MAX_COLS], zbuf[MAX_COLS];
static float octX[9], octY[9];
static int ncols, W, H;
static float px, py;
static int ang;
static Shape shapes[NSHAPES], diamond;
static int collected, state; /* 0 = playing, 1 = win, 2 = lose */
static char padBuf[256] __attribute__((aligned(64)));

static inline u64 rgb(int r, int g, int b) { return GS_SETREG_RGBAQ(r, g, b, 0x80, 0x00); }

static int solid(int x, int y)
{
    if (x < 0 || y < 0 || x >= MAPW || y >= MAPH) return 1;
    return map[y][x] == '#';
}

static void reset_game(void)
{
    static const float pos[NSHAPES][2] = {{12.5f, 2.5f}, {3.5f, 7.5f}, {12.5f, 8.5f}, {4.5f, 13.5f}, {12.5f, 13.5f}};
    int i;
    px = 2.5f; py = 2.5f; ang = 0;
    for (i = 0; i < NSHAPES; i++) {
        shapes[i].x = pos[i][0]; shapes[i].y = pos[i][1];
        shapes[i].type = i; shapes[i].alive = 1;
    }
    diamond.x = 13.5f; diamond.y = 11.5f; diamond.type = 5; diamond.alive = 1;
    collected = 0; state = 0;
}

static void init_tables(void)
{
    int i;
    for (i = 0; i < LUT; i++) {
        float a = (float)i * (6.2831853f / LUT);
        sinT[i] = sinf(a); cosT[i] = cosf(a);
    }
    for (i = 0; i < ncols; i++) camX[i] = 2.0f * i / ncols - 1.0f;
    for (i = 0; i < 9; i++) {
        float a = (float)i * (6.2831853f / 8.0f);
        octX[i] = cosf(a); octY[i] = sinf(a);
    }
}

/* Projects a world object to screen. Returns 1 if in front of the camera. */
static int project(const Shape *s, int *sx, float *depth)
{
    float dx = s->x - px, dy = s->y - py;
    float dirx = cosT[ang], diry = sinT[ang];
    float plx = -diry * PLANE, ply = dirx * PLANE;
    float a = dx * dirx + dy * diry;                       /* depth */
    float b = (dx * plx + dy * ply) / (PLANE * PLANE);     /* lateral */
    if (a < 0.2f) return 0;
    *depth = a;
    *sx = (int)((W / 2) * (1.0f + b / a));
    return 1;
}

static int visible(const Shape *s)
{
    int sx, col; float d;
    if (!project(s, &sx, &d)) return 0;
    col = sx * ncols / W;
    if (col < 0 || col >= ncols) return 0;
    return zbuf[col] >= d;
}

static void draw_oct(int cx, int cy, int rx, int ry, u64 c)
{
    int k;
    for (k = 0; k < 8; k++)
        gsKit_prim_triangle(gs, cx, cy,
                            cx + octX[k] * rx, cy + octY[k] * ry,
                            cx + octX[k + 1] * rx, cy + octY[k + 1] * ry, 0, c);
}

static void draw_shape(const Shape *s)
{
    int sx, cx, cy, hs;
    float d;
    u64 c;
    if (!s->alive || !project(s, &sx, &d)) return;
    {
        int col = sx * ncols / W;
        if (col < 0) col = 0;
        if (col >= ncols) col = ncols - 1;
        if (zbuf[col] < d) return;
    }

    hs = (int)(H / d * 0.18f);
    if (hs < 2) hs = 2;
    cx = sx;
    cy = H / 2 + (int)(H / d * 0.5f) - hs;

    switch (s->type) {
    case 0: c = rgb(200, 60, 60);  gsKit_prim_sprite(gs, cx - hs / 2, cy - hs, cx + hs / 2, cy + hs, 0, c); break; /* rectangle */
    case 1: c = rgb(230, 200, 60); gsKit_prim_sprite(gs, cx - hs, cy - hs, cx + hs, cy + hs, 0, c); break;         /* square */
    case 2: c = rgb(70, 130, 230); draw_oct(cx, cy, hs, hs, c); break;                                              /* circle */
    case 3: c = rgb(190, 90, 220); draw_oct(cx, cy, hs, hs * 6 / 10, c); break;                                     /* oval */
    case 4: c = rgb(80, 200, 100);
            gsKit_prim_triangle(gs, cx, cy - hs, cx + hs, cy + hs, cx - hs, cy + hs, 0, c); break;                  /* triangle */
    default: /* diamond */
        c = rgb(220, 20, 20);
        gsKit_prim_triangle(gs, cx, cy - hs * 3 / 2, cx + hs, cy, cx - hs, cy, 0, c);
        gsKit_prim_triangle(gs, cx - hs, cy, cx + hs, cy, cx, cy + hs * 3 / 2, 0, c);
        break;
    }
    /* eyes */
    {
        int e = hs / 6 + 1;
        u64 w = (s->type == 5) ? rgb(255, 255, 255) : rgb(20, 20, 20);
        int ey = cy - hs / 4;
        gsKit_prim_sprite(gs, cx - hs / 3 - e, ey - e, cx - hs / 3 + e, ey + e, 0, w);
        gsKit_prim_sprite(gs, cx + hs / 3 - e, ey - e, cx + hs / 3 + e, ey + e, 0, w);
    }
}

static void render(void)
{
    int i, cw = W / ncols;
    float dirx = cosT[ang], diry = sinT[ang];
    float plx = -diry * PLANE, ply = dirx * PLANE;

    gsKit_clear(gs, rgb(0, 0, 0));
    gsKit_prim_sprite(gs, 0, 0, W, H / 2, 0, rgb(8, 8, 16));
    gsKit_prim_sprite(gs, 0, H / 2, W, H, 0, rgb(22, 16, 16));

    for (i = 0; i < ncols; i++) {
        float rdx = dirx + plx * camX[i], rdy = diry + ply * camX[i];
        int mx = (int)px, my = (int)py, stx, sty, side = 0, hit = 0, guard = 0;
        float ddx = (rdx == 0.0f) ? 1e30f : fabsf(1.0f / rdx);
        float ddy = (rdy == 0.0f) ? 1e30f : fabsf(1.0f / rdy);
        float sdx, sdy, d;
        int lh, y0, y1, shade;

        if (rdx < 0) { stx = -1; sdx = (px - mx) * ddx; } else { stx = 1; sdx = (mx + 1.0f - px) * ddx; }
        if (rdy < 0) { sty = -1; sdy = (py - my) * ddy; } else { sty = 1; sdy = (my + 1.0f - py) * ddy; }

        while (!hit && guard++ < 32) {
            if (sdx < sdy) { sdx += ddx; mx += stx; side = 0; }
            else           { sdy += ddy; my += sty; side = 1; }
            if (solid(mx, my)) hit = 1;
        }
        d = side ? (sdy - ddy) : (sdx - ddx);
        if (d < 0.05f) d = 0.05f;
        zbuf[i] = d;

        lh = (int)(H / d);
        y0 = H / 2 - lh / 2; if (y0 < 0) y0 = 0;
        y1 = H / 2 + lh / 2; if (y1 > H) y1 = H;
        shade = (int)(200.0f / (1.0f + d * d * 0.15f));
        if (side) shade = shade * 3 / 4;
        gsKit_prim_sprite(gs, i * cw, y0, (i + 1) * cw, y1, 0, rgb(shade, shade * 8 / 10, shade * 6 / 10));
    }

    /* far-to-near for the two kinds of sprite is good enough here */
    for (i = 0; i < NSHAPES; i++) draw_shape(&shapes[i]);
    draw_shape(&diamond);

    for (i = 0; i < NSHAPES; i++)
        gsKit_prim_sprite(gs, 10 + i * 20, 10, 26 + i * 20, 26, 0,
                          i < collected ? rgb(240, 220, 60) : rgb(50, 50, 50));
}

static void update(u32 btn, int lx, int ly)
{
    int i, turn = 0;
    float fwd = 0.0f, mx, my, nx, ny;

    if (abs(lx - 128) > 24) turn = (lx - 128) / 32;
    if (abs(ly - 128) > 24) fwd = -(ly - 128) / 128.0f;
    if (btn & PAD_LEFT)  turn = -3;
    if (btn & PAD_RIGHT) turn = 3;
    if (btn & PAD_UP)    fwd = 1.0f;
    if (btn & PAD_DOWN)  fwd = -1.0f;

    ang = (ang + turn + LUT) % LUT;
    mx = cosT[ang] * fwd * 0.05f;
    my = sinT[ang] * fwd * 0.05f;
    nx = px + mx; ny = py + my;
    if (!solid((int)(nx + (mx > 0 ? 0.2f : -0.2f)), (int)py)) px = nx;
    if (!solid((int)px, (int)(ny + (my > 0 ? 0.2f : -0.2f)))) py = ny;

    for (i = 0; i < NSHAPES; i++) {
        float dx, dy;
        if (!shapes[i].alive) continue;
        dx = shapes[i].x - px; dy = shapes[i].y - py;
        if (dx * dx + dy * dy < 0.5f) { shapes[i].alive = 0; collected++; }
    }
    if (collected >= NSHAPES) { state = 1; return; }

    /* Diamond: frozen while seen, creeps toward you otherwise */
    {
        float dx = px - diamond.x, dy = py - diamond.y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist < 0.7f) { state = 2; return; }
        if (!visible(&diamond) && dist > 0.001f) {
            float sx = dx / dist * 0.028f, sy = dy / dist * 0.028f;
            if (!solid((int)(diamond.x + sx), (int)diamond.y)) diamond.x += sx;
            if (!solid((int)diamond.x, (int)(diamond.y + sy))) diamond.y += sy;
        }
    }
}

static void init_pad(void)
{
    int s;
    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    padPortOpen(0, 0, padBuf);
    do { s = padGetState(0, 0); } while (s != PAD_STATE_STABLE && s != PAD_STATE_FINDCTP1);
    padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
}

int main(void)
{
    u32 prev = 0;

    init_pad();

    gs = gsKit_init_global();
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gs->ZBuffering = GS_SETTING_OFF;
    gs->DoubleBuffering = GS_SETTING_ON;
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);

    W = gs->Width; H = gs->Height;
    ncols = W / 2; if (ncols > MAX_COLS) ncols = MAX_COLS;
    init_tables();
    reset_game();

    for (;;) {
        struct padButtonStatus pb;
        u32 btn = 0;
        int lx = 128, ly = 128;

        if (padGetState(0, 0) == PAD_STATE_STABLE && padRead(0, 0, &pb)) {
            btn = 0xffff ^ pb.btns; lx = pb.ljoy_h; ly = pb.ljoy_v;
        }

        if (state == 0) {
            update(btn, lx, ly);
            render();
        } else {
            gsKit_clear(gs, state == 1 ? rgb(20, 90, 30) : rgb(110, 0, 0));
            if (state == 1) {
                int i;
                for (i = 0; i < NSHAPES; i++)
                    gsKit_prim_sprite(gs, W / 2 - 100 + i * 40, H / 2 - 15, W / 2 - 70 + i * 40, H / 2 + 15, 0, rgb(240, 220, 60));
            } else {
                gsKit_prim_triangle(gs, W / 2, H / 2 - 90, W / 2 + 60, H / 2, W / 2 - 60, H / 2, 0, rgb(220, 20, 20));
                gsKit_prim_triangle(gs, W / 2 - 60, H / 2, W / 2 + 60, H / 2, W / 2, H / 2 + 90, 0, rgb(220, 20, 20));
            }
            if ((btn & PAD_START) && !(prev & PAD_START)) reset_game();
        }
        prev = btn;

        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }
    return 0;
}
