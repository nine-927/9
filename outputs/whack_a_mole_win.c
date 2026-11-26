/*
 * whack_a_mole_win.c —— Windows 原生 C 语言桌面版"打地鼠"
 *
 * 纯 Win32 API + GDI 实现，不依赖任何第三方库，编译后是独立的 .exe。
 * 玩法与终端版 / Python 版完全一致：
 *   · 3x3 格子，地鼠随机冒头，按 1~9 或鼠标点击击打
 *   · 击中 +10 分且地鼠提速；打错 -5 分并 -1 生命；超时逃跑 -1 生命
 *   · 60 秒或生命归零结束，显示得分与命中率，R 或点击重开
 *
 * 编译（VS 开发者命令行）：
 *   cl /nologo /W3 /O2 /utf-8 whack_a_mole_win.c /link user32.lib gdi32.lib
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS     /* 允许使用 wcsncpy/_snwprintf 等安全版本外的老函数 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

/* ==================== 可调整的难度参数 ==================== */
#define GAME_DURATION_SEC 60      /* 每局总时长(秒) */
#define START_LIVES       3       /* 初始生命值 */
#define HIT_SCORE         10      /* 击中得分 */
#define MISS_PENALTY      5       /* 打错扣分 */
#define MOLE_START_MS     1200    /* 地鼠初始停留时间(毫秒) */
#define MOLE_MIN_MS       450     /* 地鼠最短停留时间 */
#define MOLE_SPEEDUP_MS   30      /* 每击中一次缩短的停留时间 */
#define SPAWN_GAP_MS      300     /* 两只地鼠之间的间隔 */

/* ==================== 界面尺寸与配色 ==================== */
#define WND_W 560
#define WND_H 760

#define GRID_X0 54
#define GRID_Y0 170
#define CELL    140
#define GAP     16

#define C_BG_TOP     RGB(251, 243, 228)
#define C_BG_BOTTOM  RGB(238, 223, 198)
#define C_CARD       RGB(255, 251, 243)
#define C_BORDER     RGB(185, 138, 90)
#define C_TEXT       RGB(74, 53, 32)
#define C_SUBTLE     RGB(139, 115, 85)
#define C_HOLE_IN    RGB(46, 33, 23)
#define C_HOLE_MID   RGB(74, 51, 35)
#define C_HOLE_OUT   RGB(107, 74, 46)
#define C_RIM        RGB(139, 94, 52)
#define C_MOLE       RGB(185, 130, 76)
#define C_MOLE_DK    RGB(148, 98, 58)
#define C_MOLE_LT    RGB(201, 149, 96)
#define C_SNOUT      RGB(226, 195, 156)
#define C_NOSE       RGB(224, 138, 138)
#define C_HEART      RGB(216, 67, 67)
#define C_HEART_OFF  RGB(214, 198, 178)
#define C_GREEN      RGB(46, 158, 82)
#define C_RED        RGB(192, 57, 43)
#define C_BAR_BG     RGB(226, 210, 186)
#define C_SHADOW     RGB(201, 183, 156)

/* ==================== 游戏状态 ==================== */
typedef struct {
    int   active;
    int   cx, cy;
    ULONGLONG t0;
    int   value;
} FloatText;

typedef struct {
    int   score;
    int   lives;
    int   hits;
    int   swings;
    int   mole_idx;        /* 0~8，-1 表示无地鼠 */
    int   last_mole;
    double mole_stay;      /* 当前地鼠停留秒数 */
    ULONGLONG mole_born;   /* 弹出时刻(ms) */
    ULONGLONG mole_deadline;
    ULONGLONG next_spawn;
    ULONGLONG start;
    double time_left;
    int   phase;           /* 0 = 游戏中, 1 = 已结束 */
    wchar_t msg[160];
    COLORREF msg_color;
    FloatText floats[12];
    ULONGLONG flash[9];    /* 0 表示无闪烁 */
} Game;

static Game g;

