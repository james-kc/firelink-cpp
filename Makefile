# Compiler and flags
CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -Iinclude

# Source directories
SENSOR_SRC := src/sensors
OUTPUT_SRC := src/outputs
SRC := src/main.cpp $(SENSOR_SRC)/*.cpp $(OUTPUT_SRC)/*.cpp

# Object files
OBJ := $(SRC:.cpp=.o)

# Target executable
TARGET := firelink

# Libraries
LIBS := -lm

# Default target
all: $(TARGET)

# Link executable from object files
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $(TARGET) $(LIBS)

# Compile .cpp into .o
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean object files and executable
clean:
	rm -f $(OBJ) $(TARGET)
