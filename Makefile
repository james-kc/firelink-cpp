# Compiler and flags
CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -Iinclude

# Source directories
SRC_DIRS := src src/sensors src/outputs
SRC := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.cpp))

# Object files (keep same folder structure)
OBJ := $(SRC:.cpp=.o)

TARGET := firelink
LIBS := -lm

all: $(TARGET)

# Link
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $@ $(LIBS)

# Compile each .cpp into .o
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
