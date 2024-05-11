CC := clang
BUILDDIR := build
SRCDIR := src
TARGET := $(BUILDDIR)/main
MODE ?= debug

SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

ifeq ($(MODE), release)
	CFLAGS := -Wall -Wextra -O2
else
	CFLAGS := -Wall -Wextra -g -O0
endif

LDFLAGS :=

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
