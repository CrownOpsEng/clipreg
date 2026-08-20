# SPDX-License-Identifier: AGPL-3.0-only
CC ?= cc
PKG_CONFIG ?= pkg-config
SCANNER ?= wayland-scanner
CFLAGS ?= -O2 -g
CPPFLAGS ?=
LDFLAGS ?=

VERSION_BASE := $(shell ./scripts/version.sh --base)
VERSION_FULL := $(shell ./scripts/version.sh --full)
CPPFLAGS += -DCLIPREG_VERSION=\"$(VERSION_FULL)\"

# Warnings in project-owned C are build failures. Scanner-generated code is
# compiled separately because warning output can vary by scanner/compiler.
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

.DEFAULT_GOAL := help

help: ## Show the supported operator/developer command surface.
	@awk 'BEGIN {FS=":.*## "} /^[a-zA-Z0-9_.-]+:.*## / {printf "  %-16s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

version: ## Print the exact build version.
	@./scripts/version.sh --full

build: $(BUILD)/clipreg ## Build ClipReg against the installed Wayland client ABI.

check-deps: ## Verify native build dependencies without modifying the machine.
	@command -v $(PKG_CONFIG) >/dev/null || { echo "Missing pkg-config" >&2; exit 1; }
	@$(PKG_CONFIG) --exists wayland-client || { echo "Missing wayland-client development files" >&2; exit 1; }
	@command -v $(SCANNER) >/dev/null || { echo "Missing wayland-scanner" >&2; exit 1; }

check-protocols: check-deps
	@test -f "$(EXT_XML)" || { echo "Missing bundled ext-data-control v1 protocol" >&2; exit 1; }
	@test -f "$(COSMIC_XML)" || { echo "Missing bundled COSMIC toplevel v1 protocol" >&2; exit 1; }

check-fast: ## Run the containerless/offline repository regression suite.
	@./test.sh

selftest: build ## Run the native binary self-test.
	@$(BUILD)/clipreg --selftest

check-native: clean selftest ## Build with real wayland-scanner/libwayland and run self-test.

check: check-fast check-native ## Canonical CI/local validation.

install: ## Install/reconcile this source tree using the deployment planner.
	@./deploy.sh

package: ## Build a deterministic source archive under dist/.
	@./scripts/package.sh

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

clean: ## Remove generated build output.
	rm -rf $(BUILD) dist

.PHONY: help version build check-deps check-protocols check-fast selftest check-native check install package clean
