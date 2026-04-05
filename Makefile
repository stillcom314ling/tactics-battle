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

.PHONY: all clean list-protos

all: dist/index.html

dist/index.html: $(SRCS) vendor/raygui.h src/shell.html | dist
	$(EMCC) $(SRCS) $(INCLUDE) $(CFLAGS) -o $@ $(LDFLAGS)

dist:
	mkdir -p dist

clean:
	rm -rf dist

# Convenience target: list discovered prototype source files
list-protos:
	@echo "Discovered prototype sources:"
	@for f in $(PROTO_SRCS); do echo "  $$f"; done
