PROJECT_ABS := $(abspath .)
BUILD_DIR ?= build
AMIGA_BUILD_DIR ?= $(BUILD_DIR)/amiga
ADF_DIR ?= $(BUILD_DIR)/adf
EXE_BUILD_NAME := VisualAudio
PROGRAM_NAME := Visual Audio
ADF_LABEL ?= Visual Audio
WORKBENCH_ICON := packaging/VisualAudio.info
WORKBENCH_ICON_SOURCE := packaging/logo.png
WORKBENCH_ICON_TOOL := tools/png_to_workbench_icon.py
WORKBENCH_ICON_WIDTH ?= 80
WORKBENCH_ICON_HEIGHT ?= 56
WORKBENCH_ICON_DEPTH ?= 3
AMIGA_OFS_MAX_INLINE_FILE_BYTES ?= 131072
AMITOOLS_VENV ?= $(BUILD_DIR)/tools/amitools-venv
XDFTOOL ?= $(shell command -v xdftool 2>/dev/null || echo $(AMITOOLS_VENV)/bin/xdftool)
ADF_STAGING_DIR := $(ADF_DIR)/root
FS_UAE_STATE_NAME ?= A1200 - WB
FS_UAE_SAVE_STATE_DIR ?= $(HOME)/Documents/FS-UAE/Save States/$(FS_UAE_STATE_NAME)
FS_UAE_ADF_CACHE = $(FS_UAE_SAVE_STATE_DIR)/$(basename $(notdir $(ADF_FILE))).sdf

AMIGA_DOCKER_IMAGE ?= amigadev/crosstools:m68k-amigaos-gcc10_amd64
AMIGA_NATIVE_CC := $(shell command -v m68k-amigaos-gcc 2>/dev/null)
AMIGA_NATIVE_AS := $(shell command -v vasmm68k_mot 2>/dev/null || command -v /opt/m68k-amigaos/bin/vasmm68k_mot 2>/dev/null)
AMIGA_DOCKER_RUN ?= docker run --rm --platform linux/amd64 --user $(shell id -u):$(shell id -g) -v "$(PROJECT_ABS):/work" -w /work $(AMIGA_DOCKER_IMAGE)
AMIGA_CC ?= $(if $(AMIGA_NATIVE_CC),$(AMIGA_NATIVE_CC),$(AMIGA_DOCKER_RUN) m68k-amigaos-gcc)
AMIGA_AS ?= $(if $(AMIGA_NATIVE_AS),$(AMIGA_NATIVE_AS),$(AMIGA_DOCKER_RUN) vasmm68k_mot)
FS_UAE ?= /Applications/FS-UAE.app/Contents/MacOS/fs-uae

AMIGA_CFLAGS ?= -Os -std=gnu89 -Wall -Wextra -Werror -m68000
AMIGA_ASFLAGS ?= -quiet -Fhunk -m68000 -phxass
AMIGA_PTPLAYER_ASFLAGS ?= $(AMIGA_ASFLAGS) -DOSCOMPAT=1
AMIGA_LDFLAGS ?= -mcrt=nix13 -lamiga

SRCS := src/main.c src/input.c src/music.c src/visual.c
ASM_SRCS := src/music/ptplayer_wrap.asm src/music/vendor/ptplayer/ptplayer.asm
HEADERS := src/input.h src/music.h src/visual.h
AMIGA_OBJS := $(SRCS:src/%.c=$(AMIGA_BUILD_DIR)/%.o)
AMIGA_ASM_OBJS := $(ASM_SRCS:src/%.asm=$(AMIGA_BUILD_DIR)/%.o)
AMIGA_TARGET := $(AMIGA_BUILD_DIR)/$(EXE_BUILD_NAME)
ADF_FILE := $(ADF_DIR)/visual-audio.adf

.PHONY: all amiga icon adf adfs run-adf clean clean-fsuae-cache

all: amiga

amiga: $(AMIGA_TARGET)

