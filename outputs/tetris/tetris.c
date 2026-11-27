/* =============================================================================
 *  tetris.c —— 终端俄罗斯方块（纯 C11 + ANSI 转义序列，不依赖 ncurses）
 *
 *  编译：  make             （或 gcc -O2 -std=c11 -Wall -o tetris tetris.c）
 *  运行：  ./tetris
 *  退出：  游戏中按 Q（Ctrl-C 也能安全退出，终端状态会自动恢复）
 *
 *  设计要点：
 *    - 10 x 20 棋盘，7 种方块，7-bag 随机器（每 7 个方块为一轮洗牌，手感更公平）
 *    - 每种方块用 4 个 uint16 位掩码表示 4 个旋转态：初始态由形状表定义，
 *      其余 3 个旋转态在启动时用 rotate_cw() 对 4x4 网格做几何旋转自动生成，
 *      避免手写 28 组坐标出错
 *    - 输入是非阻塞的：POSIX 下用 termios 关闭行缓冲 + select 超时，
 *      Windows 下用 _kbhit/_getch，保证「边下落边响应按键」
 *    - 每帧把所有内容拼进一个大字符串，一次 write 输出，避免闪烁
 * ============================================================================= */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <conio.h>
#  include <io.h>
#  include <fcntl.h>
#else
#  include <termios.h>
#  include <unistd.h>
#  include <sys/select.h>
#  include <signal.h>
#endif

/* ----------------------------- 基本常量 ----------------------------- */
#define BOARD_W     10          /* 棋盘宽（格） */
#define BOARD_H     20          /* 棋盘高（格） */
#define SHAPE_N     7           /* 方块种类数 */
#define FRAME_MAX   65536       /* 单帧输出缓冲大小 */

/* 每种方块的颜色（ANSI 256 色背景色），索引 0 表示空格不用 */
static const char *PIECE_COLOR[SHAPE_N + 1] = {
    "",                     /* 0 : 空 */
    "\x1b[48;5;51m",        /* 1 : I 青 */
    "\x1b[48;5;226m",       /* 2 : O 黄 */
    "\x1b[48;5;201m",       /* 3 : T 紫 */
    "\x1b[48;5;27m",        /* 4 : J 蓝 */
    "\x1b[48;5;208m",       /* 5 : L 橙 */
    "\x1b[48;5;46m",        /* 6 : S 绿 */
    "\x1b[48;5;196m",       /* 7 : Z 红 */
};
static const char *COLOR_EMPTY = "\x1b[48;5;236m";  /* 空格：深灰底 */
static const char *COLOR_GHOST = "\x1b[48;5;240m";  /* 幽灵落点：中灰底 */
static const char *COLOR_RESET = "\x1b[0m";

/* 边框字符：默认用制表符；某些终端/字体会把制表符当双宽而错位，
 * 此时用 make ASCII=1（即定义 ASCII_BORDER）改用纯 ASCII 边框。 */
#ifdef ASCII_BORDER
#  define BH  "-"
#  define BV  "|"
#  define BTL "+"
#  define BTR "+"
#  define BBL "+"
#  define BBR "+"
#else
#  define BH  "\xe2\x94\x80"   /* ─ */
#  define BV  "\xe2\x94\x82"   /* │ */
#  define BTL "\xe2\x94\x8c"   /* ┌ */
#  define BTR "\xe2\x94\x90"   /* ┐ */
#  define BBL "\xe2\x94\x94"   /* └ */
#  define BBR "\xe2\x94\x98"   /* ┘ */
#endif
/* 横线重复：棋盘一行 20 个字符（10 格 x 2），预览框 8 个字符（4 格 x 2） */
#define REP20(s) s s s s s s s s s s s s s s s s s s s s
#define REP8(s)  s s s s s s s s

/* 消行得分表：一次消 0/1/2/3/4 行的基础分（再乘以当前等级） */
static const int LINE_SCORE[5] = { 0, 100, 300, 500, 800 };

/* 按键编码（把 Windows 扫描码 / ANSI 转义序列统一成枚举） */
enum {
    K_NONE = 0,
    K_LEFT, K_RIGHT, K_DOWN, K_UP,   /* 方向键 */
    K_ROT_CW,                        /* 旋转（↑ / W） */
    K_HARD,                          /* 硬降（空格） */
    K_PAUSE, K_RESTART, K_QUIT
};

