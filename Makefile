ifndef TARGETS
TARGETS=netcfg-dhcp netcfg-static netcfg
endif

LDOPTS=-ldebconfclient -ldebian-installer
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

netcfg-dhcp netcfg-static netcfg: netcfg-dhcp.c netcfg-static.c netcfg.c netcfg-common.o 
	$(CC) $(CFLAGS) $@.c  -o $@ $(INCS) $(LDOPTS) netcfg-common.o
	$(STRIP) $@

netcfg-common.o: netcfg-common.c
	$(CC) -c $(CFLAGS) netcfg-common.c  -o $@ $(INCS)

clean:
	rm -f netcfg-dhcp netcfg-static netcfg *.o 
