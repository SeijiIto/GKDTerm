# Makefile for GKDTerm (ROCKNIX cross build)

TARGET := gkd_term
SRC_DIR := src
SRC := \
	$(SRC_DIR)/app.c \
	$(SRC_DIR)/backlight.c \
	$(SRC_DIR)/battery.c \
	$(SRC_DIR)/clipboard.c \
	$(SRC_DIR)/config.c \
	$(SRC_DIR)/input.c \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/render.c \
	$(SRC_DIR)/screenshot.c \
	$(SRC_DIR)/scrollback.c \
	$(SRC_DIR)/session.c \
	$(SRC_DIR)/term.c \
	$(SRC_DIR)/text.c \
	$(SRC_DIR)/ui.c \
	$(SRC_DIR)/util.c

# Toolchain / sysroot: docker-shell内でexportされている想定
CC      ?= aarch64-rocknix-linux-gnu-gcc
SYSROOT ?=

# SDL headers/libs are in sysroot
#CFLAGS  += -O2 -g -Wall -Wextra
CFLAGS  += -O0 -g3 -fno-omit-frame-pointer
CFLAGS  += --sysroot=$(SYSROOT)
CFLAGS  += -I$(SYSROOT)/usr/include
CFLAGS  += -MMD -MP

LDFLAGS += --sysroot=$(SYSROOT)
LDFLAGS += -L$(SYSROOT)/usr/lib

LDLIBS  += -lSDL2 -lSDL2_ttf -lSDL2_image -lutil

# ---- libvterm (vendor build) ----
USE_VTERM ?= 1
VTERM_DIR := libvterm
VTERM_INC := -I$(VTERM_DIR)/include

# libvterm's object list (Makefile内のLIBOBJSに合わせて調整するのが確実)
VTERM_SRC := \
	$(VTERM_DIR)/src/vterm.c \
	$(VTERM_DIR)/src/parser.c \
	$(VTERM_DIR)/src/screen.c \
	$(VTERM_DIR)/src/state.c \
	$(VTERM_DIR)/src/encoding.c \
	$(VTERM_DIR)/src/unicode.c \
	$(VTERM_DIR)/src/pen.c

VTERM_OBJ := $(VTERM_SRC:.c=.o)

ifeq ($(USE_VTERM),1)
  CFLAGS += $(VTERM_INC)
  OBJ_EXTRA += $(VTERM_OBJ)
endif

OBJ := $(SRC:.c=.o) $(OBJ_EXTRA)
DEP := $(OBJ:.o=.d)
-include $(DEP)

.PHONY: all clean push run print-vars

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET)
	rm -f $(SRC_DIR)/*.o
	rm -f $(SRC_DIR)/*.d
	rm -f $(VTERM_DIR)/src/*.o
	rm -f $(VTERM_DIR)/src/*.d

print-vars:
	@echo "CC=$(CC)"
	@echo "SYSROOT=$(SYSROOT)"
	@echo "USE_VTERM=$(USE_VTERM)"

# ---- deploy ----
# 例: make push DEVICE=root@192.168.0.50 DEST=/usr/local/bin
DEVICE ?= root@gkd
DEST   ?= /storage/roms/ports

push: $(TARGET)
	scp $(TARGET) $(DEVICE):$(DEST)/$(TARGET)

run: push
	ssh $(DEVICE) "$(DEST)/$(TARGET)"
