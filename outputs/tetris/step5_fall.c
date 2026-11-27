/* =====================================================================
 *  俄罗斯方块 · 第 5 步:自动下坠(重力)
 * ---------------------------------------------------------------------
 *  第 4 步的主循环是这样的:
 *      if (!_kbhit()) { Sleep(10); continue; }
 *  意思是"没有按键就跳过这一圈"。
 *  于是方块只可能被按键推动 —— 你不按,它就永远悬在空中。
 *
 *  真正的下落需要一个"时间触发器":
 *  不管有没有人按键,只要距离上次下落已经过了 FALL_MS 毫秒,
 *  就自动往下挪一格。
 *
 *  所以主循环里现在有两件互不干扰的事:
 *      ① 有按键  -> 处理按键(左右移动 / 手动加速)
 *      ② 时间到  -> 自动下落一格
 *  这才是游戏循环该有的结构。
 * ===================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <windows.h>

#define WIDTH   10      /* 场地宽度(单位:格) */
#define HEIGHT  20      /* 场地高度(单位:格) */

#define ROWS 2
#define COLS 2

#define FALL_MS 500     /* 每 500 毫秒自动下落一格;数字越小掉得越快 */

int shape[ROWS][COLS] = {
    { 1, 1 },
    { 1, 1 }
};

static void gotoxy(int x, int y)
{
    COORD pos;
    pos.X = (SHORT)x;
    pos.Y = (SHORT)y;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
}

static void hide_cursor(void)
{
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 1;
    info.bVisible = FALSE;
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
}

static void draw_wall(void)
{
    int r, c;

    gotoxy(0, 0);

    printf("+");
    for (c = 0; c < WIDTH; c++) printf("--");
    printf("+\n");

    for (r = 0; r < HEIGHT; r++) {
        printf("|");
        for (c = 0; c < WIDTH; c++) printf("  ");
        printf("|\n");
    }

    printf("+");
    for (c = 0; c < WIDTH; c++) printf("--");
    printf("+\n");
}

/* 在场地里画 / 擦方块,gx、gy 是方块左上角在场上的格坐标 */
static void paint(int gx, int gy, int erased)
{
    int r, c;

    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            if (shape[r][c] == 1) {
                gotoxy(1 + (gx + c) * 2, 1 + (gy + r));
                printf(erased ? "  " : "[]");
            }
        }
    }
}

/* 把方块放到 (gx, gy) 合不合法? 合法返回 1,越界返回 0 */
static int can_place(int gx, int gy)
{
    int r, c;

    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            if (shape[r][c] == 1) {
                int x = gx + c;
                int y = gy + r;

                if (x < 0 || x >= WIDTH)  return 0;
                if (y < 0 || y >= HEIGHT) return 0;
            }
        }
    }
    return 1;
}

int main(void)
{
    int x = WIDTH / 2 - COLS / 2;
    int y = 0;
    int key, dir, nx, ny, moved;
    DWORD last_fall;

    system("chcp 65001 > nul");
    system("cls");
    hide_cursor();

    draw_wall();
    paint(x, y, 0);

    gotoxy(0, HEIGHT + 2);
    printf("← → 移动    ↓ 加速下落    q 退出");

    last_fall = GetTickCount();     /* 记下现在这一刻,作为"上次下落时间" */

    for (;;) {

        /* ---------- ① 有按键,先处理按键 ---------- */
        if (_kbhit()) {
            key = _getch();

            if (key == 'q' || key == 27) break;

            if (key == 0 || key == 224) {
                dir = _getch();

                nx = x;
                ny = y;
                moved = 0;

                if (dir == 72)      { ny--; moved = 1; }   /* ↑ */
                else if (dir == 80) { ny++; moved = 1; }   /* ↓ */
                else if (dir == 75) { nx--; moved = 1; }   /* ← */
                else if (dir == 77) { nx++; moved = 1; }   /* → */

                if (moved && can_place(nx, ny)) {
                    paint(x, y, 1);                     /* 擦旧 */
                    paint(nx, ny, 0);                   /* 画新 */
                    x = nx;
                    y = ny;

                    if (dir == 80) {
                        last_fall = GetTickCount();     /* 手动掉过了,重新计时 */
                    }
                }
            }
        }

        /* ---------- ② 检查"是不是到点该自动掉一格了" ---------- */
        if (GetTickCount() - last_fall >= FALL_MS) {
            last_fall = GetTickCount();

            if (can_place(x, y + 1)) {
                paint(x, y, 1);
                paint(x, y + 1, 0);
                y++;
            }
            /* 掉不下去了 = 已经触底,先停在原地 */
        }

        Sleep(10);          /* 歇一下,别让 CPU 空转 */
    }

    gotoxy(0, HEIGHT + 4);
    printf("已退出。\n");

    return 0;
}
