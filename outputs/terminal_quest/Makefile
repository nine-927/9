# 《秘境三关》Terminal Quest - Makefile
# 用法:
#   make            # 编译 (Linux/macOS/MinGW)
#   make run        # 编译并运行
#   make clean      # 清理编译产物

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
# 第 3 关的 3D 渲染用到 math.h (cosf/sinf/fabsf/fmodf)，Linux 需显式链接 libm
LDLIBS  ?= -lm
TARGET   = game
SRC      = game.c

# Windows (MinGW) 下输出带 .exe 后缀，math 库已包含在运行时中
ifeq ($(OS),Windows_NT)
    TARGET := game.exe
    LDLIBS :=
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f game game.exe

.PHONY: all run clean