/* ----------------------------- 游戏状态 ----------------------------- */
static int      board[BOARD_H][BOARD_W];    /* 0 = 空，1..7 = 已固定方块的颜色 */
static uint16_t shapes[SHAPE_N][4];         /* [种类][旋转态] 的 4x4 位掩码 */

static int      cur_type, cur_rot, cur_x, cur_y;   /* 当前活动方块 */
static int      next_type;                          /* 下一个方块 */
static int      score, lines_total, level;
static int      paused, game_over;
static double   drop_interval;              /* 当前等级下每格的自动下落间隔（秒） */
static double   last_drop;                  /* 上一次下落的时间戳 */

/* 7-bag 随机器 */
static int      bag[SHAPE_N];
static int      bag_pos = SHAPE_N;          /* == SHAPE_N 表示需要重新洗牌 */

/* 单帧输出缓冲 */
static char     frame[FRAME_MAX];
static size_t   flen;

/* ----------------------------- 平台相关 ----------------------------- */
#ifndef _WIN32
static struct termios g_orig_term;
static int            g_term_saved = 0;

/* 恢复终端原始模式（可重复调用） */
static void term_restore(void) {
    if (g_term_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_term);
        g_term_saved = 0;
    }
}

/* 收到 SIGINT/SIGTERM 时先恢复终端再退出，避免终端卡在无回显 / 备用屏幕里 */
static void on_signal(int sig) {
    (void)sig;
    term_restore();
    fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);   /* 复位颜色、显示光标、退出备用屏幕 */
    fflush(stdout);
    _exit(0);
}

/* 进入「非阻塞、无回显、无行缓冲」模式 */
static void term_init(void) {
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &g_orig_term) == 0) {
        g_term_saved = 1;
        t = g_orig_term;
        t.c_lflag &= ~(ICANON | ECHO);   /* 关闭行缓冲与回显 */
        t.c_cc[VMIN]  = 0;               /* 读操作不等待 */
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
        atexit(term_restore);
        signal(SIGINT,  on_signal);
        signal(SIGTERM, on_signal);
    }
}
#endif

/* 取单调递增的秒数，用于计算下落节奏（不受系统时间调整影响） */
static double now_sec(void) {
#ifdef _WIN32
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

/* 进入游戏界面：清屏、隐藏光标、开启 Windows 的 ANSI 支持 */
static void screen_init(void) {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  mode = 0;
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
    _setmode(_fileno(stdout), _O_BINARY);   /* 关掉 \r\n 自动转换，否则会输出 \r\r\n */
#endif
    fputs("\x1b[?1049h", stdout);   /* 切到备用屏幕，退出时还原原终端内容 */
    fputs("\x1b[?25l", stdout);     /* 隐藏光标 */
    fputs("\x1b[2J\x1b[H", stdout); /* 清屏并把光标移到左上角 */
    fflush(stdout);
}

static void screen_done(void) {
    fputs("\x1b[0m\x1b[?25h", stdout);   /* 恢复颜色、显示光标 */
    fputs("\x1b[?1049l", stdout);        /* 退回主屏幕 */
    fflush(stdout);
}

/* ----------------------------- 输入处理 ----------------------------- */
#ifdef _WIN32
/* Windows：轮询键盘，最多等待 timeout 秒 */
static int read_key(double timeout) {
    DWORD end = GetTickCount() + (DWORD)(timeout * 1000.0);
    for (;;) {
        if (_kbhit()) {
            int c = _getch();
            if (c == 0 || c == 224) {           /* 功能键/方向键前缀 */
                switch (_getch()) {
                    case 72: return K_UP;
                    case 80: return K_DOWN;
                    case 75: return K_LEFT;
                    case 77: return K_RIGHT;
                    default: return K_NONE;
                }
            }
            switch (c) {
                case 'a': case 'A': return K_LEFT;
                case 'd': case 'D': return K_RIGHT;
                case 's': case 'S': return K_DOWN;
                case 'w': case 'W': return K_ROT_CW;
                case ' ':           return K_HARD;
                case 'p': case 'P': return K_PAUSE;
                case 'r': case 'R': return K_RESTART;
                case 'q': case 'Q': return K_QUIT;
                default:            return K_NONE;
            }
        }
        if ((long)(GetTickCount() - end) >= 0) return K_NONE;
        Sleep(5);
    }
}
#else
static int g_eof = 0;   /* 标准输入被关闭（例如管道测试）时置位 */

/* 读一个字节；timeout 秒内没有输入则返回 -1 */
static int read_byte(double timeout) {
    fd_set rfds;
    struct timeval tv;
    unsigned char ch;
    ssize_t n;

    if (timeout < 0) timeout = 0;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    tv.tv_sec  = (long)timeout;
    tv.tv_usec = (long)((timeout - (double)tv.tv_sec) * 1000000.0);

    if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) <= 0)
        return -1;                       /* 超时：没有按键 */

    n = read(STDIN_FILENO, &ch, 1);
    if (n == 0) { g_eof = 1; return -1; }  /* 输入流结束 */
    if (n < 0)  return -1;
    return (int)ch;
}

