# Compiler and flags
CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -Iinclude

# Source directories
SENSOR_SRC := src/sensors
OUTPUT_SRC := src/outputs
SRC := src/main.cpp $(SENSOR_SRC)/*.cpp $(OUTPUT_SRC)/*.cpp

# Generate object files for each source
OBJ := $(SRC:.cpp=.o)

# Target executable
TARGET := firelink

# Libraries
LIBS := -lm

# Default target
all: $(TARGET)

# Link executable from object files
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $@ $(LIBS)

# Compile each .cpp into its own .o
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean all object files and the executable
clean:
	rm -f $(OBJ) $(TARGET)
