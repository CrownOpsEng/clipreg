CC ?= cc
PKG_CONFIG ?= pkg-config
SCANNER ?= wayland-scanner
CFLAGS ?= -O2 -g
CPPFLAGS ?=
LDFLAGS ?=

# Treat warnings in our own engine as build failures. Generated Wayland protocol
# code is compiled separately because warning behavior can vary with scanner/compiler
# versions even when the generated ABI is correct.
CLIPREG_WARN = -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion -Werror
PROTO_WARN = -Wall -Wextra
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags wayland-client 2>/dev/null)
PKG_LIBS := $(shell $(PKG_CONFIG) --libs wayland-client 2>/dev/null)

EXT_XML := protocol/clipreg-ext-data-control-v1.xml
COSMIC_XML := protocol/clipreg-cosmic-toplevel-v1.xml
BUILD := build
GEN := $(BUILD)/protocol

PROTO_HDRS := \
	$(GEN)/ext-data-control-v1-client-protocol.h \
	$(GEN)/cosmic-toplevel-info-unstable-v1-client-protocol.h
PROTO_SRCS := \
	$(GEN)/ext-data-control-v1-protocol.c \
	$(GEN)/cosmic-toplevel-info-unstable-v1-protocol.c
PROTO_OBJS := \
	$(BUILD)/ext-data-control-v1-protocol.o \
	$(BUILD)/cosmic-toplevel-info-unstable-v1-protocol.o

all: $(BUILD)/clipreg

check-deps:
	@command -v $(PKG_CONFIG) >/dev/null || { echo "Missing pkg-config" >&2; exit 1; }
	@$(PKG_CONFIG) --exists wayland-client || { echo "Missing wayland-client development files" >&2; exit 1; }
	@command -v $(SCANNER) >/dev/null || { echo "Missing wayland-scanner" >&2; exit 1; }

check-protocols: check-deps
	@test -f "$(EXT_XML)" || { echo "Missing bundled ext-data-control v1 protocol" >&2; exit 1; }
	@test -f "$(COSMIC_XML)" || { echo "Missing bundled COSMIC toplevel v1 protocol" >&2; exit 1; }

$(BUILD) $(GEN):
	mkdir -p $@

$(GEN)/ext-data-control-v1-client-protocol.h: $(EXT_XML) | $(GEN)
	$(SCANNER) client-header "$<" "$@"

$(GEN)/ext-data-control-v1-protocol.c: $(EXT_XML) | $(GEN)
	$(SCANNER) private-code "$<" "$@"

$(GEN)/cosmic-toplevel-info-unstable-v1-client-protocol.h: $(COSMIC_XML) | $(GEN)
	$(SCANNER) client-header "$<" "$@"

$(GEN)/cosmic-toplevel-info-unstable-v1-protocol.c: $(COSMIC_XML) | $(GEN)
	$(SCANNER) private-code "$<" "$@"

$(BUILD)/clipreg.o: src/clipreg.c $(PROTO_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CLIPREG_WARN) $(PKG_CFLAGS) -I$(GEN) -c "$<" -o "$@"

$(BUILD)/ext-data-control-v1-protocol.o: $(GEN)/ext-data-control-v1-protocol.c $(GEN)/ext-data-control-v1-client-protocol.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PROTO_WARN) $(PKG_CFLAGS) -I$(GEN) -c "$<" -o "$@"

$(BUILD)/cosmic-toplevel-info-unstable-v1-protocol.o: $(GEN)/cosmic-toplevel-info-unstable-v1-protocol.c $(GEN)/cosmic-toplevel-info-unstable-v1-client-protocol.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PROTO_WARN) $(PKG_CFLAGS) -I$(GEN) -c "$<" -o "$@"

$(BUILD)/clipreg: check-protocols $(BUILD)/clipreg.o $(PROTO_OBJS)
	$(CC) $(LDFLAGS) $(BUILD)/clipreg.o $(PROTO_OBJS) $(PKG_LIBS) -o "$@"

selftest: $(BUILD)/clipreg
	$(BUILD)/clipreg --selftest

check: selftest

clean:
	rm -rf $(BUILD)

.PHONY: all clean check check-deps check-protocols selftest
