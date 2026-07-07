CC = gcc
CFLAGS = -Wall -Wextra -Werror -O2 -g

TARGET = server
SRC = src/server.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean