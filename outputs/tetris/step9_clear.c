/* =====================================================================
 *  俄罗斯方块 · 第 9 步:消行与计分
 * ---------------------------------------------------------------------
 *  第 8 步的堆只会越叠越高,就算一整行铺满了也不消失,
 *  很快就把场地堵死。缺的就是俄罗斯方块的灵魂:消行。
 *
 *  触发条件:每次 lock_piece() 固化之后,逐行检查 board:
 *      某一行 10 格全是 1  => 这一行"满了"。
 *
 *  消行做法很朴素:
 *      从满的那一行开始,上面每一行整体往下挪一行,
 *      最顶上空出来的一行补 0。棋盘数组一挪,画面重画一遍,
 *      看起来就是整行"唰"地消失、上面的堆整体沉降。
 *
 *  每消一行 score += 100,并在场地右侧实时显示。
 *  有了消行,terrace 才有"起伏":堆不断长高又不断被削平。
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

int score = 0;

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
            gotoxy(1 + c * 2, 1 + r);
            printf(board[r][c] == 1 ? "[]" : "  ");
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

/* 第 r 行是不是铺满了? */
static int line_full(int r)
{
    int c;

    for (c = 0; c < WIDTH; c++) {
        if (board[r][c] == 0) return 0;
    }
    return 1;
}

/* 从满行开始,上面整体下移一行,顶上补一行空 */
static void clear_line(int r)
{
    int rr, c;

    for (rr = r; rr > 0; rr--) {
        for (c = 0; c < WIDTH; c++) {
            board[rr][c] = board[rr - 1][c];
        }
    }
    for (c = 0; c < WIDTH; c++) {
        board[0][c] = 0;
    }
}

/* 固化之后调用:把所有满行都消掉,返回消了几行 */
static int clear_lines(void)
{
    int r, cleared = 0;

    for (r = HEIGHT - 1; r >= 0; r--) {
        if (line_full(r)) {
            clear_line(r);
            cleared++;
            r++;        /* 整体下移后,当前行要重新检查一遍 */
        }
    }

    if (cleared > 0) {
        score += cleared * 100;
        draw_field();
        gotoxy(WIDTH * 2 + 6, 3);
        printf("分数: %d    ", score);
    }
    return cleared;
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
    clear_lines();      /* 固化之后立刻检查消行 */
}

/* 生成下一块:回到顶部中央。
 * 不擦除旧画影:lock_piece() 已把旧位置重画为固化方块,
 * 再擦一次会把固化格子抹出空洞,造成闪烁。 */
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

    /* 场地右侧的信息面板 */
    gotoxy(WIDTH * 2 + 6, 1);
    printf("俄罗斯方块");
    gotoxy(WIDTH * 2 + 6, 3);
    printf("分数: 0    ");
    gotoxy(WIDTH * 2 + 6, 5);
    printf("← → 移动");
    gotoxy(WIDTH * 2 + 6, 6);
    printf("↓ 加速下落");
    gotoxy(WIDTH * 2 + 6, 7);
    printf("q 退出");

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
                    game_over = 1;
                } else {
                    paint(x, y, 0);
                }
            }
        }

        Sleep(10);
    }

    gotoxy(0, HEIGHT + 4);
    printf(game_over ? "场地满了,游戏结束。\n" : "已退出。\n");

    return 0;
}
