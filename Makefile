# Default release build:
# make
#
# Debug build:
# make mode=debug
#
# To enable validation layers use:
# make CPPFLAGS="-DVALIDATION_LAYERS"

EXEC = vulkan_hourglass
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Werror -Wextra -Wconversion -pedantic

PREFIX = $(HOME)/.local

ifeq ($(mode), debug)
	MODE_FLAGS = -O0 -g
	BIN = ./bin/debug
	BUILD = ./build/debug
else
	MODE_FLAGS = -O3 -D_FORTIFY_SOURCE=2
	BIN = ./bin/release
	BUILD = ./build/release
endif

SRCPATH = ./src
INC = -I./src
LIB =
LIBS = -lglfw -lvulkan

SRCMAIN = ./src/main.cpp
SRCFILES = ./src/FileReading.cpp ./src/Grid.cpp ./src/GlfwContext.cpp ./src/SpecializationConstants.cpp ./src/RuntimeStatistics.cpp ./src/ComputeUpdateTimer.cpp ./src/VulkanContext.cpp
OBJFILES := $(patsubst $(SRCPATH)/%.cpp,$(BUILD)/%.o,$(SRCFILES))

COMP_SHADER = ./shaders/shader.comp
FRAG_SHADER = ./shaders/shader.frag
VERT_SHADER = ./shaders/shader.vert

.PHONY: all
all: $(EXEC)

$(EXEC): $(SRCMAIN) $(OBJFILES)
	mkdir -p $(BIN)
	mkdir -p $(BUILD)
	glslc $(VERT_SHADER) -o $(BIN)/vert.spv
	glslc $(FRAG_SHADER) -o $(BIN)/frag.spv
	glslc $(COMP_SHADER) -o $(BIN)/comp.spv
	$(CXX) $(CPPFLAGS) $(MODE_FLAGS) $(CXXFLAGS) $(INC) -o $(BIN)/$(EXEC) $(SRCMAIN) $(OBJFILES) $(LIB) $(LIBS)

$(BUILD)/%.o: $(SRCPATH)/%.cpp
	mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(MODE_FLAGS) $(CXXFLAGS) $(INC) -MMD -o $@ -c $<

-include $(BUILD)/*.d

.PHONY: install
install: $(EXEC)
	cp -f $(BIN)/$(EXEC) $(PREFIX)/bin

.PHONY: clean
clean:
	rm -rf ./bin/ ./build/
