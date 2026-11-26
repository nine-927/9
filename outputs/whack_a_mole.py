# -*- coding: utf-8 -*-
"""
whack_a_mole.py —— 终端"打地鼠"小游戏 (Windows / Linux / macOS 通用)
与 C 版 whack_a_mole.c 玩法、参数完全一致:
  - 3x3 格子, 按 1~9 击打地鼠, q 退出
  - 击中 +10 分且地鼠提速; 打错 -5 分 -1 生命; 超时 -1 生命
  - 60 秒或生命耗尽结束, 可重开
直接运行: python whack_a_mole.py
"""

import os
import sys
import time
import random

# ==================== 可调整的难度参数 ====================
GAME_DURATION_SEC = 60     # 每局总时长(秒)
START_LIVES       = 3      # 初始生命值
HIT_SCORE         = 10     # 击中地鼠得分
MISS_PENALTY      = 5      # 打错格子扣分
MOLE_START_MS     = 1200   # 地鼠最初停留时间(毫秒)
MOLE_MIN_MS       = 450    # 地鼠最短停留时间(毫秒)
MOLE_SPEEDUP_MS   = 30     # 每击中一次, 停留时间缩短的毫秒数
SPAWN_GAP_MS      = 300    # 两只地鼠之间的间隔(毫秒)
FRAME_S           = 0.02   # 输入轮询帧间隔(秒)
SHOW_HINT         = 0      # 1 = 额外显示地鼠所在格子编号(降低难度)

IS_WIN = (os.name == "nt")
IS_MAC = (sys.platform == "darwin")

# ==================== 非阻塞键盘输入 ====================

if IS_WIN:
    import msvcrt
    import ctypes

    def _enable_ansi():
        """启用 Windows 10+ 控制台的 ANSI 转义序列支持"""
        k32 = ctypes.windll.kernel32
        h = k32.GetStdHandle(-11)          # STD_OUTPUT_HANDLE
        mode = ctypes.c_uint32()
        if k32.GetConsoleMode(h, ctypes.byref(mode)):
            k32.SetConsoleMode(h, mode.value | 0x0004)  # ENABLE_VIRTUAL_TERMINAL_PROCESSING

    def read_key():
        """非阻塞读一个按键, 无按键返回 None"""
        if msvcrt.kbhit():
            return msvcrt.getwch()         # 宽字符版, 避免中文编码问题
        return None

    _enable_ansi()

else:
    import termios
    import tty
    import atexit
    import select

    _orig = None

    def _enter_raw():
        global _orig
        _orig = termios.tcgetattr(sys.stdin.fileno())
        atexit.register(_restore)
        tty.setcbreak(sys.stdin.fileno())  # 关闭行缓冲与回显

    def _restore():
        if _orig is not None:
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSAFLUSH, _orig)
            _orig = None

    def read_key():
        """非阻塞读一个按键, 无按键返回 None"""
        r, _, _ = select.select([sys.stdin], [], [], 0)
        if r:
            return sys.stdin.read(1)
        return None

    _enter_raw()

# ==================== 绘制 ====================

def draw_screen(score, lives, time_left, mole_pos, mole_active, msg):
    if time_left < 0:
        time_left = 0
    out = []
    out.append("\033[2J\033[H")        # 清屏 + 光标回左上角
    out.append("\033[?25l")            # 隐藏光标
    out.append("=============================================")
    out.append("             打 地 鼠  Whack-a-Mole")
    out.append("=============================================")
    hearts = "".join("\033[1;31m♥ \033[0m" if i < lives else "- "
                     for i in range(START_LIVES))
    out.append("  分数: %-6d  生命: %s  剩余时间: %ds" % (score, hearts, time_left))
    out.append("")
    for r in range(3):
        row = "        "
        for c in range(3):
            idx = r * 3 + c
            if mole_active and idx == mole_pos:
                row += "\033[1;33m(@)\033[0m "   # 黄色地鼠
            else:
                row += " %d  " % (idx + 1)
        out.append(row)
        out.append("")
    if SHOW_HINT and mole_active:
        out.append("  提示: 地鼠在第 %d 号格子" % (mole_pos + 1))
    out.append("  %s" % msg)
    out.append("---------------------------------------------")
    out.append("  操作: 按 1~9 击打对应格子, q 退出")
    out.append("=============================================")
    sys.stdout.write("\n".join(out) + "\n")
    sys.stdout.flush()

