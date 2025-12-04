# Makefile for WSPR-ease FPGA Simulation
# Integrates Verilator with ESP32 SW code

# Directories
FPGA_DIR := .
SW_DIR := ../sw
SIM_DIR := sim
OBJ_DIR := obj_dir

# Verilator configuration
VERILATOR := verilator
VERILATOR_FLAGS := -Wall -Wno-fatal --trace --cc --exe --build
VERILATOR_FLAGS += -I$(SIM_DIR)
VERILATOR_FLAGS += -CFLAGS "-std=c++17 -I$(SW_DIR)"
VERILATOR_FLAGS += -LDFLAGS "-lm"

# Source files
RTL_SOURCES := top.sv sequencer.sv
RTL_SOURCES += $(SIM_DIR)/SB_IO.sv $(SIM_DIR)/SB_PLL40_CORE.sv

CPP_SOURCES := tb_top.cpp
SW_SOURCES := $(SW_DIR)/transmitter.cpp $(SW_DIR)/wspr-encoder.cpp

# Output executable
TARGET := $(OBJ_DIR)/VTop

.PHONY: all clean run sim fast help

all: $(TARGET)

# Build the Verilator simulation
$(TARGET): $(RTL_SOURCES) $(CPP_SOURCES) $(SW_SOURCES) sim_hal.hpp
	@echo "Building Verilator simulation with integrated SW code..."
	$(VERILATOR) $(VERILATOR_FLAGS) \
		-o $(notdir $(TARGET)) \
		--top-module Top \
		$(RTL_SOURCES) \
		$(CPP_SOURCES) \
		$(SW_SOURCES)
	@echo "Build complete: $@"

# Run the simulation
run: $(TARGET)
	@echo "Running simulation..."
	@cd $(OBJ_DIR) && ./VTop
	@echo "Simulation complete. Waveform saved to $(OBJ_DIR)/waveform.vcd"

# Alias for run
sim: run

# Run fast simulation (no waveforms)
fast: $(TARGET)
	@echo "Running fast simulation (no waveform trace)..."
	@cd $(OBJ_DIR) && ./VTop --notrace
	@echo "Fast simulation complete."

# View waveform with gtkwave (if available)
wave: $(OBJ_DIR)/waveform.vcd
	@if command -v gtkwave > /dev/null; then \
		gtkwave $< & \
	else \
		echo "gtkwave not found. Please install it to view waveforms."; \
	fi

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(OBJ_DIR)
	@rm -f waveform.vcd
	@echo "Clean complete."

# Help target
help:
	@echo "WSPR-ease FPGA Simulation Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build the simulation (default)"
	@echo "  run     - Build and run simulation with waveform tracing"
	@echo "  fast    - Build and run fast simulation without waveforms (7x faster)"
	@echo "  sim     - Alias for 'run'"
	@echo "  wave    - Open waveform viewer (gtkwave)"
	@echo "  clean   - Remove all build artifacts"
	@echo "  help    - Show this help message"
	@echo ""
	@echo "The simulation integrates:"
	@echo "  - FPGA RTL (top.sv, sequencer.sv)"
	@echo "  - ESP32 SW code (transmitter.cpp, wspr-encoder.cpp)"
	@echo "  - Simulation HAL (sim_hal.hpp)"
