# Compiler and flags
CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -Iinclude

# Source directories
SENSOR_SRC := src/sensors
OUTPUT_SRC := src/outputs
SRC := src/main.cpp $(SENSOR_SRC)/*.cpp $(OUTPUT_SRC)/*.cpp

# Output
TARGET := firelink

# Math library
LIBS := -lm

# Default target
all: $(TARGET)

# Build executable
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LIBS)

# Clean object files / executable
clean:
	rm -f $(TARGET)
