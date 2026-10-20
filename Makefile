RACK_DIR ?= ../..

SOURCES += $(wildcard src/*.cpp src/*/*.cpp src/*/*/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/plugin.mk

# Rack's compile.mk asks for C++11; Tessera needs C++17. The later flag wins.
CXXFLAGS += -std=c++17

# Unit tests: the model and engine build without Rack, so tests are a plain native binary.
TEST_CXX ?= c++
TEST_FLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -pthread -fsanitize=address,undefined
# The model uses jansson (Rack's JSON library); tests link a system copy (Arch: jansson, Debian: libjansson-dev,
# Homebrew: jansson).
TEST_LIBS ?= $(shell pkg-config --cflags --libs jansson 2>/dev/null || echo -ljansson)
# The UI's pure parts (axes, keymap, editor state) are tested too. They only borrow GLFW's key codes from Rack.
TEST_SOURCES := $(wildcard tests/*.cpp src/model/*.cpp src/engine/*.cpp) \
	src/ui/interaction/Axes.cpp src/ui/interaction/Shortcuts.cpp src/ui/interaction/Snap.cpp \
	src/ui/editing/NoteHitTest.cpp src/ui/interaction/TrackRows.cpp src/ui/EditorState.cpp \
	src/ui/interaction/Search.cpp
TEST_HEADERS := $(wildcard tests/*.h tests/*.hpp src/model/*.hpp src/engine/*.hpp) \
	src/ui/interaction/Axes.hpp src/ui/interaction/Navigation.hpp src/ui/interaction/Shortcuts.hpp \
	src/ui/interaction/Snap.hpp src/ui/editing/NoteHitTest.hpp src/ui/interaction/TrackRows.hpp \
	src/ui/EditorState.hpp \
	src/module/OutputBus.hpp src/module/AudioHandoff.hpp src/module/PasteRemaps.hpp
TEST_BIN := build/tests/tessera_tests

test: $(TEST_BIN)
	$(TEST_BIN)

# Expose only Rack's GLFW headers, so the tests' jansson header matches the system library they link.
TEST_INCLUDE := build/tests/include

# One object per source, so `make -j test` compiles on every core and an edit only recompiles what it touches
# (each object lists the headers it includes in a .d file).
TEST_OBJ_DIR := build/tests/obj
TEST_OBJECTS := $(patsubst %.cpp,$(TEST_OBJ_DIR)/%.o,$(TEST_SOURCES))

$(TEST_INCLUDE)/GLFW:
	@mkdir -p $(TEST_INCLUDE)
	@ln -sfn $(abspath $(RACK_DIR))/dep/include/GLFW $(TEST_INCLUDE)/GLFW

$(TEST_OBJ_DIR)/%.o: %.cpp | $(TEST_INCLUDE)/GLFW
	@mkdir -p $(dir $@)
	$(TEST_CXX) $(TEST_FLAGS) -MMD -MP -isystem $(TEST_INCLUDE) $(shell pkg-config --cflags jansson 2>/dev/null) \
		-c -o $@ $<

$(TEST_BIN): $(TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(TEST_CXX) $(TEST_FLAGS) -o $@ $(TEST_OBJECTS) $(TEST_LIBS)

-include $(TEST_OBJECTS:.o=.d)

.PHONY: test
