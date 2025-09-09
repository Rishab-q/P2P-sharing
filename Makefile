# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lpthread 

# Target executables
TARGETS = server client

# OS-specific installation directory
# This block automatically sets the PREFIX based on the operating system.
# The '?=' operator allows the user to override it from the command line, e.g., 'make install PREFIX=/opt'
ifeq ($(OS),Windows_NT)
	# Default for MSYS2/MinGW environments on Windows
	PREFIX ?= /usr
	INSTALL_CMD = cp
	UNINSTALL_CMD = rm -f
else
	UNAME_S := $(shell uname -s)
	# Default for Linux and macOS
	ifeq ($(UNAME_S),Linux)
		PREFIX ?= /usr/local
		INSTALL_CMD = sudo cp
		UNINSTALL_CMD = sudo rm -f
	endif
	ifeq ($(UNAME_S),Darwin)
		PREFIX ?= /usr/local
		INSTALL_CMD = sudo cp
		UNINSTALL_CMD = sudo rm -f
	endif
endif

# Default rule: builds all targets
all: $(TARGETS)

# Rule to build the server
server: server.o
	$(CC) $(CFLAGS) -o server server.o $(LDFLAGS)

# Rule to build the client
client: client.o
	$(CC) $(CFLAGS) -o client client.o $(LDFLAGS)

# Pattern rule to compile .c files into .o (object) files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to clean up build artifacts
clean:
	@echo "Cleaning up project..."
	@rm -f $(TARGETS) *.o

# Rule to install the binaries
install: all
	@echo "Installing binaries to $(PREFIX)/bin..."
	@mkdir -p $(PREFIX)/bin
	@$(INSTALL_CMD) $(TARGETS) $(PREFIX)/bin
	@echo "Installation complete."

# Rule to uninstall the binaries
uninstall:
	@echo "Removing binaries from $(PREFIX)/bin..."
	@$(UNINSTALL_CMD) $(PREFIX)/bin/server
	@$(UNINSTALL_CMD) $(PREFIX)/bin/client
	@echo "Uninstallation complete."

.PHONY: all clean install uninstall