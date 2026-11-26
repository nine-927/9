# -*- coding: utf-8 -*-
"""
whack_a_mole_app.py —— 桌面版"打地鼠"（独立窗口应用，纯 Python 标准库 tkinter）

双击 run_app.bat 或运行：pythonw whack_a_mole_app.py

规则与终端版一致：
  · 3x3 格子，地鼠随机冒头，按 1~9 或鼠标点击击打
  · 击中 +10 分，且地鼠停留时间变短（难度递增）
  · 打错格子 -5 分并 -1 生命；地鼠超时逃跑 -1 生命
  · 60 秒结束或生命归零结束，显示得分与命中率，可一键重开
"""

import random
import time
import tkinter as tk
from tkinter import font as tkfont

# ==================== 可调整的难度参数 ====================
GAME_DURATION_SEC = 60      # 每局总时长(秒)
START_LIVES       = 3       # 初始生命值
HIT_SCORE         = 10      # 击中得分
MISS_PENALTY      = 5       # 打错扣分
MOLE_START_MS     = 1200    # 地鼠初始停留时间(毫秒)
MOLE_MIN_MS       = 450     # 地鼠最短停留时间
MOLE_SPEEDUP_MS   = 30      # 每击中一次缩短的停留时间
SPAWN_GAP_MS      = 300     # 两只地鼠之间的间隔

# ==================== 配色 ====================
BG_TOP     = (251, 243, 228)
BG_BOTTOM  = (238, 223, 198)
C_CARD     = "#FFFBF3"
C_BORDER   = "#B98A5A"
C_TEXT     = "#4A3520"
C_SUBTLE   = "#8B7355"
C_HOLE_IN  = "#2E2117"
C_HOLE_MID = "#4A3323"
C_HOLE_OUT = "#6B4A2E"
C_RIM      = "#8B5E34"
C_MOLE     = "#B9824C"
C_MOLE_DK  = "#94623A"
C_MOLE_LT  = "#C99560"
C_SNOUT    = "#E2C39C"
C_NOSE     = "#E08A8A"
C_HEART    = "#D84343"
C_HEART_OFF = "#D6C6B2"
C_GREEN    = "#2E9E52"
C_RED      = "#C0392B"
C_BAR_BG   = "#E2D2BA"

W, H = 560, 760
GRID_X0, GRID_Y0 = 54, 170
CELL, GAP = 140, 16

FONT = "Microsoft YaHei"


def lerp_color(c1, c2, t):
    return "#%02x%02x%02x" % tuple(int(a + (b - a) * t) for a, b in zip(c1, c2))


