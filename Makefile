EMCC      := emcc
RAYLIB    := raylib-5.5_webassembly

INCLUDE   := -I$(RAYLIB)/include \
             -Ivendor             \
             -Isrc                \
             -Iprototypes

LDFLAGS   := $(RAYLIB)/lib/libraylib.a \
             -s USE_GLFW=3             \
             -s ASYNCIFY               \
             -s TOTAL_MEMORY=67108864  \
             --shell-file src/shell.html

GIT_HASH  := $(shell git rev-parse --short HEAD 2>/dev/null || echo dev)
CFLAGS    := -DPLATFORM_WEB -O2 -DGIT_VERSION='"$(GIT_HASH)"'

# Core sources (always compiled)
CORE_SRCS := src/main.c src/menu.c src/raygui_impl.c

# Auto-discover all prototype sources
PROTO_SRCS := $(wildcard prototypes/*/*.c)

SRCS := $(CORE_SRCS) $(PROTO_SRCS)

.PHONY: all clean list-protos test-orbfall test-orbfall-smoke

all: dist/index.html

dist/index.html: $(SRCS) vendor/raygui.h src/shell.html | dist
	$(EMCC) $(SRCS) $(INCLUDE) $(CFLAGS) -o $@ $(LDFLAGS)

dist:
	mkdir -p dist

clean:
	rm -rf dist

# Native unit tests for the Orbfall resolver and damage pipeline
test-orbfall:
	mkdir -p build-tests
	cc -std=c99 -Wall prototypes/orbfall/resolve.c \
	   prototypes/orbfall/tests/test_resolve.c -o build-tests/test_resolve
	cc -std=c99 -Wall -Wno-missing-field-initializers \
	   prototypes/orbfall/resolve.c prototypes/orbfall/content.c \
	   prototypes/orbfall/tests/test_content.c -o build-tests/test_content -lm
	./build-tests/test_resolve
	./build-tests/test_content

# Headless whole-game smoke test (needs raylib headers, see CI/local setup)
test-orbfall-smoke:
	mkdir -p build-tests
	cc -std=c99 -g -Wall -Wno-missing-field-initializers \
	   -I$(RAYLIB)/include -Ivendor -Isrc -Iprototypes \
	   prototypes/orbfall/orbfall.c prototypes/orbfall/resolve.c \
	   prototypes/orbfall/content.c prototypes/orbfall/tests/stub_raylib.c \
	   prototypes/orbfall/tests/smoke_test.c -o build-tests/smoke -lm
	./build-tests/smoke

# Convenience target: list discovered prototype source files
list-protos:
	@echo "Discovered prototype sources:"
	@for f in $(PROTO_SRCS); do echo "  $$f"; done
