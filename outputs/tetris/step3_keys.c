/* =====================================================================
 *  俄罗斯方块 · 第 3 步:用键盘的 ↑ ↓ ← → 控制方块
 * ---------------------------------------------------------------------
 *  关键点:
 *   1) _kbhit():问一句"现在有没有按键?"。有就返回真,没按就立刻返回假。
 *      有了它,程序不用卡死在那儿等输入,可以一边转圈一边问。
 *   2) _getch():真正把按键读出来,而且不用按回车。
 *   3) 方向键是"两个字节":第一次读到 0 或 224,要再读一次才是真正的键码:
 *          ↑ = 72      ↓ = 80      ← = 75      → = 77
 *   4) 按键之后:先擦旧位置,再画新位置 —— 和第 2 步一样,不留残影。
 *
 *  注意:这一步故意"不做"边界检查,所以方块能被推出屏幕。
 *        这正是第 4 步要解决的问题。
 * ===================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>          /* _kbhit() / _getch() */
#include <windows.h>

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

/* 把闪烁的光标藏起来,画面干净些 */
static void hide_cursor(void)
{
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 1;
    info.bVisible = FALSE;
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
}

static void paint(int gx, int gy, int erased)
{
    int r, c;

    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            if (shape[r][c] == 1) {
                gotoxy((gx + c) * 2, gy + r);
                printf(erased ? "  " : "[]");
            }
        }
    }
}

int main(void)
{
    int x = 8, y = 5;       /* 方块左上角所在的"格坐标" */
    int key, dir, nx, ny;

    system("chcp 65001 > nul");
    system("cls");
    hide_cursor();

    gotoxy(0, 10);
    printf("方向键移动方块,q 退出。");

    paint(x, y, 0);

    for (;;) {
        /* 没有按键就歇一下,免得 CPU 一直空转 */
        if (!_kbhit()) {
            Sleep(10);
            continue;
        }

        key = _getch();

        if (key == 'q' || key == 27) {      /* q 或 Esc:退出 */
            break;
        }
        if (key != 0 && key != 224) {       /* 不是方向键:忽略 */
            continue;
        }

        dir = _getch();                     /* 方向键的第二个字节 */

        nx = x;
        ny = y;

        if (dir == 72)      ny--;           /* 上 */
        else if (dir == 80) ny++;           /* 下 */
        else if (dir == 75) nx--;           /* 左 */
        else if (dir == 77) nx++;           /* 右 */
        else continue;                      /* 其它键不管 */

        /* 先擦旧位置,再画新位置 */
        paint(x, y, 1);
        paint(nx, ny, 0);
        x = nx;
        y = ny;
    }

    gotoxy(0, 12);
    printf("已退出。\n");

    return 0;
}
