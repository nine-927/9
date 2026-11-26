/*
 * ============================================================================
 *   终端闯关文字游戏 —— 《秘境三关》  (Terminal Quest)
 * ============================================================================
 *   纯 C99/C11 编写，仅依赖标准库 + ANSI 终端控制码（Windows 下额外用
 *   Win32 API 开启虚拟终端以支持 ANSI 颜色，不属于第三方图形库）。
 *
 *   文件结构（单文件版，方便直接编译）：
 *       game.c    —— 全部源码
 *       Makefile  —— Linux/macOS/MinGW 构建
 *
 *   游戏流程：
 *       主菜单 → 开始游戏 → 第 1 关(猜数字谜题)
 *                        → 第 2 关(回合制怪物战斗)
 *                        → 第 3 关(迷宫逃脱)
 *                        → 通关结算 / 失败可重试或返回主菜单
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <math.h>

/* ---------------- Windows 平台：开启 ANSI 转义序列支持 ------------------- */
#ifdef _WIN32
#include <windows.h>
#include <conio.h>   /* _getch: 无需回车的即时按键 */
#include <io.h>      /* _isatty / _fileno: 判断输入是否被重定向 */

/* Windows 平台初始化：
 * 1) 把控制台输入/输出代码页切换为 UTF-8 (65001)，
 *    否则默认 GBK (936) 下 UTF-8 中文会显示为乱码；
 * 2) 打开 ENABLE_VIRTUAL_TERMINAL_PROCESSING 以支持 ANSI 转义码
 *    (Windows 10 之前的 cmd 默认不解析 ANSI)。 */
static void enable_ansi(void)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;

    /* 输出/输入统一为 UTF-8，与源码中文字符串的编码保持一致 */
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode)) {
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}

/* 跨平台毫秒级休眠 */
static void sleep_ms(int ms)
{
    Sleep(ms);
}

/* 判断标准输入是否为真正的交互式控制台。
 * 注意：在 Git Bash (mintty) 中运行原生 exe 时，stdin 是管道而非控制台，
 * _isatty 返回 0 —— 此时终端按行缓冲工作，按键必须等回车才会送达。 */
static int stdin_is_console(void)
{
    return _isatty(_fileno(stdin));
}

/* 即时按键读取（仅交互式控制台使用，无需按回车） */
static int read_key(void)
{
    int c = _getch();
    if (c == 0 || c == 224) {                /* 功能键/方向键前缀 */
        int ext = _getch();
        switch (ext) {
        case 72: return 'w';                 /* 上 -> 前进 */
        case 80: return 's';                 /* 下 -> 后退 */
        case 75: return 'a';                 /* 左 -> 左转 */
        case 77: return 'd';                 /* 右 -> 右转 */
        default: return -1;
        }
    }
    return c;
}
#else
#include <unistd.h>
#include <termios.h>

/* Linux / macOS：终端天然支持 ANSI，无需处理 */
static void enable_ansi(void) {}

static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* 判断标准输入是否为交互式控制台（管道/重定向时为 0） */
static int stdin_is_console(void)
{
    return isatty(STDIN_FILENO);
}

/* 即时按键读取（仅交互式终端使用）：临时关闭行缓冲和回显，读完立即恢复 */
static int read_key(void)
{
    struct termios oldt, newt;
    int c;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    c = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return c;
}
#endif

/* ---------------- ANSI 转义码与颜色定义 ---------------------------------- */
#define CLR_SCREEN   "\033[2J\033[H"     /* 清屏并回到左上角 */
#define CLR_RESET    "\033[0m"
#define CLR_RED      "\033[31m"
#define CLR_GREEN    "\033[32m"
#define CLR_YELLOW   "\033[33m"
#define CLR_BLUE     "\033[34m"
#define CLR_MAGENTA  "\033[35m"
#define CLR_CYAN     "\033[36m"
#define CLR_BOLD     "\033[1m"

/* ---------------- 游戏常量 ---------------------------------------------- */
#define MAX_LEVEL    3        /* 总关数 */
#define MAX_HP       100      /* 玩家生命上限 */
#define INPUT_BUF    128      /* 输入缓冲区大小 */

/* 第 1 关：猜数字 */
#define GUESS_RANGE  100      /* 数字范围 1~100 */
#define GUESS_CHANCE 7        /* 最大猜测次数 */
#define GUESS_MISS_HP 10      /* 每次猜错扣血 */

/* 第 2 关：战斗 */
#define MONSTER_HP   55       /* 怪物生命 */
#define POTION_COUNT 2        /* 药水数量 */
#define POTION_HEAL  18       /* 每瓶药水回复量 */

/* 第 3 关：第一人称伪 3D 迷宫（Raycasting） */
#define MAZE_W       9
#define MAZE_H       9
#define VIEW_W       76        /* 3D 视口宽度（字符列数） */
#define VIEW_H       22        /* 3D 视口高度（字符行数） */
#define FOV_PLANE    0.66f     /* 视平面半宽，约等于 66° 视场角 */
#define MOVE_STEP    0.40f     /* 每次前进/后退移动的格子数 */
#define TURN_STEP    0.2618f   /* 每次转向弧度（15°） */
#define MAZE_MOVES   60        /* 步数上限 */
#define TRAP_HP      15        /* 踩陷阱扣血 */
#define SPRITE_MAX   64        /* 场景精灵最大数量 */

