# 终端俄罗斯方块 —— 纯标准库 + ANSI 转义序列，无需 ncurses
#
# 用法：
#   make            编译（默认 UTF-8 制表符边框）
#   make ASCII=1    编译（纯 ASCII 边框，适用于把制表符显示成双宽的终端/字体）
#   make run        编译并运行
#   make clean      清理产物

CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -pedantic
LDFLAGS ?=
TARGET   = tetris

# 可选：make ASCII=1 时启用 ASCII 边框
ifdef ASCII
CFLAGS += -DASCII_BORDER
endif

all: $(TARGET)

$(TARGET): tetris.c
	$(CC) $(CFLAGS) -o $@ tetris.c $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe *.o

.PHONY: all run clean
