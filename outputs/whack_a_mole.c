/*
 * whack_a_mole.c —— 终端"打地鼠"小游戏
 *
 * 目标平台: Linux / macOS 终端 (或 Windows 下的 WSL)
 * 语言标准: C99 + POSIX (termios 非阻塞输入, 不依赖任何第三方库)
 *
 * 编译: gcc -std=c99 -Wall -Wextra -O2 whack_a_mole.c -o whack_a_mole
 * 运行: ./whack_a_mole
 *
 * 玩法: 3x3 格子中随机出现地鼠, 在它消失前按对应数字键 1~9 击打。
 *       击中 +10 分; 打错格子 -5 分并 -1 生命; 地鼠超时逃跑 -1 生命。
 *       击中越多, 地鼠停留时间越短(难度递增)。时间到或生命耗尽时游戏结束。
 *
 * 说明: 界面中的"♥"为 UTF-8 字符, 请在 UTF-8 终端下运行(主流终端默认即可)。
 */

#define _POSIX_C_SOURCE 199309L /* 提供 clock_gettime / nanosleep 等 POSIX 接口 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>

/* ==================== 可调整的难度参数 ==================== */
#define GAME_DURATION_SEC   60     /* 每局总时长(秒) */
#define START_LIVES         3      /* 初始生命值 */
#define HIT_SCORE           10     /* 击中地鼠得分 */
#define MISS_PENALTY        5      /* 打错格子扣分 */
#define MOLE_START_MS       1200   /* 地鼠最初停留时间(毫秒) */
#define MOLE_MIN_MS         450    /* 地鼠最短停留时间(毫秒) */
#define MOLE_SPEEDUP_MS     30     /* 每击中一次, 停留时间缩短的毫秒数 */
#define SPAWN_GAP_MS        300    /* 两只地鼠之间的间隔(毫秒) */
#define FRAME_MS            20     /* 输入轮询帧间隔(毫秒) */
#define SHOW_HINT           0      /* 1 = 额外显示地鼠所在格子编号(降低难度) */

/* ==================== 全局状态 ==================== */
static struct termios g_orig_termios; /* 原始终端设置, 退出时恢复 */
static int g_raw_enabled = 0;         /* 是否已进入 raw 模式 */
static int g_quit = 0;                /* 用户按 q 请求退出 */

/* ==================== 终端控制 ==================== */

/* 恢复终端原始设置(行缓冲、回显、光标) */
static void restore_terminal(void)
{
    if (g_raw_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        const char *show_cursor = "\033[?25h";
        ssize_t r = write(STDOUT_FILENO, show_cursor, strlen(show_cursor));
        (void)r; /* 仅 best-effort, 避免 -Wunused-result 警告 */
        g_raw_enabled = 0;
    }
}

/* Ctrl-C / kill 时也要把终端还原, 避免终端处于无回显状态 */
static void on_signal(int sig)
{
    (void)sig;
    restore_terminal(); /* write/tcsetattr 均为系统调用级操作, 此处可接受 */
    _exit(0);
}

/* 进入 raw 模式: 关闭行缓冲与回显, read 立即返回(非阻塞轮询) */
static void enable_raw_mode(void)
{
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) {
        fprintf(stderr, "错误: 无法获取终端属性, 请在真实终端中运行本程序。\n");
        exit(EXIT_FAILURE);
    }
    atexit(restore_terminal);              /* 正常退出时恢复 */
    signal(SIGINT, on_signal);             /* Ctrl-C 时恢复 */
    signal(SIGTERM, on_signal);            /* kill 时恢复 */

    raw = g_orig_termios;
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO); /* 关闭行缓冲与回显 */
    raw.c_cc[VMIN]  = 0;                       /* read 不等待 */
    raw.c_cc[VTIME] = 0;                       /* read 立即返回 */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        fprintf(stderr, "错误: 无法设置终端 raw 模式。\n");
        exit(EXIT_FAILURE);
    }
    g_raw_enabled = 1;
}

/* 非阻塞读一个按键: 无按键返回 -1 */
static int read_key(void)
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) == 1)
        return (int)c;
    return -1;
}

