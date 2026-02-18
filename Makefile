CFLAGS = -Wall -Wextra -std=c11 -fsanitize=address -g -Iinclude -I.
LDFLAGS = -lcurl -lprotobuf-c -lsqlite3

# Directories
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
PROTO_DIR = proto

TARGET = $(BIN_DIR)/telemetrix

# Source Files
SRCS = $(wildcard $(SRC_DIR)/*.c) $(PROTO_DIR)/gtfs-realtime.pb-c.c
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

all: directories $(TARGET)

$(TARGET): $(OBJS)
	gcc $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	gcc $(CFLAGS) -c $< -o $@

directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean directories