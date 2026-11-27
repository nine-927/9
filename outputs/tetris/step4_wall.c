/* =====================================================================
 *  俄罗斯方块 · 第 4 步:画出场地 + "撞墙检测"
 * ---------------------------------------------------------------------
 *  关键点:
 *   1) 场地是一块 10 列 x 20 行的矩形,用字符画个框子出来。
 *   2) 每次要移动之前,先"假想"一下:如果真移过去,会不会越界?
 *      把方块的每一格都摆到新位置上试一遍,只要有一格跑出场外
 *      (列 < 0 或 >= 宽,行 < 0 或 >= 高),这一动就不许动。
 *   3) 不许动 => 什么也不做 => 方块自然就"停"在墙边了。
 *      这个 can_place() 就是以后判断"落到底了 / 撞到其他方块了"的基础,
 *      后面要做堆叠、消行,都是从它扩展出去的。
 * ===================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <windows.h>

#define WIDTH   10      /* 场地宽度(单位:格) */
#define HEIGHT  20      /* 场地高度(单位:格) */

#define ROWS 2
#define COLS 2

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

/* 画一次场地边框。
 * 左边框占终端第 0 列,上下边框各占 1 行,所以:
 * 场地第 c 格 => 终端第 1+2c 列; 场地第 r 行 => 终端第 1+r 行。 */
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
                int x = gx + c;         /* 这一格在场地的第几列 */
                int y = gy + r;         /* 这一格在场地的第几行 */

                if (x < 0 || x >= WIDTH)  return 0;   /* 撞左右墙 */
                if (y < 0 || y >= HEIGHT) return 0;   /* 撞上边 / 下边 */
            }
        }
    }
    return 1;
}

int main(void)
{
    int x = WIDTH / 2 - COLS / 2;   /* 从场地中间开始 */
    int y = 0;
    int key, dir, nx, ny;

    system("chcp 65001 > nul");
    system("cls");
    hide_cursor();

    draw_wall();
    paint(x, y, 0);

    gotoxy(0, HEIGHT + 2);
    printf("方向键移动    q 退出");

    for (;;) {
        if (!_kbhit()) {
            Sleep(10);
            continue;
        }

        key = _getch();
        if (key == 'q' || key == 27) break;
        if (key != 0 && key != 224) continue;

        dir = _getch();

        nx = x;
        ny = y;

        if (dir == 72)      ny--;       /* 上 */
        else if (dir == 80) ny++;       /* 下 */
        else if (dir == 75) nx--;       /* 左 */
        else if (dir == 77) nx++;       /* 右 */
        else continue;

        if (can_place(nx, ny)) {        /* 撞墙检测:能走才走 */
            paint(x, y, 1);             /* 擦掉旧位置 */
            paint(nx, ny, 0);           /* 画到新位置 */
            x = nx;
            y = ny;
        }
        /* 走不了就什么也不做 —— 方块就停在墙边了 */
    }

    gotoxy(0, HEIGHT + 4);
    printf("已退出。\n");

    return 0;
}
