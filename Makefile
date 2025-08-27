# Compiler and flags
CC = gcc
CFLAGS = -Wall -g -I. -DCDEX_PARSE_TO_JSON
LDFLAGS = -lm

# Source files
SRCS = $(wildcard *.c) cjson/cJSON.c

# Object files
OBJS = $(patsubst %.c,obj/%.o,$(SRCS))

# Executable name
TARGET = cdex_demo

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) -o $@ -c $< $(CFLAGS)

clean:
	rm -f $(TARGET)
	rm -rf obj