icon: $(WORKBENCH_ICON)

adf: clean-fsuae-cache $(ADF_FILE)

adfs: adf

run-adf: clean-fsuae-cache $(ADF_FILE)
	$(FS_UAE) --floppy-drive-0=$(ADF_FILE)

clean-fsuae-cache:
	rm -f "$(FS_UAE_ADF_CACHE)"
	tmpdir="$${TMPDIR:-/tmp}"; \
		rm -f "$$tmpdir"/fs-uae-*/$(notdir $(ADF_FILE)) 2>/dev/null || true

$(AMIGA_TARGET): $(AMIGA_OBJS) $(AMIGA_ASM_OBJS)
	mkdir -p $(@D)
	$(AMIGA_CC) $(AMIGA_CFLAGS) $(AMIGA_OBJS) $(AMIGA_ASM_OBJS) $(AMIGA_LDFLAGS) -o $@
	bytes=$$(wc -c < $@); \
	if [ $$bytes -gt $(AMIGA_OFS_MAX_INLINE_FILE_BYTES) ]; then \
		echo "error: $@ is $$bytes bytes; keep it <= $(AMIGA_OFS_MAX_INLINE_FILE_BYTES) bytes for this ADF build"; \
		exit 1; \
	fi

$(AMIGA_BUILD_DIR)/%.o: src/%.c $(HEADERS)
	mkdir -p $(@D)
	$(AMIGA_CC) $(AMIGA_CFLAGS) -c $< -o $@

$(AMIGA_BUILD_DIR)/music/vendor/ptplayer/ptplayer.o: src/music/vendor/ptplayer/ptplayer.asm
	mkdir -p $(@D)
	$(AMIGA_AS) $(AMIGA_PTPLAYER_ASFLAGS) -o $@ $<

$(AMIGA_BUILD_DIR)/%.o: src/%.asm
	mkdir -p $(@D)
	$(AMIGA_AS) $(AMIGA_ASFLAGS) -o $@ $<

$(AMITOOLS_VENV)/bin/xdftool:
	python3 -m venv $(AMITOOLS_VENV)
	$(AMITOOLS_VENV)/bin/pip install amitools

$(WORKBENCH_ICON): $(WORKBENCH_ICON_SOURCE) $(WORKBENCH_ICON_TOOL)
	python3 $(WORKBENCH_ICON_TOOL) --width $(WORKBENCH_ICON_WIDTH) --height $(WORKBENCH_ICON_HEIGHT) --depth $(WORKBENCH_ICON_DEPTH) $< $@

$(ADF_FILE): $(AMIGA_TARGET) $(WORKBENCH_ICON) $(XDFTOOL) Makefile
	mkdir -p $(@D)
	rm -rf $(ADF_STAGING_DIR)
	mkdir -p $(ADF_STAGING_DIR)/S
	cp "$(AMIGA_TARGET)" "$(ADF_STAGING_DIR)/$(PROGRAM_NAME)"
	cp "$(WORKBENCH_ICON)" "$(ADF_STAGING_DIR)/$(PROGRAM_NAME).info"
	printf '"$(PROGRAM_NAME)"\n' > "$(ADF_STAGING_DIR)/S/startup-sequence"
	rm -f "$@.tmp.adf"
	$(XDFTOOL) -f "$@.tmp.adf" format "$(ADF_LABEL)" + \
		boot install boot1x + \
		write "$(ADF_STAGING_DIR)/$(PROGRAM_NAME)" "$(PROGRAM_NAME)" + \
		write "$(ADF_STAGING_DIR)/$(PROGRAM_NAME).info" "$(PROGRAM_NAME).info" + \
		makedir S + \
		write "$(ADF_STAGING_DIR)/S/startup-sequence" S/startup-sequence + \
		list
	mv -f "$@.tmp.adf" "$@"
	chmod a-w "$@"

clean:
	rm -rf $(BUILD_DIR)
