# 用户态软路由 —— 构建脚本
CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -g -pthread
LDFLAGS ?=
TARGET  := router

SRCS := main.c get_interface.c arp_link.c ip_link.c \
        key_pthread.c arp_pthread.c ip_pthread.c
OBJS := $(SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all debug clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

debug: CFLAGS += -O0 -g -DDEBUG -fsanitize=address
debug: clean all

run: all
	sudo ./$(TARGET)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

-include $(DEPS)