/* GDI 资源 */
static HFONT g_fTitle, g_fSub, g_fHud, g_fSmall, g_fBig, g_fHeart, g_fMsg, g_fNum;
static HDC   g_memDC;
static HBITMAP g_memBmp;
static void *g_bits;

/* ==================== 基础绘图工具 ==================== */
static void fill_rect(HDC dc, int l, int t, int r, int b, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    RECT rc;
    rc.left = l; rc.top = t; rc.right = r; rc.bottom = b;
    FillRect(dc, &rc, br);
    DeleteObject(br);
}

static void fill_ellipse(HDC dc, int l, int t, int r, int b, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, l, t, r, b);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
}

static void ring_ellipse(HDC dc, int l, int t, int r, int b, COLORREF c, int width)
{
    HPEN pen = CreatePen(PS_SOLID, width, c);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, l, t, r, b);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);
}

static void fill_round_rect(HDC dc, int l, int t, int r, int b, int radius,
                            COLORREF fill, COLORREF border, int bw)
{
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, bw, border);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, l, t, r, b, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static void draw_line(HDC dc, int x1, int y1, int x2, int y2, COLORREF c, int width)
{
    HPEN pen = CreatePen(PS_SOLID, width, c);
    HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, NULL);
    LineTo(dc, x2, y2);
    SelectObject(dc, op);
    DeleteObject(pen);
}

/* 在矩形内排版文字（颜色 + 字体 + 对齐方式） */
static void draw_text(HDC dc, HFONT font, COLORREF color, RECT rc,
                      const wchar_t *s, UINT flags)
{
    HGDIOBJ of = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, &rc, flags);
    SelectObject(dc, of);
}

/* 颜色线性插值，用于渐变背景和飘字淡出 */
static COLORREF lerp_color(COLORREF a, COLORREF b, double t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int r = (int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t);
    int gg = (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t);
    int bl = (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t);
    return RGB(r, gg, bl);
}