/* POSIX：方向键以 ESC [ A/B/C/D 三字节序列到达，这里做解析 */
static int read_key(double timeout) {
    int c = read_byte(timeout);
    if (c < 0) return K_NONE;

    if (c == 0x1b) {                     /* ESC 开头，可能是转义序列 */
        int c2 = read_byte(0.02);
        if (c2 == '[' || c2 == 'O') {
            int c3 = read_byte(0.02);
            switch (c3) {
                case 'A': return K_UP;
                case 'B': return K_DOWN;
                case 'C': return K_RIGHT;
                case 'D': return K_LEFT;
                default:  return K_NONE;
            }
        }
        return K_NONE;                   /* 单独按 ESC：忽略 */
    }

    switch (c) {
        case 'a': case 'A': return K_LEFT;
        case 'd': case 'D': return K_RIGHT;
        case 's': case 'S': return K_DOWN;
        case 'w': case 'W': return K_ROT_CW;
        case ' ':           return K_HARD;
        case 'p': case 'P': return K_PAUSE;
        case 'r': case 'R': return K_RESTART;
        case 'q': case 'Q': return K_QUIT;
        default:            return K_NONE;
    }
}
#endif

/* --------------------------- 方块形状的定义 ---------------------------
 * 每个字节的高 4 位代表 4x4 网格的一行，最左边一格是 bit3。
 * 只需写出「出生态」，其余旋转态在 shapes_init() 里几何旋转生成。
 * ------------------------------------------------------------------- */
static const uint8_t SPAWN[SHAPE_N][4] = {
    /* I */ { 0x0, 0xF, 0x0, 0x0 },   /* ....  ####  ....  .... */
    /* O */ { 0x6, 0x6, 0x0, 0x0 },   /* .##.  .##.  ....  .... */
    /* T */ { 0x4, 0xE, 0x0, 0x0 },   /* .#..  ###.  ....  .... */
    /* J */ { 0x8, 0xE, 0x0, 0x0 },   /* #...  ###.  ....  .... */
    /* L */ { 0x2, 0xE, 0x0, 0x0 },   /* ..#.  ###.  ....  .... */
    /* S */ { 0x6, 0xC, 0x0, 0x0 },   /* .##.  ##..  ....  .... */
    /* Z */ { 0xC, 0x6, 0x0, 0x0 },   /* ##..  .##.  ....  .... */
};

/* 把一行的 4 个位「反转」后放到掩码低 4 位（bit3 表示最左格） */
static uint16_t row_bits(uint8_t row) {
    uint16_t b = 0;
    int c;
    for (c = 0; c < 4; c++)
        if (row & (0x8 >> c)) b |= (uint16_t)(1u << c);
    return b;
}

/* 4 个字节 -> 16 位掩码，bit (r*4+c) 表示第 r 行第 c 列有方块。
 * 注意：第 0 行必须放在低 4 位，与 collide()/rotate_cw() 里的 (r*4+c) 索引保持一致，
 * 否则整块会被上下镜像（J/L 会变成反手形状）。 */
static uint16_t make_mask(uint8_t r0, uint8_t r1, uint8_t r2, uint8_t r3) {
    return (uint16_t)( row_bits(r0)        | (row_bits(r1) << 4) |
                      (row_bits(r2) << 8)  | (row_bits(r3) << 12));
}

