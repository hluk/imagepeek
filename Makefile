CFLAGS = -Wall -pedantic -O0 -g

PKG_CONFIG = pkg-config
PKGS = clutter-1.0
OUT = imagepeek

CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS))
LFLAGS += $(shell $(PKG_CONFIG) --libs $(PKGS))

all: $(OUT)

$(OUT): main.c
		gcc $(CFLAGS) $(LFLAGS) -o $@ $<

