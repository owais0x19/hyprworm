# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
LDFLAGS = -lcjson

# Target executable name
TARGET = hyprworm

# Automatically find all.c files in the current directory
SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

# Default rule: build the executable
all: $(TARGET)

# Linking rule: create the final executable from the object files
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Compilation rule: compile.c files into.o files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean rule: remove generated files
clean:
	rm -f $(OBJS) $(TARGET)

# Install rule: copy the compiled binary to a system-wide location
install: all
	install -Dm755 $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)

# Uninstall rule
uninstall:
	rm -f /usr/local/bin/$(TARGET)