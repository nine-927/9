# step6_lock.c · 触底固化（Lock 机制）

## 文件职责
引入场地数组 `board[HEIGHT][WIDTH]`，让"掉不下去"的方块固化成场地的一部分，从此方块不再是可以滑走的画影。

## 主要函数
| 函数 | 职责 |
|---|---|
| `draw_field()` | 遍历 `board`，把所有已固化格子画成 `[]`，仅在固化后调用 |
| `lock_piece(gx, gy)` | 把当前方块每格写进 `board`，随后调用 `draw_field()` |
| `can_place(gx, gy)` | 仅做撞墙检测（本步还认识不到堆） |
| `paint / gotoxy / hide_cursor / draw_wall` | 与 Step 5 相同的绘制基础件 |

## 关键逻辑与数据流
```
自动下落时 can_place(x, y+1) 失败
        └─> lock_piece(x, y) ─> board 写 1 ─> draw_field() 重画
```
已知遗留问题（Step 7/8 解决）：固化后方块仍停在原地可被按键滑动；第二块会穿透第一块。

## 与其他文件的依赖关系
- 上游：Step 5（重力下落主循环结构）。
- 下游：Step 7 在 `can_place` 里读取本文件引入的 `board`。

## 注意事项
- 本步故意不检查 `board`，用于单独演示"固化"这一个概念。
- 触发条件只有一个：`can_place(x, y+1)` 失败。