/* 顺时针旋转 90°：格子 (r,c) 移动到 (c, 3-r) */
static uint16_t rotate_cw(uint16_t m) {
    uint16_t out = 0;
    int r, c;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            if (m & (uint16_t)(1u << (r * 4 + c)))
                out |= (uint16_t)(1u << (c * 4 + (3 - r)));
    return out;
}

static void shapes_init(void) {
    int t, k;
    for (t = 0; t < SHAPE_N; t++) {
        shapes[t][0] = make_mask(SPAWN[t][0], SPAWN[t][1], SPAWN[t][2], SPAWN[t][3]);
        for (k = 1; k < 4; k++)
            shapes[t][k] = rotate_cw(shapes[t][k - 1]);
    }
}

/* ----------------------------- 随机生成 ----------------------------- */
/* 7-bag：把 7 种方块洗牌后依次发完，再洗下一轮，避免长时间不出某个方块 */
static int bag_next(void) {
    if (bag_pos >= SHAPE_N) {
        int i;
        for (i = 0; i < SHAPE_N; i++) bag[i] = i;
        for (i = SHAPE_N - 1; i > 0; i--) {      /* Fisher-Yates 洗牌 */
            int j = rand() % (i + 1);
            int tmp = bag[i];
            bag[i] = bag[j];
            bag[j] = tmp;
        }
        bag_pos = 0;
    }
    return bag[bag_pos++];
}

/* ----------------------------- 逻辑判定 ----------------------------- */
/* 判断方块在 (px,py) 处是否会碰撞：出界或与已固定方块重叠 */
static int collide(int type, int rot, int px, int py) {
    uint16_t m = shapes[type][rot];
    int r, c;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            int x, y;
            if (!(m & (uint16_t)(1u << (r * 4 + c)))) continue;
            x = px + c;
            y = py + r;
            if (x < 0 || x >= BOARD_W || y >= BOARD_H) return 1;   /* 越界 */
            if (y >= 0 && board[y][x]) return 1;                  /* 撞到已落方块 */
        }
    }
    return 0;
}

/* 尝试平移/下落；成功返回 1 */
static int try_move(int dx, int dy) {
    if (collide(cur_type, cur_rot, cur_x + dx, cur_y + dy)) return 0;
    cur_x += dx;
    cur_y += dy;
    return 1;
}

/* 生成新方块：把 next 变成当前块，再抽一个新的 next */
static void spawn_piece(void) {
    cur_type = next_type;
    cur_rot  = 0;
    cur_x    = 3;      /* 4 宽网格放在 10 宽棋盘的中间偏左，和标准手感一致 */
    cur_y    = 0;
    next_type = bag_next();

    /* 出生点就被占住 -> 游戏结束 */
    if (collide(cur_type, cur_rot, cur_x, cur_y)) game_over = 1;
}

/* 消行：从下往上扫描，满行就整行下移，返回本次消除的行数 */
static int clear_lines(void) {
    int cleared = 0, y;
    for (y = BOARD_H - 1; y >= 0; ) {
        int x, full = 1;
        for (x = 0; x < BOARD_W; x++) {
            if (!board[y][x]) { full = 0; break; }
        }
        if (full) {
            cleared++;
            /* 把第 0..y-1 行整体下移一行（memmove 处理了重叠区域） */
            if (y > 0)
                memmove(&board[1][0], &board[0][0],
                        (size_t)y * BOARD_W * sizeof(int));
            memset(&board[0][0], 0, (size_t)BOARD_W * sizeof(int));
            /* 不递减 y：同一索引现在是上面那一行，需要重新检查 */
        } else {
            y--;
        }
    }
    return cleared;
}

/* 按等级换算自动下落间隔：等级越高越快，最快 60ms 一格 */
static double interval_for_level(int lv) {
    double t = 0.75 - (double)(lv - 1) * 0.06;
    if (t < 0.06) t = 0.06;
    return t;
}

/* 固定当前方块 -> 消行 -> 计分 -> 升级 -> 出新块 */
static void lock_piece(void) {
    uint16_t m = shapes[cur_type][cur_rot];
    int r, c, n, new_level;

    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            if (m & (uint16_t)(1u << (r * 4 + c))) {
                int x = cur_x + c, y = cur_y + r;
                if (y >= 0 && y < BOARD_H && x >= 0 && x < BOARD_W)
                    board[y][x] = cur_type + 1;   /* 存 1..7 作为颜色号 */
            }

    n = clear_lines();
    if (n > 0) {
        score += LINE_SCORE[n] * level;      /* 一次消得越多分越高，再乘等级 */
        lines_total += n;

        new_level = lines_total / 10 + 1;    /* 每消 10 行升一级 */
        if (new_level > level) {
            level = new_level;
            drop_interval = interval_for_level(level);
        }
    }
    spawn_piece();
}

