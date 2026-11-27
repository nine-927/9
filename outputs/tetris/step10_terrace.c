/* =====================================================================
 *  俄罗斯方块 · 第 10 步:最终 terrace 场景
 * ---------------------------------------------------------------------
 *  到第 9 步为止,"玩"的部分已经完整:下落、固化、撞堆、
 *  消行、计分。第 10 步不新增任何机制,只做"场景"层面的收尾:
 *
 *   1) 开场标题 terrace:场地正上方打出游戏标题,
 *      右侧信息面板补上"已消行数",一眼看清全局。
 *   2) 结算画面:游戏结束后不再只打一行字,
 *      而是在场地中央叠加一块结算牌 —— 最终分数、消行数,
 *      按 r 重开一局,q / Esc 退出。
 *   3) 重开:把 board 清零、分数归零、重画场地、顶部生成
 *      新方块,一切回到最初 —— terrace 场景就此闭环。
 *
 *  至此,从第 1 步"把方块画出来"到第 10 步"完整的 terrace",
 *  每一步只前进一小格,拼起来就是一局能玩的游戏。
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
int lines = 0;

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

/* 开场标题:场地正上方的 terrace 横幅 */
static void draw_title(void)
{
    gotoxy(WIDTH * 2 + 6, 0);
    printf("T E R R A C E");
    gotoxy(WIDTH * 2 + 6, 1);
    printf("~ 俄罗斯方块 ~");
}

static void draw_wall(void)
{
    int r, c;

    gotoxy(0, 1);

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

/* 场地 + 右侧信息面板,一次画全 */
static void draw_field(void)
{
    int r, c;

    for (r = 0; r < HEIGHT; r++) {
        for (c = 0; c < WIDTH; c++) {
            gotoxy(1 + c * 2, 2 + r);
            printf(board[r][c] == 1 ? "[]" : "  ");
        }
    }

    gotoxy(WIDTH * 2 + 6, 3);
    printf("分数: %d    ", score);
    gotoxy(WIDTH * 2 + 6, 4);
    printf("消行: %d    ", lines);
    gotoxy(WIDTH * 2 + 6, 6);
    printf("← → 移动");
    gotoxy(WIDTH * 2 + 6, 7);
    printf("↓ 加速下落");
    gotoxy(WIDTH * 2 + 6, 8);
    printf("q 退出");
}

static void paint(int gx, int gy, int erased)
{
    int r, c;

    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            if (shape[r][c] == 1) {
                gotoxy(1 + (gx + c) * 2, 2 + (gy + r));
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

static int line_full(int r)
{
    int c;

    for (c = 0; c < WIDTH; c++) {
        if (board[r][c] == 0) return 0;
    }
    return 1;
}

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

static int clear_lines(void)
{
    int r, cleared = 0;

    for (r = HEIGHT - 1; r >= 0; r--) {
        if (line_full(r)) {
            clear_line(r);
            cleared++;
            r++;
        }
    }

    if (cleared > 0) {
        score += cleared * 100;
        lines += cleared;
        draw_field();
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
    clear_lines();
}

/* 开一局 / 重开一局:清场、清分、画框、顶部生成第一块 */
static int spawn(int *gx, int *gy)
{
    *gx = WIDTH / 2 - COLS / 2;
    *gy = 0;

    return can_place(*gx, *gy);
}

static void new_game(int *gx, int *gy, DWORD *last_fall)
{
    int r, c;

    system("cls");

    for (r = 0; r < HEIGHT; r++) {
        for (c = 0; c < WIDTH; c++) {
            board[r][c] = 0;
        }
    }
    score = 0;
    lines = 0;

    hide_cursor();
    draw_title();
    draw_wall();
    draw_field();

    spawn(gx, gy);
    paint(*gx, *gy, 0);

    *last_fall = GetTickCount();
}

/* 结算画面:场地中央的 terrace 结算牌,返回 1 = 重开,0 = 退出 */
static int game_over_screen(int final_score, int final_lines)
{
    int key;

    gotoxy(3, HEIGHT / 2 - 1);
    printf("+----------------+");
    gotoxy(3, HEIGHT / 2);
    printf("|   游 戏 结 束   |");
    gotoxy(3, HEIGHT / 2 + 1);
    printf("|  分数 %-9d |", final_score);
    gotoxy(3, HEIGHT / 2 + 2);
    printf("|  消行 %-9d |", final_lines);
    gotoxy(3, HEIGHT / 2 + 3);
    printf("| r 重开   q 退出 |");
    gotoxy(3, HEIGHT / 2 + 4);
    printf("+----------------+");

    for (;;) {
        if (_kbhit()) {
            key = _getch();
            if (key == 'r' || key == 'R') return 1;
            if (key == 'q' || key == 27)  return 0;
        }
        Sleep(10);
    }
}

int main(void)
{
    int x, y;
    int key, dir, nx, ny;
    int game_over;
    int running = 1;        /* 整个程序的开关:0 = 真正退出 */
    DWORD last_fall;

    system("chcp 65001 > nul");

    new_game(&x, &y, &last_fall);

    while (running) {
        game_over = 0;

        /* ---------- 一局游戏 ---------- */
        while (!game_over) {

            if (_kbhit()) {
                key = _getch();

                if (key == 'q' || key == 27) {
                    running = 0;        /* 直接退出整个程序 */
                    break;
                }

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
                        game_over = 1;  /* 出生点被堵死 */
                    } else {
                        paint(x, y, 0);
                    }
                }
            }

            Sleep(10);
        }

        if (!running) break;

        /* ---------- terrace 结算画面 ---------- */
        if (game_over_screen(score, lines)) {
            new_game(&x, &y, &last_fall);       /* r:重开一局 */
        } else {
            running = 0;                        /* q:退出 */
        }
    }

    system("cls");
    printf("感谢游玩,再见。\n");

    return 0;
}
