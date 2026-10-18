# Edith – a text editor for the terminal
#
#   tools/fetch-grammars.sh   once: download tree-sitter and the grammars into third_party/
#   make                      build the binary ./edith
#   make STATIC=1             build a statically linked binary (as for the releases)
#   make test                 build and run the tests (tests/)
#   make install              install binary and manual page into $(DESTDIR)$(PREFIX) (PREFIX=~/.local)
#   make uninstall            remove the installed files
#   make dist                 source tarball including third_party/ (builds without network)
#   make clean                remove the build output
#   make help                 show this list

CC       ?= cc
CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra
TSFLAGS  ?= -O2 -w
# CPPFLAGS (e.g. -D_FORTIFY_SOURCE=2 from a distribution's package build) is used for src/ and tests/
# always used, also when a package build sets its own CXXFLAGS
CXXSTD   := -std=c++17
CSTD     := -std=c11
PREFIX   ?= $(HOME)/.local
STATIC   ?= 0
VERSION  := $(shell sed -n 's/^\#define EDITH_VERSION "\(.*\)"/\1/p' src/version.h)

TP := third_party
B  := build
TS_INC := $(TP)/tree-sitter/lib/include

SECTIONS := -ffunction-sections -fdata-sections
UNAME_S  := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
# macOS: Apple's linker removes unused code with -dead_strip; there is no static libc
LDFLAGS  ?= -Wl,-dead_strip -Wl,-S
GC_FLAGS := -Wl,-dead_strip
TS_DEFS  := -D_DARWIN_C_SOURCE
ifeq ($(STATIC),1)
$(error STATIC=1 is not possible on macOS)
endif
else
LDFLAGS  ?= -Wl,--gc-sections -s
GC_FLAGS := -Wl,--gc-sections
TS_DEFS  :=
ifeq ($(STATIC),1)
LDFLAGS += -static
endif
endif

GRAMMAR_SRCS := $(shell find $(TP)/grammars \( -name parser.c -o -name scanner.c \) 2>/dev/null | sort)
GRAMMAR_OBJS := $(patsubst $(TP)/%.c,$(B)/%.o,$(GRAMMAR_SRCS))
QUERY_FILES  := $(wildcard $(TP)/queries/*/*.scm)

# Targets that work without the downloaded grammars
NO_GRAMMARS_GOALS := clean help uninstall version
ifeq ($(GRAMMAR_SRCS),)
ifneq ($(filter-out $(NO_GRAMMARS_GOALS),$(or $(MAKECMDGOALS),edith)),)
$(error No grammars found - run tools/fetch-grammars.sh first)
endif
endif

EDITH_SRCS := $(wildcard src/*.cpp)
EDITH_OBJS := $(patsubst src/%.cpp,$(B)/src/%.o,$(EDITH_SRCS))

.DEFAULT_GOAL := edith

# Relink when STATIC changes: the stamp file records how ./edith was linked
LINK_STAMP := $(B)/linked-static-$(STATIC)
$(LINK_STAMP):
	@mkdir -p $(B)
	@rm -f $(B)/linked-static-*
	@touch $@

edith: $(EDITH_OBJS) $(B)/tree-sitter.o $(GRAMMAR_OBJS) $(LINK_STAMP)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.o,$^) $(LDFLAGS)

# Only treesitter.cpp needs the generated grammar/query registry
$(B)/src/treesitter.o: $(B)/ts_registry.inc

$(B)/src/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXSTD) $(CPPFLAGS) $(CXXFLAGS) $(SECTIONS) -MMD -MP -I$(TS_INC) -I$(B) -c $< -o $@

-include $(EDITH_OBJS:.o=.d)

$(B)/ts_registry.inc: tools/gen-registry.sh $(QUERY_FILES) $(GRAMMAR_SRCS)
	@mkdir -p $(@D)
	tools/gen-registry.sh $(TP) > $@.tmp && mv $@.tmp $@

$(B)/tree-sitter.o: $(TP)/tree-sitter/lib/src/lib.c
	@mkdir -p $(@D)
	$(CC) $(CSTD) -O2 $(SECTIONS) -D_POSIX_C_SOURCE=200112L -D_DEFAULT_SOURCE $(TS_DEFS) \
	    -I$(TP)/tree-sitter/lib/src -I$(TS_INC) -c $< -o $@

$(B)/%.o: $(TP)/%.c
	@mkdir -p $(@D)
	@echo "  CC  $<"
	@$(CC) $(CSTD) $(TSFLAGS) $(SECTIONS) -I$(<D) -c $< -o $@

# Tests: every object of src/ except main.o, linked with the tests in tests/
TEST_SRCS := $(wildcard tests/*.cpp)
TEST_OBJS := $(patsubst tests/%.cpp,$(B)/tests/%.o,$(TEST_SRCS))
LIB_OBJS  := $(filter-out $(B)/src/main.o,$(EDITH_OBJS))

$(B)/edith-tests: $(TEST_OBJS) $(LIB_OBJS) $(B)/tree-sitter.o $(GRAMMAR_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(GC_FLAGS)

$(B)/tests/%.o: tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXSTD) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -Isrc -I$(TS_INC) -I$(B) -c $< -o $@

-include $(TEST_OBJS:.o=.d)

test: $(B)/edith-tests edith
	$(B)/edith-tests
	python3 tests/pty_test.py ./edith

# Only the third-party objects (e.g. to warm a CI cache)
objs: $(B)/tree-sitter.o $(GRAMMAR_OBJS)

# (no "install -D": the BSD install of macOS does not have it)
install: edith
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/man/man1
	install -m755 edith $(DESTDIR)$(PREFIX)/bin/edith
	install -m644 docs/edith.1 $(DESTDIR)$(PREFIX)/share/man/man1/edith.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/edith $(DESTDIR)$(PREFIX)/share/man/man1/edith.1

# The source tarball of a release: the git tree plus the downloaded third_party/
DIST := edith-$(VERSION)
dist:
	@test -z "$$(git status --porcelain --untracked-files=no)" || \
	    { echo "dist: commit your changes first (the tarball is built from HEAD)"; exit 1; }
	@mkdir -p $(B)
	git archive --prefix=$(DIST)/ -o $(B)/$(DIST).tar HEAD
	tar --transform 's,^,$(DIST)/,' -rf $(B)/$(DIST).tar $(TP)
	gzip -9f $(B)/$(DIST).tar
	@echo "$(B)/$(DIST).tar.gz"

version:
	@echo $(VERSION)

clean:
	rm -rf $(B) edith

help:
	@sed -n 's/^#   //p' Makefile

.PHONY: test objs install uninstall dist version clean help
