CC := gcc

CFLAGS := -Wall -Wextra -pedantic -Wno-unused-parameter
LDLIBS := -lm

SRC := $(wildcard src/*.c)
NAME := solace

BUILD := build
BIN := bin

NORMAL_OBJ := $(SRC:src/%.c=$(BUILD)/normal/%.o)
DEBUG_OBJ  := $(SRC:src/%.c=$(BUILD)/debug/%.o)
PROF_OBJ   := $(SRC:src/%.c=$(BUILD)/prof/%.o)

# ------------------------------------------------------------------------------
# Executables
# ------------------------------------------------------------------------------

$(BIN)/$(NAME): $(NORMAL_OBJ)
	@mkdir -p $(@D)
	$(CC) -O2 -o $@ $^ $(LDLIBS)

$(BIN)/$(NAME)_dbg: $(DEBUG_OBJ)
	@mkdir -p $(@D)
	$(CC) -g -O0 -o $@ $^ $(LDLIBS)

$(BIN)/$(NAME)_prof: $(PROF_OBJ)
	@mkdir -p $(@D)
	$(CC) -O2 -pg -o $@ $^ $(LDLIBS)

# ------------------------------------------------------------------------------
# Object files
# ------------------------------------------------------------------------------

$(BUILD)/normal/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -O2 -MMD -MP -c $< -o $@

$(BUILD)/debug/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DSLC_DEBUG -g -O0 -MMD -MP -c $< -o $@

$(BUILD)/prof/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -O2 -pg -MMD -MP -c $< -o $@

# Automatically include GCC's generated header dependencies.
-include $(NORMAL_OBJ:.o=.d)
-include $(DEBUG_OBJ:.o=.d)
-include $(PROF_OBJ:.o=.d)

# ------------------------------------------------------------------------------
# Convenience targets
# ------------------------------------------------------------------------------

.PHONY: run
run: $(BIN)/$(NAME)
	$(BIN)/$(NAME)

.PHONY: debug
debug: $(BIN)/$(NAME)_dbg
	$(BIN)/$(NAME)_dbg

.PHONY: prof
prof: $(BIN)/$(NAME)_prof profile.slc
	$(BIN)/$(NAME)_prof profile.slc
	gprof $(BIN)/$(NAME)_prof gmon.out -bp

.PHONY: test
test: $(BIN)/$(NAME)
	./tests/run.sh

.PHONY: all
all: $(BIN)/$(NAME) $(BIN)/$(NAME)_dbg $(BIN)/$(NAME)_prof

.PHONY: clean
clean:
	rm -rf $(BUILD) $(BIN) gmon.out
