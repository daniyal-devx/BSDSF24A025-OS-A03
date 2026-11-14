CC      := gcc
CFLAGS  := -Wall -Wextra -g -Iinclude
LDFLAGS :=

SRCDIR  := src
INCDIR  := include
OBJDIR  := obj
BINDIR  := bin

TARGET  := $(BINDIR)/myshell
SRCS    := $(SRCDIR)/main.c $(SRCDIR)/shell.c $(SRCDIR)/execute.c
OBJS    := $(OBJDIR)/main.o $(OBJDIR)/shell.o $(OBJDIR)/execute.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/shell.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR) $(OBJDIR):
	mkdir -p $@

clean:
	rm -rf $(OBJDIR) $(BINDIR)