/* 每关的基础得分 */
#define SCORE_L1     100
#define SCORE_L2     150
#define SCORE_L3     200

/* ---- 第 3 关专用：读取一个移动指令（自适应输入环境） --------------------
 *   1) 交互式控制台 (cmd / PowerShell / Windows Terminal / Linux 终端)：
 *      单键即时生效，无需回车；
 *   2) 行缓冲终端 (如在 Git Bash/mintty 中运行原生 exe，stdin 是管道)：
 *      按行读取，一行可包含多个指令，如 "wwwwdd"，回车后依次执行。
 * 返回：指令字符；0 表示空行（仅刷新画面）；-1 表示输入流结束。 */
static int read_cmd(void)
{
    static char line[INPUT_BUF];
    static int  pos = 0, len = 0;

    if (stdin_is_console()) {
        return read_key();                   /* 单键模式 */
    }
    if (pos >= len) {                        /* 行模式：取下一行指令 */
        if (fgets(line, sizeof(line), stdin) == NULL) return -1;
        len = (int)strlen(line);
        pos = 0;
    }
    while (pos < len && (line[pos] == '\n' || line[pos] == '\r' ||
                         line[pos] == ' '  || line[pos] == '\t')) {
        pos++;                               /* 跳过空白字符 */
    }
    if (pos >= len) {
        pos = len = 0;
        return 0;                            /* 空行 */
    }
    return (unsigned char)line[pos++];
}

/* ---------------- 玩家 / 游戏状态 ---------------------------------------- */
typedef struct {
    int hp;          /* 当前生命值 */
    int score;       /* 累计分数 */
    int level;       /* 当前关卡 (1 ~ MAX_LEVEL) */
} GameState;

/* ---------------- 通用工具函数 ------------------------------------------- */

/* 清屏（依赖 ANSI，若终端不支持则退化为打印若干空行） */
static void clear_screen(void)
{
    fputs(CLR_SCREEN, stdout);
}

/* 打印分隔线 */
static void print_line(void)
{
    printf("================================================================\n");
}

/* 暂停，等待玩家按回车继续 */
static void pause_enter(void)
{
    char buf[INPUT_BUF];
    printf(CLR_YELLOW "\n[按回车键继续...]" CLR_RESET);
    fflush(stdout);
    /* 读掉一行输入即可（包括换行符） */
    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        /* 输入流关闭（如 Ctrl+Z/Ctrl+D），直接退出程序 */
        exit(0);
    }
}

/* 从标准输入读取一行并解析为整数。
 * 返回 1 表示解析成功，0 表示输入非法（此时不修改 *out）。 */
static int read_int(int *out)
{
    char buf[INPUT_BUF];
    char *end = NULL;
    long val;

    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        exit(0); /* 输入流结束，直接退出 */
    }
    val = strtol(buf, &end, 10);
    /* 跳过尾随空白后必须恰好到达行尾，且有过数字，才算合法 */
    while (end && isspace((unsigned char)*end)) end++;
    if (end == buf || (end && *end != '\0')) {
        return 0;
    }
    *out = (int)val;
    return 1;
}

/* 读取一个"菜单选项"：要求输入 1~n 之间的整数，非法则反复提示 */
static int read_choice(int min, int max, const char *prompt)
{
    int val;
    for (;;) {
        printf("%s", prompt);
        if (read_int(&val) && val >= min && val <= max) {
            return val;
        }
        printf(CLR_RED ">> 输入无效！请输入 %d 到 %d 之间的整数。\n" CLR_RESET,
               min, max);
    }
}

/* 简易延时打字机效果（可选，短文本使用） */
static void slow_print(const char *s)
{
    for (; *s; s++) {
        putchar(*s);
        fflush(stdout);
        sleep_ms(12);
    }
}

/* ---------------- 状态栏 / 头部绘制 -------------------------------------- */

/* 用 '#' 绘制生命条，如 [#######...] */
static void draw_hp_bar(int hp, int max_hp)
{
    int cells = 20;                       /* 生命条总共 20 格 */
    int filled = max_hp > 0 ? (hp * cells + max_hp - 1) / max_hp : 0;
    int i;

    if (filled < 0)   filled = 0;
    if (filled > cells) filled = cells;

    printf("HP ");
    if (hp <= max_hp / 4) {
        printf(CLR_RED);                  /* 濒危时生命条显示红色 */
    } else {
        printf(CLR_GREEN);
    }
    putchar('[');
    for (i = 0; i < filled; i++)      putchar('#');
    for (i = filled; i < cells; i++)  putchar('.');
    putchar(']');
    printf(CLR_RESET " %d/%d", hp, max_hp);
}

/* 每屏顶部统一显示玩家状态 */
static void draw_status(const GameState *g)
{
    print_line();
    printf(" " CLR_BOLD "《秘境三关》" CLR_RESET);
    printf("  关卡: " CLR_CYAN "%d/%d" CLR_RESET, g->level, MAX_LEVEL);
    printf("  分数: " CLR_YELLOW "%d" CLR_RESET "\n", g->score);
    draw_hp_bar(g->hp, MAX_HP);
    putchar('\n');
    print_line();
}

