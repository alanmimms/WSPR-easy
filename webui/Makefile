# Makefile for WSPR-ease Web UI Mock Server

CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -O2 -Isrc -I/usr/include
LDFLAGS := -pthread

# Source files
SOURCES := src/main.cpp
TARGET := wspr_webui_server

# Directories
DATA_DIR := www-test-data
WWW_DIR := $(DATA_DIR)/www

# Version hash computed from www source files
VERSION_HASH := $(shell cat www/*.html www/*.css www/*.js 2>/dev/null | md5sum | cut -c1-8)

.PHONY: all clean run setup test version

all: setup $(TARGET)

$(TARGET): $(SOURCES) src/version.hpp
	@echo "Building web UI server..."
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

src/version.hpp: www/*.html www/*.css www/*.js
	@echo "Generating version hash: $(VERSION_HASH)"
	@echo '#pragma once' > src/version.hpp
	@echo '#define WEBUI_VERSION "$(VERSION_HASH)"' >> src/version.hpp

setup: src/version.hpp
	@echo "Setting up directories..."
	@mkdir -p $(DATA_DIR)
	@mkdir -p $(WWW_DIR)
	@cp -r www/* $(WWW_DIR)/
	@echo "Injecting version $(VERSION_HASH) into app.js..."
	@sed -i "s/const APP_VERSION = '[^']*'/const APP_VERSION = '$(VERSION_HASH)'/" $(WWW_DIR)/app.js
	@if [ ! -f src/httplib.h ]; then \
		echo "Downloading cpp-httplib..."; \
		wget -q https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h -O src/httplib.h; \
	fi

version:
	@echo "Current version hash: $(VERSION_HASH)"

run: $(TARGET)
	@echo "Starting web UI server..."
	@./$(TARGET)

clean:
	@echo "Cleaning build artifacts..."
	@rm -f $(TARGET)
	@echo "Clean complete."

clean-all: clean
	@echo "Removing data directory..."
	@rm -rf $(DATA_DIR)

test:
	@echo "Running API tests..."
	@bash test/test_api.sh

help:
	@echo "WSPR-ease Web UI Mock Server Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build the server (default)"
	@echo "  run       - Build and run the server"
	@echo "  setup     - Create directories and download dependencies"
	@echo "  clean     - Remove build artifacts"
	@echo "  clean-all - Remove build artifacts and data"
	@echo "  test      - Run API tests"
	@echo "  help      - Show this message"
