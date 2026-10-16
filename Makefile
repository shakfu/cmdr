CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic

.PHONY: all clean
all: cmdr

cmdr: main.o cmdr.o commands.o

main.o cmdr.o commands.o: cmdr.h
main.o commands.o: commands.h

clean:
	rm -f cmdr main.o cmdr.o commands.o