/* ---------------- 第 1 关：猜数字谜题 ------------------------------------ */
/* 规则：猜 1~100 之间的神秘数字，共 7 次机会，每猜错一次扣 10 HP。 */
static int level1_guess(GameState *g)
{
    int answer, guess, tries = 0;

    answer = rand() % GUESS_RANGE + 1;

    printf(CLR_CYAN "\n【第 1 关 · 守门人的谜题】\n" CLR_RESET);
    slow_print("守门人：\"我心中藏着一个 1~100 的数字，\n");
    slow_print("         猜中它，门就为你打开；猜错 7 次，你将血流而退。\"\n\n");

    while (tries < GUESS_CHANCE) {
        printf(CLR_YELLOW "剩余机会: %d / %d\n" CLR_RESET,
               GUESS_CHANCE - tries, GUESS_CHANCE);
        printf("请输入你的猜测: ");

        if (!read_int(&guess)) {
            printf(CLR_RED ">> 无效输入！请输入一个整数。\n\n" CLR_RESET);
            continue;                 /* 非法输入不消耗机会 */
        }
        if (guess < 1 || guess > GUESS_RANGE) {
            printf(CLR_RED ">> 数字超出范围，请输入 1~100 之间的数。\n\n" CLR_RESET);
            continue;
        }

        tries++;
        if (guess == answer) {
            printf(CLR_GREEN "\n>> \"正是此数！\" 大门轰然开启！\n" CLR_RESET);
            g->score += SCORE_L1;
            printf(CLR_YELLOW ">> 获得 %d 分！当前分数: %d\n\n" CLR_RESET,
                   SCORE_L1, g->score);
            return 1;                 /* 通关 */
        } else if (guess < answer) {
            printf(CLR_BLUE ">> \"太小了……\" (损失 %d HP)\n\n" CLR_RESET,
                   GUESS_MISS_HP);
        } else {
            printf(CLR_MAGENTA ">> \"太大了……\" (损失 %d HP)\n\n" CLR_RESET,
                   GUESS_MISS_HP);
        }
        g->hp -= GUESS_MISS_HP;
        if (g->hp <= 0) {
            g->hp = 0;
            return 0;                 /* 血尽失败 */
        }
    }

    printf(CLR_RED ">> 机会耗尽！守门人摇头叹息，将你击退。(答案是 %d)\n" CLR_RESET,
           answer);
    return 0;
}

/* ---------------- 第 2 关：回合制战斗 ------------------------------------ */
/* 规则：与"石像守卫"回合制对战。
 *   1) 攻击   : 稳定造成 12~20 伤害
 *   2) 重击   : 五成概率造成 22~32 伤害，五成概率落空并被反击
 *   3) 防御   : 本回合受到的伤害减半
 *   4) 喝药水 : 回复 18 HP（全场共 2 瓶）
 * 怪物每回合造成 8~14 伤害。 */
static int level2_battle(GameState *g)
{
    int monster_hp = MONSTER_HP;
    int potions    = POTION_COUNT;
    int round      = 0;

    printf(CLR_CYAN "\n【第 2 关 · 石殿的守卫】\n" CLR_RESET);
    slow_print("一尊巨大的石像缓缓睁开了眼睛——石像守卫现身了！\n\n");

    while (g->hp > 0 && monster_hp > 0) {
        int choice, dmg, m_dmg;

        round++;
        printf(CLR_BOLD "---- 第 %d 回合 ----\n" CLR_RESET, round);
        printf("石像守卫 HP: ");
        {
            int cells = 20;
            int filled = monster_hp * cells / MONSTER_HP;
            int i;
            printf(CLR_RED "[");
            for (i = 0; i < filled; i++)      putchar('#');
            for (i = filled; i < cells; i++)  putchar('.');
            printf("]" CLR_RESET " %d/%d\n", monster_hp, MONSTER_HP);
        }
        draw_hp_bar(g->hp, MAX_HP);
        printf("   药水: " CLR_GREEN "%d 瓶\n" CLR_RESET, potions);
        printf("\n选择行动: 1)攻击  2)重击  3)防御  4)喝药水\n");

        choice = read_choice(1, 4, "> ");

        switch (choice) {
        case 1:                              /* 普通攻击 */
            dmg = 12 + rand() % 9;           /* 12~20 */
            monster_hp -= dmg;
            printf(CLR_GREEN ">> 你挥剑攻击，造成 %d 点伤害！\n" CLR_RESET, dmg);
            break;
        case 2:                              /* 重击：高风险高回报 */
            if (rand() % 2 == 0) {
                dmg = 22 + rand() % 11;      /* 22~32 */
                monster_hp -= dmg;
                printf(CLR_GREEN ">> 重击命中！造成 %d 点巨额伤害！\n" CLR_RESET, dmg);
            } else {
                printf(CLR_RED ">> 重击落空，露出破绽！(0 伤害)\n" CLR_RESET);
            }
            break;
        case 3:                              /* 防御标记 */
            printf(CLR_BLUE ">> 你举起盾牌，摆出防御姿态。\n" CLR_RESET);
            break;
        case 4:                              /* 喝药水 */
            if (potions <= 0) {
                printf(CLR_RED ">> 药水已用完！本回合白白浪费。\n" CLR_RESET);
            } else {
                potions--;
                g->hp += POTION_HEAL;
                if (g->hp > MAX_HP) g->hp = MAX_HP;
                printf(CLR_GREEN ">> 你喝下药水，回复 %d HP。\n" CLR_RESET,
                       POTION_HEAL);
            }
            break;
        default:
            break; /* read_choice 已保证不会走到这里 */
        }

        if (monster_hp <= 0) break;          /* 怪物已倒下，跳过怪物回合 */

        /* ---- 怪物回合 ---- */
        m_dmg = 8 + rand() % 7;              /* 8~14 */
        if (choice == 3) {
            m_dmg = (m_dmg + 1) / 2;         /* 防御时伤害减半（向上取整） */
            printf(CLR_BLUE ">> 守卫的攻击被盾牌挡下，只受到 %d 点伤害。\n" CLR_RESET,
                   m_dmg);
        } else {
            printf(CLR_RED ">> 守卫挥出石拳，你受到 %d 点伤害！\n" CLR_RESET,
                   m_dmg);
        }
        g->hp -= m_dmg;
        if (g->hp < 0) g->hp = 0;
        printf("\n");
        sleep_ms(400);
    }

    if (monster_hp <= 0) {
        printf(CLR_GREEN "\n>> 石像守卫轰然碎裂！石殿深处的大门打开了。\n" CLR_RESET);
        g->score += SCORE_L2;
        printf(CLR_YELLOW ">> 获得 %d 分！当前分数: %d\n\n" CLR_RESET,
               SCORE_L2, g->score);
        return 1;
    }
    printf(CLR_RED "\n>> 你倒在了石殿之中……\n" CLR_RESET);
    return 0;
}

