#INCLUDE_DIR := 
EXEC_DIR := exec/
SRC_DIR := src/
CC = gcc #Flag for implicit rules
CFLAGS = -g #Flag for implicit rules
CLIBS = wayland-client

all: window test

window:
	mkdir -p $(EXEC_DIR)
	$(CC) $(CFLAGS) $(SRC_DIR)main.cpp $(SRC_DIR)xdg-shell-protocol.c -l$(CLIBS) -o $(EXEC_DIR)window

test:
	./$(EXEC_DIR)window

clean:
	rm -rf $(EXEC_DIR)  