/* ==================== 时间工具 ==================== */

/* 当前单调时钟毫秒数(不受系统时间跳变影响) */
static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* 毫秒级睡眠 */
static void msleep(long long ms)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000);
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* ==================== 游戏逻辑 ==================== */

/*
 * 生成新的地鼠位置(0~8)。
 * 允许偶尔与上一只重复, 但大概率换位置, 避免"原地连刷"太频繁。
 */
static int spawn_mole(int last_pos)
{
    int pos = rand() % 9;
    if (pos == last_pos && (rand() & 1)) { /* 重复时 50% 概率强制换位 */
        do {
            pos = rand() % 9;
        } while (pos == last_pos);
    }
    return pos;
}

/* 绘制 3x3 格子。地鼠所在格子用黄色 (@) 显示。 */
static void draw_board(int mole_pos, int mole_active)
{
    for (int r = 0; r < 3; r++) {
        printf("        ");
        for (int c = 0; c < 3; c++) {
            int idx = r * 3 + c;
            if (mole_active && idx == mole_pos)
                printf("\033[1;33m(@)\033[0m "); /* 黄色地鼠 */
            else
                printf(" %d  ", idx + 1);
        }
        printf("\n\n");
    }
}

/*
 * 绘制整个界面。
 * 只在"状态变化"或"整秒变化"时才重绘, 避免频繁刷屏造成闪烁。
 */
static void draw_screen(int score, int lives, int time_left,
                        int mole_pos, int mole_active, const char *msg)
{
    if (time_left < 0)
        time_left = 0;

    printf("\033[2J\033[H"); /* 清屏并把光标移到左上角 */
    printf("\033[?25l");     /* 隐藏光标, 界面更干净 */

    printf("=============================================\n");
    printf("             打 地 鼠  Whack-a-Mole\n");
    printf("=============================================\n");
    printf("  分数: %-6d  生命: ", score);
    for (int i = 0; i < START_LIVES; i++) {
        if (i < lives)
            printf("\033[1;31m♥ \033[0m"); /* 剩余生命: 红色爱心 */
        else
            printf("- ");                  /* 已失去: 占位符 */
    }
    printf("  剩余时间: %ds\n\n", time_left);

    draw_board(mole_pos, mole_active);
    printf("\n");

    if (SHOW_HINT && mole_active)
        printf("  提示: 地鼠在第 %d 号格子\n", mole_pos + 1);
    printf("  %s\n", msg);
    printf("---------------------------------------------\n");
    printf("  操作: 按 1~9 击打对应格子, q 退出\n");
    printf("=============================================\n");
    fflush(stdout);
}

/*
 * 游戏结束画面: 显示最终得分与命中率, 询问是否重新开始。
 * 返回 1 = 再来一局, 0 = 退出。
 */
static int ask_restart(int score, int hits, int swings)
{
    printf("\033[2J\033[H\033[?25l");
    printf("=============================================\n");
    printf("                游 戏 结 束\n");
    printf("=============================================\n");
    printf("  最终得分: %d\n", score);
    printf("  击中地鼠: %d / 共出手 %d 次", hits, swings);
    if (swings > 0)
        printf("   命中率 %.0f%%\n", 100.0 * hits / (double)swings);
    else
        printf("\n");
    printf("=============================================\n");
    printf("  再来一局?  [y] 是    [n / 其他] 退出\n");
    fflush(stdout);

    for (;;) {
        int k = read_key();
        if (k == 'y' || k == 'Y')
            return 1;
        if (k == 'n' || k == 'N' || k == 'q' || k == 'Q' || k == 27)
            return 0;
        msleep(FRAME_MS); /* 忽略其他按键, 继续等待 */
    }
}

/*
 * 一局游戏主循环。
 * 通过指针带回最终得分/命中数/出手数; 正常结束返回 1, 用户按 q 返回 0。
 */