/* 旋转：先试原地，再左右微调（简单版 wall kick），最后试上移一格 */
static void rotate_piece(void) {
    static const int kick[5] = { 0, -1, 1, -2, 2 };
    int nr = (cur_rot + 1) % 4;
    int i, nx;

    for (i = 0; i < 5; i++) {
        nx = cur_x + kick[i];
        if (!collide(cur_type, nr, nx, cur_y)) {
            cur_rot = nr;
            cur_x  = nx;
            return;
        }
    }
    if (!collide(cur_type, nr, cur_x, cur_y - 1)) {   /* 贴地时向上踢 */
        cur_rot = nr;
        cur_y  -= 1;
    }
}

/* 硬降：一路下到底，每格 +2 分，然后立刻固定 */
static void hard_drop(void) {
    while (try_move(0, 1)) score += 2;
    lock_piece();
}

/* 重力：能下落就下落，不能下落就固定 */
static void gravity_step(void) {
    if (!try_move(0, 1)) lock_piece();
}

/* 重新开始：清空一切状态 */
static void game_reset(void) {
    memset(board, 0, sizeof(board));
    score       = 0;
    lines_total = 0;
    level       = 1;
    paused      = 0;
    game_over   = 0;
    drop_interval = interval_for_level(level);
    last_drop     = now_sec();
    bag_pos = SHAPE_N;          /* 强制重新洗牌 */
    next_type = bag_next();
    spawn_piece();
}

/* ----------------------------- 界面输出 ----------------------------- */
/* 往帧缓冲追加格式化文本 */
static void app(const char *fmt, ...) {
    va_list ap;
    int n;
    if (flen >= FRAME_MAX - 1) return;
    va_start(ap, fmt);
    n = vsnprintf(frame + flen, FRAME_MAX - flen, fmt, ap);
    va_end(ap);
    if (n > 0) {
        flen += (size_t)n;
        if (flen >= FRAME_MAX) flen = FRAME_MAX - 1;   /* 溢出保护 */
    }
}

/* 追加一个两字符宽的彩色格子 */
static void app_cell(const char *color) {
    app("%s  %s", color, COLOR_RESET);
}