/* ---------------- 第 3 关：第一人称伪 3D 迷宫 ---------------------------- */
/* 用光线投射 (Raycasting) 算法把 9x9 迷宫渲染成 Wolfenstein 3D 风格的
 * 第一人称字符画面（纯终端实现，不依赖任何图形库）：
 *   - 视口为 VIEW_W x VIEW_H 的字符网格，屏幕上每一列发射一条射线
 *   - DDA 算法求射线与墙壁的交点，距离越远字符越"虚"（雾效）
 *   - 南北朝向的墙面使用更暗一档的字符，制造明暗立体感
 *   - 出口 E、陷阱 * 作为"精灵"(sprite) 绘制，用 z-buffer 处理遮挡
 * 地图字符：'#'墙  '.'通路  'S'起点  'E'出口  '*'陷阱
 *
 * 坐标系：x 为列（向右为东），y 为行（向下为南），角度 0 表示朝东。
 */
static const char *MAZE_MAP[MAZE_H] = {
    "#########",
    "#S..#...#",
    "#.#.#.#.#",
    "#.#...#.#",
    "#.###.#*#",
    "#...#.#.#",
    "###.#.#.#",
    "#*..#..E#",
    "#########"
};

/* 场景精灵：立在迷宫中的可见物体（出口传送门、陷阱） */
typedef struct {
    float x, y;     /* 世界坐标（格子中心） */
    char  ch;       /* 显示字符 */
    char  color;    /* 'E' 显示绿色，'*' 显示红色 */
} Sprite;

/* 根据距离与墙面朝向选择墙壁字符：越近越实，越远越虚 */
static char wall_char(float dist, int side)
{
    char c;
    if (dist < 1.2f)      c = '@';   /* 贴着墙 */
    else if (dist < 2.4f) c = '#';
    else if (dist < 4.0f) c = '%';
    else if (dist < 6.0f) c = '=';
    else                  c = '-';   /* 远处的墙，几乎融进黑暗 */

    if (side == 1) {                 /* side==1 表示南北朝向的墙面，整体调暗一档 */
        switch (c) {
        case '@': c = '#'; break;
        case '#': c = '%'; break;
        case '%': c = '='; break;
        case '=': c = '-'; break;
        default: break;
        }
    }
    return c;
}

/* 判断浮点坐标所在格子是否为墙（用于移动碰撞检测） */
static int is_wall(const char grid[MAZE_H][MAZE_W], float x, float y)
{
    int cx = (int)x;
    int cy = (int)y;
    if (cx < 0 || cx >= MAZE_W || cy < 0 || cy >= MAZE_H) return 1;
    return grid[cy][cx] == '#';
}

/* 渲染一帧 3D 画面：
 *   scr  —— 字符画布
 *   tag  —— 每个格子对应的颜色标记（0=普通, 'E'=绿, '*'=红）
 *   zbuf —— 每列墙壁的垂直距离，供精灵遮挡判断使用 */