def spawn_mole(last_pos):
    """生成新地鼠位置 0~8; 与上一只重复时有 50% 概率强制换位"""
    pos = random.randrange(9)
    if pos == last_pos and random.random() < 0.5:
        while pos == last_pos:
            pos = random.randrange(9)
    return pos

def show_cursor():
    sys.stdout.write("\033[?25h")
    sys.stdout.flush()

# ==================== 一局游戏 ====================

def play_round():
    score, lives = 0, START_LIVES
    hits, swings = 0, 0

    mole_active = False
    mole_pos = -1
    last_mole = -1
    mole_deadline = 0.0        # 地鼠消失的截止时刻
    next_spawn = 0.0           # 下一次允许生成地鼠的时刻
    mole_stay = MOLE_START_MS / 1000.0

    msg = "游戏开始! 看准地鼠, 快按对应数字键!"
    start = time.monotonic()
    next_frame = start
    last_render_sec = -1
    dirty = True

    while lives > 0:
        now = time.monotonic()
        time_left = GAME_DURATION_SEC - int(now - start)
        if time_left <= 0:
            msg = "时间到! 本局结束。"
            dirty = True
            break

        # --- 地鼠生成 ---
        if not mole_active and now >= next_spawn:
            mole_pos = spawn_mole(last_mole)
            last_mole = mole_pos
            mole_active = True
            mole_deadline = now + mole_stay
            dirty = True

        # --- 地鼠超时逃跑 ---
        if mole_active and now >= mole_deadline:
            mole_active = False
            lives -= 1
            next_spawn = now + SPAWN_GAP_MS / 1000.0
            msg = "太慢了! 地鼠跑了, 生命 -1"
            dirty = True

        # --- 处理本帧所有按键 ---
        while True:
            k = read_key()
            if k is None:
                break
            k = k.lower()
            if k == "q":
                return None, hits, swings
            if k not in "123456789":
                continue       # 非法按键: 直接忽略

            idx = int(k) - 1
            swings += 1
            if mole_active and idx == mole_pos:
                hits += 1
                score += HIT_SCORE
                mole_active = False
                next_spawn = now + SPAWN_GAP_MS / 1000.0
                if mole_stay > (MOLE_MIN_MS + MOLE_SPEEDUP_MS) / 1000.0:
                    mole_stay -= MOLE_SPEEDUP_MS / 1000.0   # 难度递增
                msg = "击中 %d 号! +%d 分 (地鼠更快了)" % (idx + 1, HIT_SCORE)
            else:
                lives -= 1
                score = max(0, score - MISS_PENALTY)
                msg = "打错格子 %d! -%d 分, 生命 -1" % (idx + 1, MISS_PENALTY)
            dirty = True

        # --- 绘制: 仅在状态变化或整秒变化时刷新 ---
        sec = int(now - start)
        if dirty or sec != last_render_sec:
            draw_screen(score, lives, time_left, mole_pos, mole_active, msg)
            last_render_sec = sec
            dirty = False

        # --- 帧率控制 ---
        next_frame += FRAME_S
        delay = next_frame - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        else:
            next_frame = time.monotonic()

    return score, hits, swings

def ask_restart(score, hits, swings):
    acc = ("   命中率 %.0f%%" % (100.0 * hits / swings)) if swings > 0 else ""
    sys.stdout.write(
        "\033[2J\033[H\033[?25l"
        "=============================================\n"
        "                游 戏 结 束\n"
        "=============================================\n"
        "  最终得分: %d\n"
        "  击中地鼠: %d / 共出手 %d 次%s\n"
        "=============================================\n"
        "  再来一局?  [y] 是    [n / 其他] 退出\n" % (score, hits, swings, acc))
    sys.stdout.flush()
    while True:
        k = read_key()
        if k is not None:
            k = k.lower()
            if k == "y":
                return True
            if k in ("n", "q", "\x1b"):
                return False
        time.sleep(FRAME_S)

# ==================== 入口 ====================

def main():
    try:
        again = True
        while again:
            random.seed()   # 每局重新播种
            score, hits, swings = play_round()
            if score is None:
                break       # 用户按 q 中途退出
            show_cursor()
            again = ask_restart(score, hits, swings)
    finally:
        show_cursor()       # 无论如何都恢复光标
        print("\n再见, 感谢游玩!")

if __name__ == "__main__":
    main()