/* 绘制整帧：棋盘 + 活动方块 + 幽灵落点 + 右侧信息面板 + 状态条 */
static void render(void) {
    int x, y, r, c;
    int ghost_y = cur_y;
    uint16_t cm = shapes[cur_type][cur_rot];

    /* 幽灵方块：一直往下探到不能落为止 */
    while (!collide(cur_type, cur_rot, cur_x, ghost_y + 1)) ghost_y++;

    flen = 0;
    app("\x1b[H");                              /* 光标回到左上角，整体重绘 */
    app("  俄罗斯方块  TETRIS\r\n");
    app("  " BTL REP20(BH) BTR "\r\n");   /* 10 格 x 2 字符 = 20 个横线 */

    for (y = 0; y < BOARD_H; y++) {
        app("  " BV);
        for (x = 0; x < BOARD_W; x++) {
            int v = board[y][x];
            int is_cur = 0, is_ghost = 0;

            if (!game_over) {
                for (r = 0; r < 4 && !is_cur; r++)
                    for (c = 0; c < 4; c++)
                        if ((cm & (uint16_t)(1u << (r * 4 + c))) &&
                            cur_x + c == x && cur_y + r == y) { is_cur = 1; break; }
                if (!is_cur && ghost_y != cur_y) {
                    for (r = 0; r < 4 && !is_ghost; r++)
                        for (c = 0; c < 4; c++)
                            if ((cm & (uint16_t)(1u << (r * 4 + c))) &&
                                cur_x + c == x && ghost_y + r == y) { is_ghost = 1; break; }
                }
            }

            if (is_cur)          app_cell(PIECE_COLOR[cur_type + 1]);
            else if (v)          app_cell(PIECE_COLOR[v]);
            else if (is_ghost)   app_cell(COLOR_GHOST);
            else                 app_cell(COLOR_EMPTY);
        }
        app(BV);

        /* 右侧信息面板：逐行对齐地贴在棋盘右边 */
        if (y == 0) {
            app("  下一个");
        } else if (y == 1) {
            app("  " BTL REP8(BH) BTR);
        } else if (y == 2 || y == 3) {
            int pr = y - 2;                     /* 预览只用出生态的前两行 */
            app("  " BV);
            for (c = 0; c < 4; c++) {
                if (shapes[next_type][0] & (uint16_t)(1u << (pr * 4 + c)))
                    app_cell(PIECE_COLOR[next_type + 1]);
                else
                    app("  ");
            }
            app(BV);
        } else if (y == 4) {
            app("  " BBL REP8(BH) BBR);
        } else if (y == 6) {
            app("   分数  %6d", score);
        } else if (y == 7) {
            app("   等级  %6d", level);
        } else if (y == 8) {
            app("   消行  %6d", lines_total);
        } else if (y == 10) {
            app("   方向键 / WASD");
        } else if (y == 11) {
            app("   ←→ 移动 ↑ 旋转");
        } else if (y == 12) {
            app("   ↓ 软降 空格 硬降");
        } else if (y == 13) {
            app("   P 暂停  R 重开");
        } else if (y == 14) {
            app("   Q 退出");
        }
        app("\r\n");
    }

    app("  " BBL REP20(BH) BBR "\r\n");

    /* 底部状态条：正常 / 暂停 / 结束 三种状态 */
    if (game_over)
        app("\x1b[1;31m  游戏结束！得分 %d，按 R 重开，Q 退出\x1b[0m", score);
    else if (paused)
        app("\x1b[1;33m  == 已暂停，按 P 继续 ==\x1b[0m");
    else
        app("\x1b[2m  方向键/WASD 移动旋转，空格硬降，P 暂停，R 重开，Q 退出\x1b[0m");

    fwrite(frame, 1, flen, stdout);   /* 一次性输出整帧，避免闪烁 */
    fflush(stdout);
}

/* ------------------------------- 主循环 ----------------------------- */
int main(void) {
    int running = 1, dirty = 1;

#ifndef _WIN32
    term_init();
#endif
    srand((unsigned)time(NULL) ^ (unsigned)(now_sec() * 1000.0));
    shapes_init();
    screen_init();
    game_reset();

    while (running) {
        double now = now_sec();
        double wait = 0.03;          /* 最长睡 30ms，保证按键响应灵敏 */
        int k;

        if (!paused && !game_over) {
            double remain = drop_interval - (now - last_drop);
            if (remain < wait) wait = remain;   /* 到点就提前醒来做下落 */
        }
        if (wait < 0) wait = 0;

        k = read_key(wait);
#ifndef _WIN32
        if (g_eof) break;            /* 标准输入被关闭（管道/重定向）时退出 */
#endif
        if (k == K_QUIT) break;
        if (k == K_RESTART) { game_reset(); dirty = 1; continue; }
        if (k == K_PAUSE) {
            if (!game_over) {
                paused = !paused;
                if (!paused) last_drop = now_sec();   /* 恢复时重置计时，防止瞬间掉落 */
                dirty = 1;
            }
            continue;
        }

        if (!paused && !game_over) {
            switch (k) {
                case K_LEFT:
                    if (try_move(-1, 0)) dirty = 1;
                    break;
                case K_RIGHT:
                    if (try_move(1, 0)) dirty = 1;
                    break;
                case K_DOWN:                          /* 软降：每格 +1 分 */
                    if (try_move(0, 1)) {
                        score += 1;
                        last_drop = now_sec();
                        dirty = 1;
                    }
                    break;
                case K_UP:
                case K_ROT_CW:
                    rotate_piece();
                    dirty = 1;
                    break;
                case K_HARD:
                    hard_drop();
                    dirty = 1;
                    break;
                default:
                    break;
            }
        }

        /* 到时间就自动下落一格 */
        if (!paused && !game_over && now_sec() - last_drop >= drop_interval) {
            gravity_step();
            last_drop = now_sec();
            dirty = 1;
        }

        if (dirty) { render(); dirty = 0; }   /* 状态变化时才重绘 */
    }

    screen_done();
    return 0;
}