static void render_view(const char grid[MAZE_H][MAZE_W],
                        float px, float py, float ang,
                        float zbuf[VIEW_W],
                        char scr[VIEW_H][VIEW_W],
                        char tag[VIEW_H][VIEW_W])
{
    /* 相机方向向量与其垂直的视平面向量（共同决定视场角） */
    float dirX = cosf(ang);
    float dirY = sinf(ang);
    float planeX = -dirY * FOV_PLANE;
    float planeY =  dirX * FOV_PLANE;
    int x, y;

    memset(scr, ' ', sizeof(char) * VIEW_W * VIEW_H);
    memset(tag, 0, sizeof(char) * VIEW_W * VIEW_H);

    for (x = 0; x < VIEW_W; x++) {
        /* 把屏幕列映射到 [-1, 1] 的相机平面坐标 */
        float cameraX = 2.0f * (float)x / (float)VIEW_W - 1.0f;
        float rdx = dirX + planeX * cameraX;
        float rdy = dirY + planeY * cameraX;
        int mapX = (int)px;
        int mapY = (int)py;
        float ddx, ddy, sdx, sdy, perp;
        int stepX, stepY, side = 0, hit = 0;

        /* DDA 步进距离：每跨一格在 x/y 方向前进的距离 */
        ddx = (rdx == 0.0f) ? 1e30f : fabsf(1.0f / rdx);
        ddy = (rdy == 0.0f) ? 1e30f : fabsf(1.0f / rdy);

        if (rdx < 0) { stepX = -1; sdx = (px - mapX) * ddx; }
        else         { stepX =  1; sdx = (mapX + 1.0f - px) * ddx; }
        if (rdy < 0) { stepY = -1; sdy = (py - mapY) * ddy; }
        else         { stepY =  1; sdy = (mapY + 1.0f - py) * ddy; }

        /* 沿射线逐格前进，直到撞墙或越界 */
        while (!hit) {
            if (sdx < sdy) { sdx += ddx; mapX += stepX; side = 0; }
            else           { sdy += ddy; mapY += stepY; side = 1; }
            if (mapX < 0 || mapX >= MAZE_W || mapY < 0 || mapY >= MAZE_H) break;
            if (grid[mapY][mapX] == '#') hit = 1;
        }

        /* 计算垂直于视平面的距离（避免鱼眼畸变） */
        perp = (side == 0) ? (sdx - ddx) : (sdy - ddy);
        if (perp < 0.01f) perp = 0.01f;      /* 贴脸时防止除零 */
        zbuf[x] = perp;

        /* 按距离计算墙面在屏幕上的高度，并填充这一列 */
        {
            int line_h = (int)((float)VIEW_H / perp);
            int start  = VIEW_H / 2 - line_h / 2;
            int end    = VIEW_H / 2 + line_h / 2;
            char c = wall_char(perp, side);

            if (start < 0)        start = 0;
            if (end >= VIEW_H)    end   = VIEW_H - 1;
            for (y = start; y <= end; y++) {
                scr[y][x] = c;
                tag[y][x] = 0;               /* 墙壁不着色，保持中性 */
            }
        }
    }
}

/* 把场景中的精灵绘制到画布上（已按由远及近排序） */
static void render_sprites(const Sprite *sp, int n,
                           float px, float py, float ang,
                           const float zbuf[VIEW_W],
                           char scr[VIEW_H][VIEW_W],
                           char tag[VIEW_H][VIEW_W])
{
    float dirX = cosf(ang);
    float dirY = sinf(ang);
    float planeX = -dirY * FOV_PLANE;
    float planeY =  dirX * FOV_PLANE;
    /* 变换矩阵的行列式，用于把世界坐标转成相机坐标 */
    float inv_det = 1.0f / (planeX * dirY - dirX * planeY);
    int i;

    for (i = 0; i < n; i++) {
        float sx = sp[i].x - px;
        float sy = sp[i].y - py;
        /* tx 为横向偏移，ty 为深度（正数表示在正前方） */
        float tx = inv_det * ( dirY * sx - dirX * sy);
        float ty = inv_det * (-planeY * sx + planeX * sy);
        int cx, size, half, cy, y0, y1, x0, x1, x, y;

        if (ty < 0.2f) continue;             /* 在身后：看不见 */

        cx   = (int)((float)VIEW_W / 2.0f * (1.0f + tx / ty));
        size = (int)((float)VIEW_H / ty) / 2;/* 精灵高度取墙高的一半 */
        if (size < 1) size = 1;
        half = size / 2;
        cy   = VIEW_H / 2 + (int)((float)VIEW_H / ty / 4.0f);  /* 略微下移，像立在地面 */

        y0 = cy - half; y1 = cy + half;
        x0 = cx - half; x1 = cx + half;
        if (y0 < 0)        y0 = 0;
        if (y1 >= VIEW_H)  y1 = VIEW_H - 1;
        if (x0 < 0)        x0 = 0;
        if (x1 >= VIEW_W)  x1 = VIEW_W - 1;

        for (y = y0; y <= y1; y++) {
            for (x = x0; x <= x1; x++) {
                int on_edge = (x == x0 || x == x1 || y == y0 || y == y1);

                /* 只在比该列墙壁更近的位置绘制，实现正确遮挡 */
                if (ty >= zbuf[x]) continue;

                /* 出口画成空心的"门框"，远看是一道发光的门，
                 * 比实心色块更容易辨认；陷阱则保持实心，醒目危险。 */
                if (sp[i].ch == 'E' && !on_edge &&
                    (x1 - x0) > 2 && (y1 - y0) > 2) {
                    scr[y][x] = ' ';      /* 门内是幽暗的通道 */
                    tag[y][x] = 0;
                    continue;
                }
                scr[y][x] = sp[i].ch;
                tag[y][x] = sp[i].color;
            }
        }
    }
}

