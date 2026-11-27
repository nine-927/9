/* =====================================================================
 *  俄罗斯方块 · 第 8 步:新方块生成(固化之后,从顶部再来一块)
 * ---------------------------------------------------------------------
 *  第 7 步的方块固化后就"瘫"在原地不动了,游戏到此为止。
 *  真正的俄罗斯方块是:一块落地,紧接着顶部又生出新的下一块。
 *
 *  新增 spawn():
 *      1) 把当前方块擦掉(它已经固化进 board,画影不要了);
 *      2) 位置回到顶部中央,y 从 0 重新开始下落;
 *      3) 出生前先 can_place() 检查一次:出生点如果已经被堆
 *         顶到,说明场地满了 —— 置 game_over = 1,主循环退出。
 *
 *  从这一步起,游戏有了"一局"的概念:一块接一块,
 *  堆越来越高,terrace 越叠越完整,直到顶部堵死为止。
 * ===================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <windows.h>

#define WIDTH   10
#define HEIGHT  20

#define ROWS 2
#define COLS 2

#define FALL_MS 500

int shape[ROWS][COLS] = {
    { 1, 1 },
    { 1, 1 }
};

int board[HEIGHT][WIDTH];

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

static void draw_field(void)
{
    int r, c;

    for (r = 0; r < HEIGHT; r++) {
        for (c = 0; c < WIDTH; c++) {
            if (board[r][c] == 1) {
                gotoxy(1 + c * 2, 1 + r);
                printf("[]");
            }
        }
    }
}

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
                if (board[y][x] == 1)     return 0;
            }
        }
    }
    return 1;
}

static void lock_piece(int gx, int gy)
{
    int r, c;

    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            if (shape[r][c] == 1) {
                board[gy + r][gx + c] = 1;
            }
        }
    }
    draw_field();
}

/* 生成下一块:回到顶部中央。
 * 注意:这里不再擦除旧画影 —— lock_piece() 已经把旧位置
 * 重画成了固化方块,再擦一次反而会把固化格子抹出空洞,
 * 造成"先消失再出现"的闪烁。 */
static int spawn(int *gx, int *gy)
{
    *gx = WIDTH / 2 - COLS / 2;
    *gy = 0;

    return can_place(*gx, *gy);
}

int main(void)
{
    int x = WIDTH / 2 - COLS / 2;
    int y = 0;
    int key, dir, nx, ny;
    int game_over = 0;
    DWORD last_fall;

    system("chcp 65001 > nul");
    system("cls");
    hide_cursor();

    draw_wall();
    paint(x, y, 0);

    gotoxy(0, HEIGHT + 2);
    printf("← → 移动    ↓ 加速下落    q 退出");

    last_fall = GetTickCount();

    while (!game_over) {

        if (_kbhit()) {
            key = _getch();

            if (key == 'q' || key == 27) break;

            if (key == 0 || key == 224) {
                dir = _getch();

                nx = x;
                ny = y;

                if (dir == 72)      ny--;
                else if (dir == 80) ny++;
                else if (dir == 75) nx--;
                else if (dir == 77) nx++;
                else continue;

                if (can_place(nx, ny)) {
                    paint(x, y, 1);
                    paint(nx, ny, 0);
                    x = nx;
                    y = ny;

                    if (dir == 80) last_fall = GetTickCount();
                }
            }
        }

        if (GetTickCount() - last_fall >= FALL_MS) {
            last_fall = GetTickCount();

            if (can_place(x, y + 1)) {
                paint(x, y, 1);
                paint(x, y + 1, 0);
                y++;
            } else {
                lock_piece(x, y);

                if (!spawn(&x, &y)) {
                    game_over = 1;      /* 出生点被堵死,这一局到此为止 */
                } else {
                    paint(x, y, 0);     /* 画出新的下一块 */
                }
            }
        }

        Sleep(10);
    }

    gotoxy(0, HEIGHT + 4);
    printf(game_over ? "场地满了,游戏结束。\n" : "已退出。\n");

    return 0;
}