class WhackAMole:
    def __init__(self, root: tk.Tk):
        self.root = root
        root.title("打地鼠 · Whack-a-Mole")
        root.resizable(False, False)
        root.configure(bg="#FBF3E4")

        self.canvas = tk.Canvas(root, width=W, height=H, highlightthickness=0, bd=0)
        self.canvas.pack()
        self.canvas.focus_set()

        self.cells = [
            (GRID_X0 + c * (CELL + GAP), GRID_Y0 + r * (CELL + GAP))
            for r in range(3) for c in range(3)
        ]

        self.build_static_ui()
        self.build_dynamic_ui()

        self.canvas.bind("<Button-1>", self.on_click)
        root.bind("<KeyPress>", self.on_key)
        root.protocol("WM_DELETE_WINDOW", self.close)

        self.reset_game()
        self.tick()

    # ---------------- 静态界面(只画一次) ----------------
    def build_static_ui(self):
        cv = self.canvas
        # 竖向渐变背景
        bands = 76
        for i in range(bands):
            y0 = H * i / bands
            y1 = H * (i + 1) / bands
            cv.create_rectangle(0, y0, W, y1, width=0,
                                fill=lerp_color(BG_TOP, BG_BOTTOM, i / (bands - 1)))

        # 标题
        cv.create_text(W / 2, 40, text="打 地 鼠", font=(FONT, 24, "bold"), fill=C_TEXT)
        cv.create_text(W / 2, 70, text="W H A C K - A - M O L E", font=("Segoe UI", 8, "bold"),
                       fill="#B49A78")

        # 卡片底板
        cv.create_rectangle(GRID_X0 - 18, GRID_Y0 - 18,
                            W - GRID_X0 + 18, GRID_Y0 + 3 * CELL + 2 * GAP + 18,
                            fill=C_CARD, outline=C_BORDER, width=3)

        # 洞口
        for i, (x, y) in enumerate(self.cells):
            cx, cy = x + CELL / 2, y + CELL / 2
            r = CELL / 2 - 8
            # 用几层椭圆模拟洞口立体感
            cv.create_oval(cx - r, cy - r, cx + r, cy + r, fill=C_RIM, outline="")
            cv.create_oval(cx - r + 5, cy - r + 5, cx + r - 5, cy + r - 5,
                           fill=C_HOLE_OUT, outline="")
            cv.create_oval(cx - r + 12, cy - r + 12, cx + r - 12, cy + r - 12,
                           fill=C_HOLE_MID, outline="")
            cv.create_oval(cx - r + 22, cy - r + 22, cx + r - 22, cy + r - 22,
                           fill=C_HOLE_IN, outline="")
            cv.create_text(x + CELL / 2, y + 16, text=str(i + 1),
                           font=(FONT, 10, "bold"), fill="#C9B79C")

        # 底部说明
        cv.create_text(W / 2, H - 34,
                       text="按 1~9 击打对应格子   ·   也可用鼠标点击地鼠   ·   R 重开   ·   Esc 退出",
                       font=(FONT, 9), fill=C_SUBTLE)

    # ---------------- 动态界面(每帧更新) ----------------
    def build_dynamic_ui(self):
        cv = self.canvas
        self.t_score = cv.create_text(GRID_X0, 98, anchor="w", text="",
                                      font=(FONT, 13, "bold"), fill=C_TEXT)
        self.t_time = cv.create_text(W - GRID_X0, 98, anchor="e", text="",
                                     font=(FONT, 13, "bold"), fill=C_TEXT)

        # 生命(心形)
        self.heart_ids = []
        for i in range(START_LIVES):
            self.heart_ids.append(
                cv.create_text(GRID_X0 + 250 + i * 26, 98, text="♥",
                               font=("Segoe UI Symbol", 15), fill=C_HEART))

        # 时间条
        self.bar_x0, self.bar_x1, self.bar_y = GRID_X0, W - GRID_X0, 126
        cv.create_rectangle(self.bar_x0, self.bar_y, self.bar_x1, self.bar_y + 10,
                            fill=C_BAR_BG, outline="")
        self.bar_fill = cv.create_rectangle(self.bar_x0, self.bar_y, self.bar_x1,
                                            self.bar_y + 10, fill=C_GREEN, outline="")

        # 提示文字
        self.t_msg = cv.create_text(W / 2, GRID_Y0 + 3 * CELL + 2 * GAP + 34, text="",
                                    font=(FONT, 11, "bold"), fill=C_TEXT,
                                    width=W - 2 * GRID_X0)

        # 遮罩(游戏结束时显示)
        self.overlay = []
        self.overlay_visible = False

    # ---------------- 游戏状态 ----------------
    def reset_game(self):
        self.score = 0
        self.lives = START_LIVES
        self.hits = 0
        self.swings = 0
        self.mole_idx = -1
        self.last_mole = -1
        self.mole_stay = MOLE_START_MS / 1000.0
        self.mole_born = 0.0
        self.mole_deadline = 0.0
        self.next_spawn = time.monotonic() + 0.8
        self.start_ts = time.monotonic()
        self.time_left = float(GAME_DURATION_SEC)
        self.phase = "playing"
        self.mole_items = []
        self.float_items = []
        self.float_x = {}
        self.flash_items = []

        if self.overlay_visible:
            self.hide_overlay()

        self.set_msg("准备好了吗？看准地鼠，快速按下对应数字键！", C_TEXT)

    def set_msg(self, text, color=C_TEXT):
        self.canvas.itemconfigure(self.t_msg, text=text, fill=color)

    def spawn_mole(self, now):
        """随机生成地鼠；与上一只重复时 50% 概率强制换位"""
        pos = random.randrange(9)
        if pos == self.last_mole and random.random() < 0.5:
            while pos == self.last_mole:
                pos = random.randrange(9)
        self.mole_idx = pos
        self.last_mole = pos
        self.mole_born = now
        self.mole_deadline = now + self.mole_stay

    # ---------------- 主循环 ----------------
    def tick(self):
        now = time.monotonic()

        if self.phase == "playing":
            left = GAME_DURATION_SEC - (now - self.start_ts)
            self.time_left = max(0.0, left)
            if left <= 0:
                self.set_msg("时间到！本局结束", C_TEXT)
                self.game_over()
            else:
                if self.mole_idx >= 0 and now >= self.mole_deadline:
                    self.clear_mole()
                    self.lives -= 1
                    self.next_spawn = now + SPAWN_GAP_MS / 1000.0
                    self.set_msg("太慢了！地鼠跑了，生命 -1", C_RED)
                    if self.lives <= 0:
                        self.game_over()
                if self.phase == "playing" and self.mole_idx < 0 and now >= self.next_spawn:
                    self.spawn_mole(now)

        self.redraw_dynamic(now)
        self.root.after(16, self.tick)

    def redraw_dynamic(self, now):
        cv = self.canvas
        # 分数 / 时间 / 生命
        cv.itemconfigure(self.t_score, text="分数  %d" % self.score)
        cv.itemconfigure(self.t_time, text="%d 秒" % int(round(self.time_left)))
        for i, hid in enumerate(self.heart_ids):
            cv.itemconfigure(hid, fill=C_HEART if i < self.lives else C_HEART_OFF)

        # 时间条
        ratio = max(0.0, min(1.0, self.time_left / GAME_DURATION_SEC))
        x1 = self.bar_x0 + (self.bar_x1 - self.bar_x0) * ratio
        cv.coords(self.bar_fill, self.bar_x0, self.bar_y, x1, self.bar_y + 10)
        cv.itemconfigure(self.bar_fill, fill=C_GREEN if ratio > 0.25 else "#D9822B")

        # 地鼠(带弹出缩放动画)
        if self.mole_idx >= 0:
            scale = min(1.0, (now - self.mole_born) / 0.12)
            self.draw_mole(self.mole_idx, scale)
        elif self.mole_items:
            self.clear_mole()

        # 飘字
        alive = []
        for item, t0, y0 in self.float_items:
            t = (now - t0) / 0.75
            if t >= 1:
                cv.delete(item)
                continue
            cv.coords(item, self.float_x[item], y0 - 46 * t)
            alive.append((item, t0, y0))
        self.float_items = alive

        # 打错红圈
        alive_f = []
        for item, t0 in self.flash_items:
            if now - t0 >= 0.35:
                cv.delete(item)
                continue
            alive_f.append((item, t0))
        self.flash_items = alive_f

    def draw_mole(self, idx, scale):
        cv = self.canvas
        for it in self.mole_items:
            cv.delete(it)
        self.mole_items = []

        x, y = self.cells[idx]
        cx, cy = x + CELL / 2, y + CELL / 2 + 20
        r = 46 * scale
        if r < 1:
            return

        # 耳朵
        er = r * 0.33
        for sx in (-0.62, 0.62):
            self.mole_items.append(cv.create_oval(
                cx + sx * r - er, cy - 0.62 * r - er,
                cx + sx * r + er, cy - 0.62 * r + er,
                fill=C_MOLE_DK, outline=""))

        # 身体
        self.mole_items.append(cv.create_oval(cx - r, cy - r * 0.95,
                                              cx + r, cy + r * 0.95,
                                              fill=C_MOLE, outline=""))
        # 头顶高光
        self.mole_items.append(cv.create_oval(cx - r * 0.55, cy - r * 0.72,
                                              cx + r * 0.55, cy - r * 0.1,
                                              fill=C_MOLE_LT, outline=""))
        # 口鼻区
        self.mole_items.append(cv.create_oval(cx - r * 0.5, cy + r * 0.1,
                                              cx + r * 0.5, cy + r * 0.62,
                                              fill=C_SNOUT, outline=""))
        # 眼睛
        ey = 5.5 * scale
        for sx in (-0.32, 0.32):
            self.mole_items.append(cv.create_oval(cx + sx * r - ey,
                                                  cy - r * 0.22 - ey * 1.15,
                                                  cx + sx * r + ey,
                                                  cy - r * 0.22 + ey * 1.15,
                                                  fill="#241710", outline=""))
        # 鼻子
        self.mole_items.append(cv.create_oval(cx - 6 * scale, cy + r * 0.16,
                                              cx + 6 * scale, cy + r * 0.28,
                                              fill=C_NOSE, outline=""))
        # 胡须
        for dy in (-1, 0, 1):
            ys = cy + r * 0.2 + dy * 5 * scale
            self.mole_items.append(cv.create_line(
                cx + r * 0.34, ys, cx + r * 0.9, ys - dy * 4 * scale - 6 * scale,
                fill="#6B4A2E", width=1))
            self.mole_items.append(cv.create_line(
                cx - r * 0.34, ys, cx - r * 0.9, ys - dy * 4 * scale - 6 * scale,
                fill="#6B4A2E", width=1))

    def clear_mole(self):
        for it in self.mole_items:
            self.canvas.delete(it)
        self.mole_items = []
        self.mole_idx = -1

    # ---------------- 交互 ----------------
    def whack(self, idx):
        if self.phase == "over":
            self.restart()
            return
        now = time.monotonic()
        self.swings += 1
        cx = self.cells[idx][0] + CELL / 2
        cy = self.cells[idx][1] + CELL / 2

        if idx == self.mole_idx:
            self.hits += 1
            self.score += HIT_SCORE
            self.clear_mole()
            self.next_spawn = now + SPAWN_GAP_MS / 1000.0
            if self.mole_stay > (MOLE_MIN_MS + MOLE_SPEEDUP_MS) / 1000.0:
                self.mole_stay -= MOLE_SPEEDUP_MS / 1000.0
            self.set_msg("漂亮！击中 %d 号格子，+%d 分（地鼠变快了）" % (idx + 1, HIT_SCORE), C_GREEN)
            self.add_float("+%d" % HIT_SCORE, cx, cy, C_GREEN)
        else:
            self.lives -= 1
            self.score = max(0, self.score - MISS_PENALTY)
            self.add_float("-%d" % MISS_PENALTY, cx, cy, C_RED)
            self.add_flash(idx)
            self.set_msg("打错格子 %d！-%d 分，生命 -1" % (idx + 1, MISS_PENALTY), C_RED)
            if self.lives <= 0:
                self.game_over()

    def add_float(self, text, cx, cy, color):
        item = self.canvas.create_text(cx, cy - 10, text=text,
                                       font=(FONT, 15, "bold"), fill=color)
        if not hasattr(self, "float_x"):
            self.float_x = {}
        self.float_x[item] = cx
        self.float_items.append((item, time.monotonic(), cy - 10))

    def add_flash(self, idx):
        x, y = self.cells[idx]
        item = self.canvas.create_oval(x + 6, y + 6, x + CELL - 6, y + CELL - 6,
                                       outline=C_RED, width=5)
        self.flash_items.append((item, time.monotonic()))

    def on_click(self, ev):
        if self.phase == "over":
            self.restart()
            return
        for i, (x, y) in enumerate(self.cells):
            if x <= ev.x <= x + CELL and y <= ev.y <= y + CELL:
                self.whack(i)
                return

    def on_key(self, ev):
        ch = ev.char
        if ch in "123456789":
            self.whack(int(ch) - 1)
        elif ch in ("r", "R"):
            self.restart()
        elif ev.keysym in ("Escape", "q", "Q"):
            self.close()

    # ---------------- 结束 / 重开 ----------------
    def game_over(self):
        self.phase = "over"
        self.clear_mole()
        self.set_msg("", C_TEXT)
        cv = self.canvas
        acc = ("命中率 %.0f%%" % (100.0 * self.hits / self.swings)) if self.swings else "命中率 --"

        self.overlay = []
        # 阴影 + 面板
        self.overlay.append(cv.create_rectangle(GRID_X0 + 8, 258, W - GRID_X0 + 8, 520,
                                                fill="#C9B79C", outline=""))
        x0, x1 = GRID_X0, W - GRID_X0
        self.overlay.append(cv.create_rectangle(x0, 250, x1, 512, fill=C_CARD,
                                                outline=C_BORDER, width=3))
        self.overlay.append(cv.create_text(W / 2, 292, text="游 戏 结 束",
                                           font=(FONT, 20, "bold"), fill=C_TEXT))
        self.overlay.append(cv.create_text(W / 2, 350, text="%d 分" % self.score,
                                           font=(FONT, 32, "bold"), fill=C_RED))
        self.overlay.append(cv.create_text(
            W / 2, 400, text="击中 %d 只 / 出手 %d 次      %s" % (self.hits, self.swings, acc),
            font=(FONT, 11), fill=C_SUBTLE))
        self.overlay.append(cv.create_text(W / 2, 452, text="按 R 或点击任意位置 → 再来一局",
                                           font=(FONT, 12, "bold"), fill=C_GREEN))
        self.overlay.append(cv.create_text(W / 2, 484, text="Esc 退出游戏",
                                           font=(FONT, 9), fill=C_SUBTLE))
        self.overlay_visible = True

    def hide_overlay(self):
        for it in self.overlay:
            self.canvas.delete(it)
        self.overlay = []
        self.overlay_visible = False

    def restart(self):
        self.reset_game()

    def close(self):
        self.root.destroy()


def main():
    random.seed()
    root = tk.Tk()
    WhackAMole(root)
    root.update_idletasks()
    # 窗口居中
    sw, sh = root.winfo_screenwidth(), root.winfo_screenheight()
    root.geometry("+%d+%d" % ((sw - W) // 2, max(0, (sh - H) // 2 - 20)))
    root.focus_force()
    root.mainloop()


if __name__ == "__main__":
    main()
