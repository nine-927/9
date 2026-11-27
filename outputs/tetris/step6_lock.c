/* =====================================================================
 *  俄罗斯方块 · 第 6 步:触底固化(引入场地数组 board)
 * ---------------------------------------------------------------------
 *  第 5 步的方块"掉不下去就停在原地",但它只是一个浮动的画影:
 *  你再按一下 ←,它又能横着滑走 —— 因为程序根本不记得
 *  "底部这里已经有一块方块了"。
 *
 *  这一步引入场地数组:
 *      int board[HEIGHT][WIDTH];
 *  board[r][c] == 1 表示场地的第 r 行第 c 列被"已经落下的方块"占着。
 *
 *  触发条件:自动下落时 can_place(x, y + 1) 失败 => 触底,
 *  此时不只是停在原地,而是把方块"写进" board —— 这叫固化。
 *  固化之后由 draw_field() 统一重画,方块从此长在场地上。
 *  (本步还故意留了一个尾巴:固化后当前方块还在原地,下一步处理。)
 * ===================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <windows.h>

#define WIDTH   10      /* 场地宽度(单位:格) */
#define HEIGHT  20      /* 场地高度(单位:格) */

#define ROWS 2
#define COLS 2

#define FALL_MS 500     /* 每 500 毫秒自动下落一格 */

int shape[ROWS][COLS] = {
    { 1, 1 },
    { 1, 1 }
};

/* 场地:1 = 该格已被落下的方块占据,0 = 空 */
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

/* 把 board 里所有已固化的格子画出来。
 * 每次固化后调用一次即可,平时不用反复重画。 */
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

/* 在场地里画 / 擦正在下落的方块 */
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
            }
        }
    }
    return 1;
}

/* 触底固化:把正在下落的方块写进 board,再重画一次场地 */
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

int main(void)
{
    int x = WIDTH / 2 - COLS / 2;
    int y = 0;
    int key, dir, nx, ny;
    DWORD last_fall;

    system("chcp 65001 > nul");
    system("cls");
    hide_cursor();

    draw_wall();
    paint(x, y, 0);

    gotoxy(0, HEIGHT + 2);
    printf("← → 移动    ↓ 加速下落    q 退出");

    last_fall = GetTickCount();

    for (;;) {

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
                /* 触底:这一格掉不动了,固化进 board */
                lock_piece(x, y);
            }
        }

        Sleep(10);
    }

    gotoxy(0, HEIGHT + 4);
    printf("已退出。\n");

    return 0;
}