static void make_fonts(void)
{
    g_fTitle = CreateFontW(-32, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Microsoft YaHei");
    g_fSub = CreateFontW(-12, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, L"Segoe UI");
    g_fHud = CreateFontW(-18, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, L"Microsoft YaHei");
    g_fSmall = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Microsoft YaHei");
    g_fBig = CreateFontW(-44, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, L"Microsoft YaHei");
    g_fHeart = CreateFontW(-20, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI Symbol");
    g_fMsg = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, L"Microsoft YaHei");
    g_fNum = CreateFontW(-13, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, L"Microsoft YaHei");
}

static void free_fonts(void)
{
    HFONT fonts[] = { g_fTitle, g_fSub, g_fHud, g_fSmall, g_fBig, g_fHeart, g_fMsg, g_fNum };
    for (int i = 0; i < (int)(sizeof(fonts) / sizeof(fonts[0])); i++)
        if (fonts[i]) DeleteObject(fonts[i]);
}

/* ==================== 游戏逻辑 ==================== */
static int cell_x(int idx) { return GRID_X0 + (idx % 3) * (CELL + GAP); }
static int cell_y(int idx) { return GRID_Y0 + (idx / 3) * (CELL + GAP); }

static void set_msg(const wchar_t *text, COLORREF color)
{
    wcsncpy(g.msg, text, 159);
    g.msg[159] = L'\0';
    g.msg_color = color;
}

static void game_reset(void)
{
    ULONGLONG now = GetTickCount64();
    g.score = 0;
    g.lives = START_LIVES;
    g.hits = 0;
    g.swings = 0;
    g.mole_idx = -1;
    g.last_mole = -1;
    g.mole_stay = MOLE_START_MS / 1000.0;
    g.mole_born = now;
    g.mole_deadline = now;
    g.next_spawn = now + 800;
    g.start = now;
    g.time_left = (double)GAME_DURATION_SEC;
    g.phase = 0;
    for (int i = 0; i < 12; i++) g.floats[i].active = 0;
    for (int i = 0; i < 9; i++) g.flash[i] = 0;
    set_msg(L"准备好了吗？看准地鼠，快速按下对应数字键！", C_TEXT);
}

static void spawn_mole(ULONGLONG now)
{
    int pos = rand() % 9;
    /* 与上一只重复时，50% 概率强制换位，避免原地连刷 */
    if (pos == g.last_mole && (rand() & 1)) {
        while (pos == g.last_mole)
            pos = rand() % 9;
    }
    g.mole_idx = pos;
    g.last_mole = pos;
    g.mole_born = now;
    g.mole_deadline = now + (ULONGLONG)(g.mole_stay * 1000.0);
}

static void add_float(int idx, int value)
{
    for (int i = 0; i < 12; i++) {
        if (!g.floats[i].active) {
            g.floats[i].active = 1;
            g.floats[i].cx = cell_x(idx) + CELL / 2;
            g.floats[i].cy = cell_y(idx) + CELL / 2;
            g.floats[i].t0 = GetTickCount64();
            g.floats[i].value = value;
            return;
        }
    }
}

static void game_over(void)
{
    g.phase = 1;
    g.mole_idx = -1;
    set_msg(L"", C_TEXT);
}

static void whack(int idx)
{
    ULONGLONG now;
    if (g.phase == 1) {
        game_reset();
        return;
    }
    now = GetTickCount64();
    g.swings++;
    if (idx == g.mole_idx) {
        g.hits++;
        g.score += HIT_SCORE;
        g.mole_idx = -1;
        g.next_spawn = now + SPAWN_GAP_MS;
        if (g.mole_stay > (MOLE_MIN_MS + MOLE_SPEEDUP_MS) / 1000.0)
            g.mole_stay -= MOLE_SPEEDUP_MS / 1000.0;
        add_float(idx, HIT_SCORE);
        {
            wchar_t buf[160];
            _snwprintf(buf, 159, L"漂亮！击中 %d 号格子，+%d 分（地鼠变快了）",
                       idx + 1, HIT_SCORE);
            buf[159] = L'\0';
            set_msg(buf, C_GREEN);
        }
    } else {
        g.lives--;
        g.score -= MISS_PENALTY;
        if (g.score < 0) g.score = 0;
        g.flash[idx] = now;
        add_float(idx, -MISS_PENALTY);
        {
            wchar_t buf[160];
            _snwprintf(buf, 159, L"打错格子 %d！-%d 分，生命 -1", idx + 1, MISS_PENALTY);
            buf[159] = L'\0';
            set_msg(buf, C_RED);
        }
        if (g.lives <= 0) game_over();
    }
}

static void game_tick(ULONGLONG now)
{
    if (g.phase != 0) return;

    g.time_left = GAME_DURATION_SEC - (double)(now - g.start) / 1000.0;
    if (g.time_left <= 0) {
        g.time_left = 0;
        set_msg(L"时间到！本局结束", C_TEXT);
        game_over();
        return;
    }
    if (g.mole_idx >= 0 && now >= g.mole_deadline) {
        g.mole_idx = -1;
        g.lives--;
        g.next_spawn = now + SPAWN_GAP_MS;
        set_msg(L"太慢了！地鼠跑了，生命 -1", C_RED);
        if (g.lives <= 0) {
            game_over();
            return;
        }
    }
    if (g.mole_idx < 0 && now >= g.next_spawn)
        spawn_mole(now);
}

/* ==================== 绘制 ==================== */
static void draw_mole(HDC dc, int cx, int cy, double s)
{
    int r = (int)(46 * s);
    int er, ey;
    if (r < 1) return;

    /* 耳朵 */
    er = (int)(r * 0.33);
    fill_ellipse(dc, cx - (int)(0.62 * r) - er, cy - (int)(0.62 * r) - er,
                 cx - (int)(0.62 * r) + er, cy - (int)(0.62 * r) + er, C_MOLE_DK);
    fill_ellipse(dc, cx + (int)(0.62 * r) - er, cy - (int)(0.62 * r) - er,
                 cx + (int)(0.62 * r) + er, cy - (int)(0.62 * r) + er, C_MOLE_DK);

    /* 身体 */
    fill_ellipse(dc, cx - r, cy - (int)(r * 0.95), cx + r, cy + (int)(r * 0.95), C_MOLE);
    /* 头顶高光 */
    fill_ellipse(dc, cx - (int)(r * 0.55), cy - (int)(r * 0.72),
                 cx + (int)(r * 0.55), cy - (int)(r * 0.10), C_MOLE_LT);
    /* 口鼻区 */
    fill_ellipse(dc, cx - (int)(r * 0.50), cy + (int)(r * 0.10),
                 cx + (int)(r * 0.50), cy + (int)(r * 0.62), C_SNOUT);

    /* 眼睛 */
    ey = (int)(5.5 * s);
    if (ey < 1) ey = 1;
    fill_ellipse(dc, cx - (int)(0.32 * r) - ey, cy - (int)(0.22 * r) - (int)(ey * 1.15),
                 cx - (int)(0.32 * r) + ey, cy - (int)(0.22 * r) + (int)(ey * 1.15),
                 RGB(36, 23, 16));
    fill_ellipse(dc, cx + (int)(0.32 * r) - ey, cy - (int)(0.22 * r) - (int)(ey * 1.15),
                 cx + (int)(0.32 * r) + ey, cy - (int)(0.22 * r) + (int)(ey * 1.15),
                 RGB(36, 23, 16));

    /* 鼻子 */
    fill_ellipse(dc, cx - (int)(6 * s), cy + (int)(r * 0.16),
                 cx + (int)(6 * s), cy + (int)(r * 0.28), C_NOSE);

    /* 胡须 */
    for (int d = -1; d <= 1; d++) {
        int ys = cy + (int)(r * 0.2) + d * (int)(5 * s);
        int ye = ys - d * (int)(4 * s) - (int)(6 * s);
        draw_line(dc, cx + (int)(r * 0.34), ys, cx + (int)(r * 0.90), ye,
                  RGB(107, 74, 46), 1);
        draw_line(dc, cx - (int)(r * 0.34), ys, cx - (int)(r * 0.90), ye,
                  RGB(107, 74, 46), 1);
    }
}

static void draw_header(HDC dc)
{
    RECT rc;
    wchar_t buf[64];

    /* 标题与副标题 */
    SetRect(&rc, 0, 22, WND_W, 58);
    draw_text(dc, g_fTitle, C_TEXT, rc, L"打 地 鼠", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SetRect(&rc, 0, 58, WND_W, 82);
    draw_text(dc, g_fSub, RGB(180, 154, 120), rc, L"W H A C K - A - M O L E",
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* 分数 */
    SetRect(&rc, GRID_X0, 86, GRID_X0 + 200, 112);
    _snwprintf(buf, 63, L"分数  %d", g.score);
    buf[63] = L'\0';
    draw_text(dc, g_fHud, C_TEXT, rc, buf, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* 生命(心形) */
    for (int i = 0; i < START_LIVES; i++) {
        SetRect(&rc, GRID_X0 + 250 + i * 26, 84, GRID_X0 + 276 + i * 26, 114);
        draw_text(dc, g_fHeart, i < g.lives ? C_HEART : C_HEART_OFF, rc, L"♥",
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* 剩余时间 */
    _snwprintf(buf, 63, L"%d 秒", (int)(g.time_left + 0.5));
    buf[63] = L'\0';
    SetRect(&rc, WND_W - GRID_X0 - 120, 86, WND_W - GRID_X0, 112);
    draw_text(dc, g_fHud, C_TEXT, rc, buf, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    /* 时间进度条 */
    fill_round_rect(dc, GRID_X0, 126, WND_W - GRID_X0, 136, 10, C_BAR_BG, C_BAR_BG, 0);
    {
        double ratio = g.time_left / (double)GAME_DURATION_SEC;
        int x1;
        if (ratio < 0) ratio = 0;
        if (ratio > 1) ratio = 1;
        x1 = GRID_X0 + (int)((WND_W - 2 * GRID_X0) * ratio);
        if (x1 > GRID_X0 + 2)
            fill_round_rect(dc, GRID_X0, 126, x1, 136, 10,
                            ratio > 0.25 ? C_GREEN : RGB(217, 130, 43), C_BAR_BG, 0);
    }

    /* 卡片底板 */
    fill_round_rect(dc, GRID_X0 - 18, GRID_Y0 - 18,
                    WND_W - GRID_X0 + 18, GRID_Y0 + 3 * CELL + 2 * GAP + 18,
                    18, C_CARD, C_BORDER, 3);
}

static void draw_board(HDC dc, ULONGLONG now)
{
    for (int i = 0; i < 9; i++) {
        int x = cell_x(i), y = cell_y(i);
        int cx = x + CELL / 2, cy = y + CELL / 2;
        int r = CELL / 2 - 8;
        wchar_t buf[8];
        RECT rc;

        /* 洞口：四层椭圆模拟立体感 */
        fill_ellipse(dc, cx - r, cy - r, cx + r, cy + r, C_RIM);
        fill_ellipse(dc, cx - r + 5, cy - r + 5, cx + r - 5, cy + r - 5, C_HOLE_OUT);
        fill_ellipse(dc, cx - r + 12, cy - r + 12, cx + r - 12, cy + r - 12, C_HOLE_MID);
        fill_ellipse(dc, cx - r + 22, cy - r + 22, cx + r - 22, cy + r - 22, C_HOLE_IN);

        /* 格子编号 */
        _snwprintf(buf, 7, L"%d", i + 1);
        buf[7] = L'\0';
        SetRect(&rc, x, y + 4, x + CELL, y + 28);
        draw_text(dc, g_fNum, RGB(201, 183, 156), rc, buf,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        /* 地鼠（带弹出缩放动画） */
        if (g.mole_idx == i) {
            double s = (double)(now - g.mole_born) / 120.0;
            if (s > 1.0) s = 1.0;
            draw_mole(dc, cx, cy + 20, s);
        }

        /* 打错时的红圈 */
        if (g.flash[i] != 0) {
            ULONGLONG age = now - g.flash[i];
            if (age < 350) {
                double t = (double)age / 350.0;
                COLORREF c = lerp_color(C_RED, C_CARD, t);
                ring_ellipse(dc, x + 6, y + 6, x + CELL - 6, y + CELL - 6, c, 4);
            } else {
                g.flash[i] = 0;
            }
        }
    }
}

static void draw_floats(HDC dc, ULONGLONG now)
{
    for (int i = 0; i < 12; i++) {
        if (!g.floats[i].active) continue;
        {
            double t = (double)(now - g.floats[i].t0) / 750.0;
            wchar_t buf[16];
            RECT rc;
            COLORREF base = g.floats[i].value > 0 ? C_GREEN : C_RED;
            COLORREF c;
            int y;
            if (t >= 1.0) {
                g.floats[i].active = 0;
                continue;
            }
            c = lerp_color(base, C_CARD, t);
            y = g.floats[i].cy - 10 - (int)(46 * t);
            _snwprintf(buf, 15, L"%+d", g.floats[i].value);
            buf[15] = L'\0';
            SetRect(&rc, g.floats[i].cx - 50, y - 18, g.floats[i].cx + 50, y + 18);
            draw_text(dc, g_fMsg, c, rc, buf, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

static void draw_footer(HDC dc)
{
    RECT rc;
    SetRect(&rc, GRID_X0, GRID_Y0 + 3 * CELL + 2 * GAP + 24, WND_W - GRID_X0,
            GRID_Y0 + 3 * CELL + 2 * GAP + 78);
    draw_text(dc, g_fMsg, g.msg_color, rc, g.msg, DT_CENTER | DT_WORDBREAK);

    SetRect(&rc, 0, WND_H - 52, WND_W, WND_H - 26);
    draw_text(dc, g_fSmall, C_SUBTLE, rc,
              L"按 1~9 击打对应格子   ·   也可用鼠标点击地鼠   ·   R 重开   ·   Esc 退出",
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void draw_overlay(HDC dc)
{
    RECT rc;
    wchar_t buf[96];
    int x0 = GRID_X0, x1 = WND_W - GRID_X0;

    /* 阴影 + 面板 */
    fill_round_rect(dc, x0 + 8, 258, x1 + 8, 520, 18, C_SHADOW, C_SHADOW, 0);
    fill_round_rect(dc, x0, 250, x1, 512, 18, C_CARD, C_BORDER, 3);

    SetRect(&rc, x0, 268, x1, 312);
    draw_text(dc, g_fHud, C_TEXT, rc, L"游 戏 结 束", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    _snwprintf(buf, 95, L"%d 分", g.score);
    buf[95] = L'\0';
    SetRect(&rc, x0, 322, x1, 386);
    draw_text(dc, g_fBig, C_RED, rc, buf, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (g.swings > 0)
        _snwprintf(buf, 95, L"击中 %d 只 / 出手 %d 次      命中率 %.0f%%",
                   g.hits, g.swings, 100.0 * g.hits / g.swings);
    else
        _snwprintf(buf, 95, L"击中 %d 只 / 出手 %d 次      命中率 --", g.hits, g.swings);
    buf[95] = L'\0';
    SetRect(&rc, x0, 392, x1, 420);
    draw_text(dc, g_fSmall, C_SUBTLE, rc, buf, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SetRect(&rc, x0, 438, x1, 470);
    draw_text(dc, g_fMsg, C_GREEN, rc, L"按 R 或点击任意位置 → 再来一局",
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SetRect(&rc, x0, 474, x1, 498);
    draw_text(dc, g_fSmall, C_SUBTLE, rc, L"Esc 退出游戏",
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* 把整个界面画到指定 HDC（双缓冲与截图共用同一套绘制代码） */
static void render(HDC dc, ULONGLONG now)
{
    /* 竖向渐变背景 */
    for (int i = 0; i < 76; i++) {
        int y0 = WND_H * i / 76;
        int y1 = WND_H * (i + 1) / 76;
        fill_rect(dc, 0, y0, WND_W, y1, lerp_color(C_BG_TOP, C_BG_BOTTOM, i / 75.0));
    }
    draw_header(dc);
    draw_board(dc, now);
    draw_floats(dc, now);
    draw_footer(dc);
    if (g.phase == 1)
        draw_overlay(dc);
}

/* ==================== 截图模式（自检用） ==================== */
static int save_bmp(const char *path, const void *bits, int w, int h)
{
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    FILE *fp = fopen(path, "wb");
    DWORD size = (DWORD)(w * h * 4);
    if (!fp) return 0;
    memset(&fh, 0, sizeof(fh));
    memset(&ih, 0, sizeof(ih));
    fh.bfType = 0x4D42;                      /* 'BM' */
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + size;
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = -h;                        /* 负数 = 自上而下 */
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = size;
    fwrite(&fh, sizeof(fh), 1, fp);
    fwrite(&ih, sizeof(ih), 1, fp);
    fwrite(bits, 1, size, fp);
    fclose(fp);
    return 1;
}

static void shot_mode(void)
{
    BITMAPINFO bi;
    void *bits = NULL;
    HDC dc;
    HBITMAP bmp;
    HGDIOBJ old;
    ULONGLONG now;

    make_fonts();
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = WND_W;
    bi.bmiHeader.biHeight = -WND_H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dc = CreateCompatibleDC(NULL);
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    old = SelectObject(dc, bmp);

    /* 场景 1：游戏进行中 */
    game_reset();
    now = GetTickCount64();
    g.score = 40;
    g.lives = 2;
    g.time_left = 42;
    g.mole_idx = 4;
    g.mole_born = now - 1000;
    g.mole_deadline = now + 10000;
    set_msg(L"漂亮！击中 5 号格子，+10 分（地鼠变快了）", C_GREEN);
    add_float(4, 10);
    render(dc, now);
    save_bmp("preview_play.bmp", bits, WND_W, WND_H);

    /* 场景 2：游戏结束 */
    g.hits = 21;
    g.swings = 27;
    game_over();
    render(dc, now);
    save_bmp("preview_over.bmp", bits, WND_W, WND_H);

    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    free_fonts();
}

/* ==================== 窗口过程 ==================== */
static int cell_hit(int x, int y)
{
    for (int i = 0; i < 9; i++) {
        int cx = cell_x(i), cy = cell_y(i);
        if (x >= cx && x <= cx + CELL && y >= cy && y <= cy + CELL)
            return i;
    }
    return -1;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        BITMAPINFO bi;
        HDC dc = GetDC(hwnd);
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = WND_W;
        bi.bmiHeader.biHeight = -WND_H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        g_memDC = CreateCompatibleDC(dc);
        g_memBmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &g_bits, NULL, 0);
        SelectObject(g_memDC, g_memBmp);
        ReleaseDC(hwnd, dc);

        make_fonts();
        game_reset();
        SetTimer(hwnd, 1, 16, NULL);   /* ~60fps 游戏循环 */
        return 0;
    }
    case WM_TIMER:
        game_tick(GetTickCount64());
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        render(g_memDC, GetTickCount64());
        BitBlt(dc, 0, 0, WND_W, WND_H, g_memDC, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDOWN: {
        int idx = cell_hit((int)(short)LOWORD(lp), (int)(short)HIWORD(lp));
        if (g.phase == 1)
            game_reset();
        else if (idx >= 0)
            whack(idx);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp >= '1' && wp <= '9') {
            whack((int)wp - '1');
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (wp == 'R') {
            game_reset();
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (wp == VK_ESCAPE || wp == 'Q') {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (g_memBmp) DeleteObject(g_memBmp);
        if (g_memDC) DeleteDC(g_memDC);
        free_fonts();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* 创建主窗口(visible=0 时用于自动化自检) */
static HWND create_main_window(HINSTANCE inst, int visible)
{
    WNDCLASSEXW wc;
    HWND hwnd;
    RECT rc;
    const wchar_t *cls = L"WhackAMoleWindow";

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = NULL;
    wc.lpszClassName = cls;
    if (!RegisterClassExW(&wc))
        return NULL;

    /* 固定 560x760 客户区，禁止改变大小 */
    rc.left = 0; rc.top = 0; rc.right = WND_W; rc.bottom = WND_H;
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX), FALSE);

    hwnd = CreateWindowExW(0, cls, L"打地鼠 · Whack-a-Mole",
                           WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX),
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           rc.right - rc.left, rc.bottom - rc.top,
                           NULL, NULL, inst, NULL);
    if (!hwnd)
        return NULL;

    /* 窗口居中 */
    {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        SetWindowPos(hwnd, NULL, (sw - w) / 2, (sh - h) / 2 - 20, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
    }

    if (visible) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    return hwnd;
}

/*
 * 自检模式(--selftest)：创建隐藏窗口，把真实的窗口消息打进去，
 * 验证定时器、按键、鼠标点击、重开与绘制流程，结果写入 selftest.txt。
 */
static void selftest_mode(HINSTANCE inst)
{
    HWND hwnd = create_main_window(inst, 0);
    FILE *fp;
    int ok = 1;
    int s_hit, s_miss, lives_miss, s_click, phase_over, s_reset, lives_reset;
    ULONGLONG now;

    if (!hwnd) {
        fp = fopen("selftest.txt", "w");
        if (fp) { fputs("FAIL: CreateWindow failed\n", fp); fclose(fp); }
        return;
    }
    SendMessageW(hwnd, WM_TIMER, 1, 0);     /* 跑一次游戏循环 */

    /* 1) 键盘击中 */
    g.mole_idx = 3;
    g.mole_deadline = GetTickCount64() + 5000;
    SendMessageW(hwnd, WM_KEYDOWN, '4', 0);
    s_hit = g.score;

    /* 2) 键盘打错 */
    SendMessageW(hwnd, WM_KEYDOWN, '1', 0);
    s_miss = g.score;
    lives_miss = g.lives;

    /* 3) 鼠标点击击中(9 号格中心) */
    g.mole_idx = 8;
    g.mole_deadline = GetTickCount64() + 5000;
    SendMessageW(hwnd, WM_LBUTTONDOWN, 0,
                 MAKELPARAM(cell_x(8) + CELL / 2, cell_y(8) + CELL / 2));
    s_click = g.score;

    /* 4) 生命耗尽 => 结束 */
    g.lives = 1;
    g.mole_idx = -1;
    SendMessageW(hwnd, WM_KEYDOWN, '2', 0);
    phase_over = g.phase;

    /* 5) R 重开 */
    SendMessageW(hwnd, WM_KEYDOWN, 'R', 0);
    s_reset = g.score;
    lives_reset = g.lives;

    /* 6) 绘制流程(隐藏窗口也能走到 WM_PAINT 分支) */
    InvalidateRect(hwnd, NULL, FALSE);
    UpdateWindow(hwnd);

    /* 7) 超时扣命 */
    SendMessageW(hwnd, WM_KEYDOWN, 'R', 0);
    now = GetTickCount64();
    g.mole_idx = 5;
    g.mole_deadline = now - 1;
    SendMessageW(hwnd, WM_TIMER, 1, 0);

    if (s_hit != HIT_SCORE) ok = 0;
    if (s_miss != HIT_SCORE - MISS_PENALTY) ok = 0;
    if (lives_miss != START_LIVES - 1) ok = 0;
    if (s_click != s_miss + HIT_SCORE) ok = 0;   /* 5 + 10 = 15 */
    if (phase_over != 1) ok = 0;
    if (s_reset != 0 || lives_reset != START_LIVES) ok = 0;
    if (g.lives != START_LIVES - 1) ok = 0;

    fp = fopen("selftest.txt", "w");
    if (fp) {
        fprintf(fp, "%s\n", ok ? "PASS" : "FAIL");
        fprintf(fp, "hit_score=%d (expect %d)\n", s_hit, HIT_SCORE);
        fprintf(fp, "miss_score=%d (expect %d)\n", s_miss, HIT_SCORE - MISS_PENALTY);
        fprintf(fp, "lives_after_miss=%d (expect %d)\n", lives_miss, START_LIVES - 1);
        fprintf(fp, "click_score=%d (expect %d)\n", s_click, s_miss + HIT_SCORE);
        fprintf(fp, "phase_after_lives0=%d (expect 1)\n", phase_over);
        fprintf(fp, "reset_score=%d lives=%d (expect 0/%d)\n", s_reset, lives_reset, START_LIVES);
        fprintf(fp, "lives_after_timeout=%d (expect %d)\n", g.lives, START_LIVES - 1);
        fclose(fp);
    }

    DestroyWindow(hwnd);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    HWND hwnd;
    MSG msg;

    (void)prev;
    (void)cmdline;
    (void)show;

    /* 自检模式：whack_a_mole_win.exe --shot / --selftest */
    if (wcsstr(GetCommandLineW(), L"--shot")) {
        shot_mode();
        return 0;
    }
    if (wcsstr(GetCommandLineW(), L"--selftest")) {
        selftest_mode(inst);
        return 0;
    }

    SetProcessDPIAware();         /* 高 DPI 下保持 1:1 像素，画面不模糊 */
    srand((unsigned)time(NULL) ^ (unsigned)GetCurrentProcessId());

    hwnd = create_main_window(inst, 1);
    if (!hwnd)
        return 1;

    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

