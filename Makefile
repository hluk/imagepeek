CFLAGS = -Wall -O0 -g

PKG_CONFIG = pkg-config
PKGS = clutter-1.0
OUT = imagepeek

CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS))
LFLAGS += $(shell $(PKG_CONFIG) --libs $(PKGS))

.PHONY:
all: $(OUT)

$(OUT): main.c
		gcc $(CFLAGS) $(LFLAGS) -o $@ $<

.PHONY:
clean:
	$(RM) $(OUT)

