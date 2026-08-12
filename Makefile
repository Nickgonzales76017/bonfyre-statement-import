CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
TARGET = bonfyre-statement-import

all: $(TARGET)

$(TARGET): src/main.c
	$(CC) $(CFLAGS) -o $@ $<

test: $(TARGET)
	bash tests/run.sh

clean:
	rm -f $(TARGET)

.PHONY: all test clean
