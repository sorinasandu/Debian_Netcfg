ifndef TARGETS
TARGETS=netcfg-dhcp netcfg-static
endif

LDOPTS=-ldebconf -ldebian-installer
PREFIX=$(DESTDIR)/usr/
CFLAGS=-Wall  -Os

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
	size $@ 

netcfg.o: netcfg.c
	$(CC) -c $(CFLAGS) netcfg.c  -o $@ $(INCS)


test: netcfg.o
	cc -g test.c netcfg.o -o test

clean:
	rm -f netcfg-dhcp netcfg-static *.o 
