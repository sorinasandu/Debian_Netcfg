ifndef TARGETS
TARGETS=netcfg-dhcp netcfg-static
endif

LDOPTS=-ldebconfclient -ldebian-installer
PREFIX=$(DESTDIR)/usr/
CFLAGS=-W -Wall -Os

ifneq (,$(findstring debug,$(DEB_BUILD_OPTIONS)))
CFLAGS += -g
else
CFLAGS += -fomit-frame-pointer
endif

INSTALL=install
STRIPTOOL=strip
STRIP = $(STRIPTOOL) --remove-section=.note --remove-section=.comment

all: $(TARGETS)

netcfg-dhcp netcfg-static: netcfg-dhcp.c netcfg.o
	$(CC) $(CFLAGS) $@.c  -o $@ $(INCS) $(LDOPTS) netcfg.o
	$(STRIP) $@

netcfg.o: netcfg.c
	$(CC) -c $(CFLAGS) netcfg.c  -o $@ $(INCS)

clean:
	rm -f netcfg-dhcp netcfg-static *.o 
