# Compiler
CC = g++
# Compiler flags
CFLAGS = -Wall -g -pthread  # Added -pthread here for compilation with pthread support
# Include directories
INCLUDES = -I.
# Source files
SOURCES = proxy.cpp CacheManager.cpp
# Object files
OBJECTS = $(SOURCES:.cpp=.o)
# Executable target
TARGET = proxy_server

# The default rule
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TARGET) $(OBJECTS) -pthread  # Also added -pthread here for linking

# Rule to convert .cpp to .o
%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Rule to clean files
clean:
	rm -f $(TARGET) $(OBJECTS)

# Phony targets
.PHONY: all clean