/* 输出画布：单元格颜色变化时才发送 ANSI 颜色码，减少转义序列数量 */
static void print_view(char scr[VIEW_H][VIEW_W], char tag[VIEW_H][VIEW_W])
{
    int y, x;
    char cur = 0;

    for (y = 0; y < VIEW_H; y++) {
        putchar(' ');
        cur = 0;
        for (x = 0; x < VIEW_W; x++) {
            if (tag[y][x] != cur) {
                fputs(CLR_RESET, stdout);
                if (tag[y][x] == 'E')      fputs(CLR_GREEN CLR_BOLD, stdout);
                else if (tag[y][x] == '*') fputs(CLR_RED CLR_BOLD, stdout);
                cur = tag[y][x];
            }
            putchar(scr[y][x]);
        }
        fputs(CLR_RESET "\n", stdout);
    }
}

/* 把角度换算成方位名（0 弧度朝东，顺时针为南/西/北） */
static const char *facing_name(float ang)
{
    float a = fmodf(ang, 6.2831853f);
    if (a < 0) a += 6.2831853f;
    if (a < 0.7854f || a >= 5.4978f) return "东";
    if (a < 2.3562f)                 return "南";
    if (a < 3.9270f)                 return "西";
    return "北";
}

/* 绘制顶部小地图（辅助认路，可按 M 隐藏） */
static void draw_minimap(const char grid[MAZE_H][MAZE_W],
                         float px, float py, float ang)
{
    float a = fmodf(ang, 6.2831853f);
    int y, x;
    char arrow;

    if (a < 0) a += 6.2831853f;
    if (a < 0.7854f || a >= 5.4978f)      arrow = '>';   /* 朝东 */
    else if (a < 2.3562f)                 arrow = 'v';   /* 朝南 */
    else if (a < 3.9270f)                 arrow = '<';   /* 朝西 */
    else                                  arrow = '^';   /* 朝北 */

    printf(CLR_BLUE "  小地图（M 键隐藏/显示）\n" CLR_RESET);
    for (y = 0; y < MAZE_H; y++) {
        printf("  ");
        for (x = 0; x < MAZE_W; x++) {
            if ((int)px == x && (int)py == y) {
                printf(CLR_GREEN CLR_BOLD "%c" CLR_RESET, arrow);
            } else if (grid[y][x] == '#') {
                putchar('#');
            } else if (grid[y][x] == 'E') {
                printf(CLR_YELLOW CLR_BOLD "E" CLR_RESET);
            } else if (grid[y][x] == '*') {
                printf(CLR_RED "*" CLR_RESET);
            } else if (grid[y][x] == 'S') {
                printf(CLR_CYAN "S" CLR_RESET);
            } else {
                putchar('.');
            }
        }
        putchar('\n');
    }
}

