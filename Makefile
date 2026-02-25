CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude -I.
LDFLAGS = -lcurl -lprotobuf-c -lsqlite3 -pthread

# Directories
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
PROTO_DIR = proto

TARGET = $(BIN_DIR)/telemetrix

# Safely grab all .c files but explicitly ignore macOS hidden metadata files (._*)
SRC_FILES = $(filter-out $(SRC_DIR)/._%, $(wildcard $(SRC_DIR)/*.c))
PROTO_FILES = $(filter-out $(PROTO_DIR)/._%, $(wildcard $(PROTO_DIR)/*.c))

# Object Files (maps both directories into obj/)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC_FILES)) \
       $(patsubst $(PROTO_DIR)/%.c, $(OBJ_DIR)/%.o, $(PROTO_FILES))

all: directories $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

# Compile rule for src/ folder
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile rule for proto/ folder
$(OBJ_DIR)/%.o: $(PROTO_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean directories