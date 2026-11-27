/* =====================================================================
 *  俄罗斯方块 · 第 7 步:撞堆检测(方块会"停"在别的方块上)
 * ---------------------------------------------------------------------
 *  第 6 步固化之后有个尴尬的现象:第二块方块会径直穿过
 *  第一块固化好的方块,直到撞上场地底部才停下。
 *  因为 can_place() 只认识"墙",不认识 board 里的堆。
 *
 *  修改只有一处:can_place() 里增加一条判断 ——
 *      目标格如果 board[y][x] 已经是 1,这一动就不合法。
 *
 *  于是方块会稳稳落在堆顶,一格压一格,慢慢叠出"台阶"。
 *  这正是 terrace 场景的地基:堆本身成了新的"地面"。
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

/* 合法 = 不撞墙,也不撞堆。 */
static int can_place(int gx, int gy)
{
    int r, c;

    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            if (shape[r][c] == 1) {
                int x = gx + c;
                int y = gy + r;

                if (x < 0 || x >= WIDTH)  return 0;   /* 撞左右墙 */
                if (y < 0 || y >= HEIGHT) return 0;   /* 撞上 / 下边 */
                if (board[y][x] == 1)     return 0;   /* 撞上已固化的堆 */
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
                lock_piece(x, y);
            }
        }

        Sleep(10);
    }

    gotoxy(0, HEIGHT + 4);
    printf("已退出。\n");

    return 0;
}