/* 第 3 关主循环：即时按键控制第一人称视角在迷宫中移动 */
static int level3_maze(GameState *g)
{
    char grid[MAZE_H][MAZE_W];
    char scr[VIEW_H][VIEW_W];
    char tag[VIEW_H][VIEW_W];
    Sprite sprites[SPRITE_MAX];
    float zbuf[VIEW_W];
    float px = 1.5f, py = 1.5f;      /* 玩家世界坐标（格子中心起步） */
    float ang = 0.0f;                /* 朝向：0 = 东 */
    int moves = 0;
    int trap_hit = 0;                /* 是否踩过陷阱（影响额外奖励分） */
    int show_map = 1;                /* 小地图开关 */
    int y;
    char msg[128] = "";

    /* 把只读地图拷贝到可修改的网格中（陷阱触发后会被清除） */
    for (y = 0; y < MAZE_H; y++) {
        memcpy(grid[y], MAZE_MAP[y], MAZE_W);
    }

    printf(CLR_CYAN "\n【第 3 关 · 迷宫逃脱】(第一人称 3D 视角)\n" CLR_RESET);
    slow_print("火把只照亮前方几步远的石壁，黑暗在四周呼吸。\n");
    slow_print("找到绿色的出口 'E'，别踩上红色的陷阱 '*'。\n\n");
    printf("操作: " CLR_BOLD "W" CLR_RESET " 前进   "
           CLR_BOLD "S" CLR_RESET " 后退   "
           CLR_BOLD "A" CLR_RESET " 左转   "
           CLR_BOLD "D" CLR_RESET " 右转   "
           CLR_BOLD "M" CLR_RESET " 小地图\n");
    if (stdin_is_console()) {
        printf("（按下按键立即生效，无需回车）\n");
    } else {
        /* 行缓冲终端（如 Git Bash）：按键要等回车才送达，
         * 改为支持一次输入一串指令，体验与单键模式一致 */
        printf("（当前终端为行缓冲模式：输入指令串后按回车即可连续执行，\n");
        printf("  例如输入 wwwwdd 表示前进 4 步再右转 2 次）\n");
    }
    pause_enter();

    for (;;) {
        int key, nsp = 0, i, j;
        float oldx, oldy;
        char cell;

        /* ---- 收集场景精灵：出口 + 尚未触发的陷阱 ---- */
        for (y = 0; y < MAZE_H; y++) {
            int x;
            for (x = 0; x < MAZE_W; x++) {
                char c = grid[y][x];
                if ((c == 'E' || c == '*') && nsp < SPRITE_MAX) {
                    sprites[nsp].x = (float)x + 0.5f;
                    sprites[nsp].y = (float)y + 0.5f;
                    sprites[nsp].ch = c;
                    sprites[nsp].color = c;
                    nsp++;
                }
            }
        }
        /* 按距离由远及近插入排序，保证近处精灵覆盖远处精灵 */
        for (i = 1; i < nsp; i++) {
            Sprite key_sp = sprites[i];
            float d = (key_sp.x - px) * (key_sp.x - px)
                    + (key_sp.y - py) * (key_sp.y - py);
            j = i - 1;
            while (j >= 0 &&
                   ((sprites[j].x - px) * (sprites[j].x - px) +
                    (sprites[j].y - py) * (sprites[j].y - py)) < d) {
                sprites[j + 1] = sprites[j];
                j--;
            }
            sprites[j + 1] = key_sp;
        }

        /* ---- 渲染一帧并显示 ---- */
        clear_screen();
        render_view(grid, px, py, ang, zbuf, scr, tag);
        render_sprites(sprites, nsp, px, py, ang, zbuf, scr, tag);
        print_view(scr, tag);

        /* ---- 状态栏 ---- */
        printf("\n ");
        draw_hp_bar(g->hp, MAX_HP);
        printf("   朝向: " CLR_CYAN "%s" CLR_RESET, facing_name(ang));
        printf("   步数: " CLR_YELLOW "%d/%d" CLR_RESET, moves, MAZE_MOVES);
        printf("\n");
        if (msg[0] != '\0') {
            printf(" " CLR_YELLOW "%s" CLR_RESET "\n", msg);
        }
        if (show_map) {
            putchar('\n');
            draw_minimap(grid, px, py, ang);
        }

        /* ---- 读取指令（单键模式或行缓冲模式，见 read_cmd） ---- */
        key = read_cmd();
        if (key == -1) exit(0);                  /* 输入流结束 */
        if (key == 0) continue;                  /* 空行：仅刷新画面 */
        key = toupper((unsigned char)key);
        msg[0] = '\0';
        oldx = px;
        oldy = py;

        switch (key) {
        case 'W':                                /* 前进 */
        case 'S': {                              /* 后退 */
            float sign = (key == 'W') ? 1.0f : -1.0f;
            float mx = cosf(ang) * MOVE_STEP * sign;
            float my = sinf(ang) * MOVE_STEP * sign;

            /* 分轴移动：撞墙时仍可沿墙面滑动 */
            if (!is_wall(grid, px + mx, py)) px += mx;
            if (!is_wall(grid, px, py + my)) py += my;

            if (px == oldx && py == oldy) {
                snprintf(msg, sizeof(msg), "%s", ">> 前面是石壁，撞得头晕眼花。");
            } else {
                moves++;
            }
            break;
        }
        case 'A': ang -= TURN_STEP; break;       /* 左转 */
        case 'D': ang += TURN_STEP; break;       /* 右转 */
        case 'M': show_map = !show_map; break;   /* 切换小地图 */
        default:
            snprintf(msg, sizeof(msg), "%s", ">> 无效按键，只能使用 W / A / S / D / M。");
            break;
        }

        /* ---- 判定所在格子 ---- */
        cell = grid[(int)py][(int)px];

        if (cell == 'E') {                       /* 抵达出口：通关 */
            clear_screen();
            printf(CLR_GREEN CLR_BOLD);
            printf("\n\n");
            printf("        ★  你冲出了迷宫，久违的星光洒在脸上  ★\n\n");
            printf(CLR_RESET);
            g->score += SCORE_L3;
            printf(CLR_YELLOW ">> 获得 %d 分！" CLR_RESET, SCORE_L3);
            if (!trap_hit) {
                g->score += 50;
                printf(CLR_YELLOW "（全程未踩陷阱，额外 +50）" CLR_RESET);
            }
            printf(CLR_YELLOW "\n>> 当前分数: %d\n" CLR_RESET, g->score);
            sleep_ms(1200);
            return 1;
        }

        if (cell == '*') {                       /* 踩中陷阱 */
            grid[(int)py][(int)px] = '.';        /* 触发后清除该陷阱 */
            trap_hit = 1;
            g->hp -= TRAP_HP;
            if (g->hp < 0) g->hp = 0;
            snprintf(msg, sizeof(msg), "%s", ">> 咔哒一声——踩中陷阱！生命值受损。");
        }

        if (g->hp <= 0) {                        /* 生命耗尽：失败 */
            printf(CLR_RED "\n>> 你倒在冰冷的石板上，火把滚落熄灭……\n" CLR_RESET);
            sleep_ms(1000);
            return 0;
        }
        if (moves >= MAZE_MOVES) {               /* 步数耗尽：失败 */
            printf(CLR_RED "\n>> 火把燃尽，黑暗吞没了一切，你再也没能走出去。\n" CLR_RESET);
            sleep_ms(1000);
            return 0;
        }
    }
}

/* ---------------- 关卡调度 ----------------------------------------------- */

/* 两个关卡之间的小休息：回复部分 HP */
static void rest_between_levels(GameState *g)
{
    int heal = 25;
    g->hp += heal;
    if (g->hp > MAX_HP) g->hp = MAX_HP;
    printf(CLR_GREEN "\n你在篝火旁稍作休息，回复了 %d HP。\n" CLR_RESET, heal);
    pause_enter();
}

