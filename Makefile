CC = clang
#CFLAGS += -Wall -O0 -g

PKG_CONFIG = pkg-config
PKGS = clutter-1.0
OUT = imagepeek

CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS))
LFLAGS += $(shell $(PKG_CONFIG) --libs $(PKGS))

.PHONY:
all: $(OUT)

$(OUT): main.c
	$(CC) $(CFLAGS) $(LFLAGS) -o $@ $<

%.pch: %
	$(CC) -emit-pch $(CFLAGS) -o $@ $<

.PHONY:
analyze: main.c
	@echo '=== running cppcheck ==='
	cppcheck --enable=all --force --inline-suppr $(shell echo $(CFLAGS)|grep -o -- '-[DIU]\s*\S\+') -q $<
	@echo '=== running clang-analyzer ==='
	scan-build -v -V make clean all

.PHONY:
clean:
	$(RM) $(OUT)