static int play_round(int *out_score, int *out_hits, int *out_swings)
{
    int score = 0, lives = START_LIVES;
    int hits = 0, swings = 0;

    int mole_active = 0;
    int mole_pos = -1, last_mole = -1;
    long long mole_deadline = 0;   /* 地鼠消失的截止时刻 */
    long long next_spawn = 0;      /* 下一次允许生成地鼠的时刻 */
    int mole_stay = MOLE_START_MS; /* 当前地鼠停留时长, 随命中递减 */

    char msg[128];
    snprintf(msg, sizeof msg, "游戏开始! 看准地鼠, 快按对应数字键!");

    long long start = now_ms();
    long long next_frame = start;
    int last_render_sec = -1;
    int dirty = 1; /* 需要重绘的标志 */

    while (lives > 0 && !g_quit) {
        long long now = now_ms();
        int time_left = GAME_DURATION_SEC - (int)((now - start) / 1000);

        if (time_left <= 0) { /* 时间到 */
            snprintf(msg, sizeof msg, "时间到! 本局结束。");
            dirty = 1;
            break;
        }

        /* --- 地鼠生成 --- */
        if (!mole_active && now >= next_spawn) {
            mole_pos = spawn_mole(last_mole);
            last_mole = mole_pos;
            mole_active = 1;
            mole_deadline = now + mole_stay;
            dirty = 1;
        }

        /* --- 地鼠超时逃跑 --- */
        if (mole_active && now >= mole_deadline) {
            mole_active = 0;
            lives--;
            next_spawn = now + SPAWN_GAP_MS;
            snprintf(msg, sizeof msg, "太慢了! 地鼠跑了, 生命 -1");
            dirty = 1;
        }

        /* --- 处理本帧内所有已到达的按键(清空输入队列, 防止堆积) --- */
        for (;;) {
            int k = read_key();
            if (k == -1)
                break;
            if (k == 'q' || k == 'Q') {
                g_quit = 1;
                break;
            }
            if (k < '1' || k > '9')
                continue; /* 非法按键: 直接忽略, 不崩溃 */

            int idx = k - '1'; /* 0~8 */
            swings++;
            if (mole_active && idx == mole_pos) {
                /* 击中 */
                hits++;
                score += HIT_SCORE;
                mole_active = 0;
                next_spawn = now + SPAWN_GAP_MS;
                if (mole_stay > MOLE_MIN_MS + MOLE_SPEEDUP_MS)
                    mole_stay -= MOLE_SPEEDUP_MS; /* 难度递增 */
                snprintf(msg, sizeof msg, "击中 %d 号! +%d 分 (地鼠更快了)",
                         idx + 1, HIT_SCORE);
            } else {
                /* 打错格子 */
                lives--;
                score -= MISS_PENALTY;
                if (score < 0)
                    score = 0;
                snprintf(msg, sizeof msg, "打错格子 %d! -%d 分, 生命 -1",
                         idx + 1, MISS_PENALTY);
            }
            dirty = 1;
        }
        if (g_quit)
            break;

        /* --- 绘制: 仅在状态变化或整秒变化时刷新, 界面稳定不闪烁 --- */
        int sec = (int)((now - start) / 1000);
        if (dirty || sec != last_render_sec) {
            draw_screen(score, lives, time_left, mole_pos, mole_active, msg);
            last_render_sec = sec;
            dirty = 0;
        }

        /* --- 帧率控制: 每FRAME_MS轮询一次输入 --- */
        next_frame += FRAME_MS;
        long long now2 = now_ms();
        if (next_frame > now2)
            msleep(next_frame - now2);
        else
            next_frame = now2; /* 掉帧时重新对齐, 避免疯狂补偿 */
    }

    *out_score  = score;
    *out_hits   = hits;
    *out_swings = swings;
    return g_quit ? 0 : 1;
}

/* ==================== 入口 ==================== */

int main(void)
{
    /* 用时间 + 进程号做种子, 避免同一秒内两次运行出现相同序列 */
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    enable_raw_mode(); /* atexit / 信号处理已保证退出时恢复终端 */

    int again = 1;
    while (again && !g_quit) {
        int score = 0, hits = 0, swings = 0;
        if (!play_round(&score, &hits, &swings))
            break; /* 用户按 q 中途退出 */
        again = ask_restart(score, hits, swings);
    }

    restore_terminal();
    printf("\n再见, 感谢游玩!\n");
    return 0;
}
