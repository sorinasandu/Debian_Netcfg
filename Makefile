ifndef TARGETS
TARGETS=netcfg-dhcp netcfg-static netcfg
endif

# Note that the -lm is only here until we can go back to linking with libiw
# synamically. Which is waiting on libiw-udeb, which will not use libm.
LDOPTS=-ldebconfclient -ldebian-installer -lm # -liw
PREFIX=$(DESTDIR)/usr/
CFLAGS=-W -Wall  -Os

ifneq (,$(findstring debug,$(DEB_BUILD_OPTIONS)))
CFLAGS += -g
else
CFLAGS += -fomit-frame-pointer
endif

INSTALL=install
STRIPTOOL=strip
STRIP = $(STRIPTOOL) --remove-section=.note --remove-section=.comment

all: $(TARGETS)

netcfg-dhcp: netcfg-dhcp.o
netcfg-static: netcfg-static.o
netcfg: netcfg.o

$(TARGETS): netcfg-common.o
	$(CC) $(LDOPTS) -o $@ $^ /lib/libiw.a
	$(STRIP) $@

%.o: %.c
	$(CC) -c $(CFLAGS) $(INCS) -o $@ $<

clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean
