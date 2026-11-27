# 终端俄罗斯方块（C 语言 / ANSI 终端）

纯 C11 + ANSI 转义序列实现的终端俄罗斯方块。**只用标准库，不需要 ncurses**，编译即用。  
Linux / macOS 为原生目标，Windows（MinGW 或 MSVC）也能编译运行，方便在本地调试。

## 1. 编译与运行

```bash
make            # 编译，生成 ./tetris
./tetris        # 运行
```

不用 make 也行：

```bash
cc -O2 -std=c11 -Wall -Wextra -o tetris tetris.c
./tetris
```

macOS 的 `cc` 就是 clang，命令一致。想换编译器：

```bash
make CC=clang
```

边框显示错位（少数终端把制表符当双宽字符）时，改用纯 ASCII 边框：

```bash
make ASCII=1
```

## 2. 操作

| 按键        | 功能                 |
| --------- | ------------------ |
| `←` / `A` | 左移                 |
| `→` / `D` | 右移                 |
| `↓` / `S` | 软降（每格 +1 分）        |
| `↑` / `W` | 顺时针旋转              |
| `空格`      | 硬降（每格 +2 分，落地立即固定） |
| `P`       | 暂停 / 继续            |
| `R`       | 重开一局               |
| `Q`       | 退出                 |
| `Ctrl-C`  | 强制退出（终端状态会自动恢复）    |

## 3. 规则

- 棋盘 10 × 20，7 种标准方块（I O T J L S Z），**7-bag 随机**：每 7 个方块为一轮洗牌，不会长时间不出某一种。
- 消行得分（再乘以当前等级）：1 行 100、2 行 300、3 行 500、4 行 800。
- 每消 10 行升 1 级，下落间隔 `0.75s - 0.06s ×(等级-1)`，最快 60ms 一格。
- 方块出生态被占住即游戏结束，此时按 `R` 重开、`Q` 退出。
- 棋盘上会显示灰色「幽灵落点」，提示当前方块会落在哪里。
- 旋转带简易 wall kick：原地转不动就左右各试 1~2 格，贴地时再试上移一格。

## 4. 环境要求

- 终端尺寸至少 **80 × 24**（界面占 24 行、约 56 列）。
- 需要 UTF-8 locale，否则中文/边框会乱码：
  ```bash
  echo $LANG          # 期望形如 zh_CN.UTF-8 / en_US.UTF-8
  export LANG=C.UTF-8 # 不是的话临时设一下
  ```
- 支持 256 色（`echo $TERM` 一般为 `xterm-256color`）；不支持时方块会退化成默认底色，但游戏仍可玩。

## 5. 关于 ncurses

本实现**没有用到 ncurses**：键盘用 `termios`（关掉行缓冲与回显）+ `select` 超时做非阻塞读取，  
画面用 ANSI 转义序列一次性输出整帧。这样零依赖、编译完就能跑。

如果你后续想改成 ncurses 版本，需要先装开发库：

| 系统              | 安装命令                                                |
| --------------- | --------------------------------------------------- |
| Debian / Ubuntu | `sudo apt install libncurses5-dev libncursesw5-dev` |
| Fedora / RHEL   | `sudo dnf install ncurses-devel`                    |
| Arch            | `sudo pacman -S ncurses`                            |
| macOS           | 系统自带，无需安装（Homebrew: `brew install ncurses`）         |

编译时额外加链接参数：`cc -O2 -std=c11 tetris.c -lncurses -o tetris`。

## 6. 代码结构（tetris.c）

| 区域    | 说明                                                              |
| ----- | --------------------------------------------------------------- |
| 常量与配色 | 棋盘尺寸、ANSI 256 色色板、边框字符（可切 ASCII）                                |
| 平台层   | `termios`/`select`（POSIX）或 `_kbhit`（Windows）实现非阻塞按键             |
| 形状表   | `SPAWN` 只写出生态，`rotate_cw()` 自动推导其余 3 个旋转态                       |
| 逻辑    | `collide` 碰撞检测、`try_move` 移动、`clear_lines` 消行、`lock_piece` 固定结算 |
| 渲染    | `render()` 拼一整帧到缓冲区，`fwrite` 一次输出，避免闪烁                          |
| 主循环   | 按「下一次下落时刻」计算等待时间，既省 CPU 又不卡按键                                   |

关键逻辑均有中文注释，尤其是消行的整行下移、旋转的 4x4 位掩码推导，以及主循环的计时方式。