/* 运行完整的一局游戏（从第 1 关打到第 3 关或中途失败） */
static void run_game(void)
{
    GameState g;
    int won = 0;

    g.hp    = MAX_HP;
    g.score = 0;
    g.level = 1;

    clear_screen();
    print_line();
    slow_print(CLR_BOLD "  秘 境 三 关\n" CLR_RESET);
    slow_print("  传说中，秘境深处藏着无尽的宝藏。\n");
    slow_print("  三道关卡，九死一生……祝你好运，冒险者！\n");
    print_line();
    pause_enter();

    /* 关卡循环：胜利进入下一关，失败询问重试或返回主菜单 */
    while (g.level <= MAX_LEVEL) {
        clear_screen();
        draw_status(&g);

        switch (g.level) {
        case 1: won = level1_guess(&g);  break;
        case 2: won = level2_battle(&g); break;
        case 3: won = level3_maze(&g);   break;
        }

        if (won) {
            g.level++;
            if (g.level <= MAX_LEVEL) {
                rest_between_levels(&g);   /* 通关后小憩回复 */
            }
        } else {
            /* ---- 失败处理界面 ---- */
            clear_screen();
            print_line();
            printf(CLR_RED CLR_BOLD "          你 被 击 败 了 …\n" CLR_RESET);
            print_line();
            printf("倒在第 %d 关。当前分数: %d\n\n", g.level, g.score);
            printf("1) 重试本关\n");
            printf("2) 返回主菜单\n");

            if (read_choice(1, 2, "> ") == 1) {
                /* 重试：把 HP 恢复到满（关卡内的状态由各关卡函数重置） */
                g.hp = MAX_HP;
            } else {
                return;              /* 回到主菜单 */
            }
        }
    }

    /* ---- 全部通关：结算界面 ---- */
    clear_screen();
    print_line();
    printf(CLR_YELLOW CLR_BOLD);
    printf("          *  *  *  通  关  结  算  *  *  *\n");
    printf(CLR_RESET);
    print_line();
    printf("\n");
    printf("   冒险者，你征服了秘境的全部三关！\n\n");
    printf("   剩余生命值 : ");
    draw_hp_bar(g.hp, MAX_HP);
    printf("\n");
    printf("   最终得分   : " CLR_YELLOW "%d\n" CLR_RESET, g.score);

    /* 按分数给出评价 */
    printf("   评价       : ");
    if      (g.score >= 500) printf(CLR_MAGENTA "传奇冒险家 ★★★\n" CLR_RESET);
    else if (g.score >= 430) printf(CLR_CYAN     "精英探险者   ★★\n" CLR_RESET);
    else                     printf(CLR_GREEN    "幸存者       ★\n" CLR_RESET);

    printf("\n");
    print_line();
    pause_enter();
}

/* ---------------- 主菜单 -------------------------------------------------- */

static void show_help(void)
{
    clear_screen();
    print_line();
    printf(CLR_BOLD "  游 戏 说 明\n" CLR_RESET);
    print_line();
    printf("  《秘境三关》是一款纯终端文字冒险游戏：\n\n");
    printf("  第 1 关  猜数字谜题 —— 猜中守门人的数字 (1~100)\n");
    printf("  第 2 关  回合制战斗 —— 击败强大的石像守卫\n");
    printf("  第 3 关  3D 迷宫逃脱 —— 第一人称视角走出迷宫（W/A/S/D 移动）\n\n");
    printf("  · 每关失败可选择重试或返回主菜单\n");
    printf("  · 前两关输入数字选项后按回车；第 3 关按 W/A/S/D 移动\n");
    printf("    （交互终端单键即时生效；Git Bash 等行缓冲终端可一次输入\n");
    printf("      一串指令如 wwwwdd 后按回车）\n");
    printf("  · 非法输入会有提示，不会崩溃\n");
    pause_enter();
}

static void show_menu(void)
{
    int running = 1;

    /* 初始化随机数种子（只做一次） */
    srand((unsigned int)time(NULL));
    enable_ansi();               /* Windows 下开启 ANSI 颜色支持 */

    while (running) {
        clear_screen();
        printf("\n");
        printf(CLR_CYAN CLR_BOLD);
        printf("   ╔══════════════════════════════════╗\n");
        printf("   ║        秘 境 三 关                ║\n");
        printf("   ║      Terminal Quest v1.1 (3D)    ║\n");
        printf("   ╚══════════════════════════════════╝\n");
        printf(CLR_RESET);
        printf("\n");
        printf("      " CLR_GREEN "1" CLR_RESET ") 开始游戏\n");
        printf("      " CLR_YELLOW "2" CLR_RESET ") 游戏说明\n");
        printf("      " CLR_RED   "3" CLR_RESET ") 退出游戏\n");
        printf("\n");

        switch (read_choice(1, 3, "   请选择 > ")) {
        case 1:
            run_game();
            break;
        case 2:
            show_help();
            break;
        case 3:
            printf(CLR_CYAN "\n感谢游玩，再见！\n" CLR_RESET);
            sleep_ms(600);
            running = 0;
            break;
        }
    }
}

/* ---------------- 程序入口 ------------------------------------------------ */
int main(void)
{
    show_menu();
    return 0;
}
