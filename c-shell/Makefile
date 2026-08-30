CC = gcc

CFLAGS = -std=c23 \
	-D_POSIX_C_SOURCE=200809L \
	-D_XOPEN_SOURCE=700 \
	-Wall \
	-Wextra \
	-Werror \
	-Wno-unused-parameter \
	-fno-asm \
	-Iinclude

TARGET = shell.out

SOURCES = \
	src/main.c \
	src/lexer.c \
	src/parser.c \
	src/prompt.c

OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(TARGET)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

