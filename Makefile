# Compiler and flags
CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -Iinclude

# Source directories
SRC_DIRS := src src/sensors src/outputs src/telemetry src/web
SRC := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.cpp))

# Object files (keep same folder structure)
OBJ := $(SRC:.cpp=.o)

TARGET := firelink
LIBS := -lm -lgpiod -lpthread

# Platform-independent sources that also build on macOS/Linux for host tests.
HOST_SRC := src/config.cpp src/state.cpp src/sensors/nmea.cpp src/telemetry/packet.cpp src/calib.cpp
TEST_SRC := $(wildcard tests/*.cpp)
TEST_BIN := tests/run_tests

# Bench sweep tool for the SX1272 flight radio (needs Pi SPI0 + libgpiod).
SWEEP_BIN := tools/lora_sweep
SWEEP_SRC := tools/lora_sweep.cpp src/telemetry/lora_sx1272.cpp src/telemetry/packet.cpp

all: $(TARGET) sweep

# Link
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $@ $(LIBS)

# Compile each .cpp into .o
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Host-side unit tests (no Pi hardware required).
test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) $(HOST_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lpthread

# Bench sweep tool.
sweep: $(SWEEP_BIN)

$(SWEEP_BIN): $(SWEEP_SRC) include/telemetry/lora_sx1272.h include/telemetry/radio.h
	$(CXX) $(CXXFLAGS) $(SWEEP_SRC) -o $@ $(LIBS)

# Regenerate the golden frame used by the Python codec test.
golden: $(TEST_BIN)
	./$(TEST_BIN) --emit-golden groundstation/tests/golden_frame.hex

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_BIN) $(SWEEP_BIN)

.PHONY: all test golden sweep clean
