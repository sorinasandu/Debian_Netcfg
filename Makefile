ifndef PROGS
PROGS=netcfg-dhcp netcfg-static
endif


INCS=-I../cdebconf/src/
LDOPTS=-L../cdebconf/src -ldebconf
PREFIX=$(DESTDIR)/usr/
CFLAGS=-Wall  -Os -fomit-frame-pointer
INSTALL=install
STRIPTOOL=strip
STRIP = $(STRIPTOOL) --remove-section=.note --remove-section=.comment


all: $(PROGS)

install:
	$(foreach PROG, $(PROGS), \
	-cp $(PROG) debian/$(PROG).postinst)

netcfg-dhcp: netcfg.c
	$(CC) $(CFLAGS) -DDHCP netcfg.c -o $@ $(INCS) $(LDOPTS)
	$(STRIP) $@
	size $@ 

netcfg-static: netcfg.c
	$(CC) $(CFLAGS) -DSTATIC netcfg.c -o $@ $(INCS) $(LDOPTS)
	$(STRIP) $@
	size $@ 

clean:
	rm -f netcfg-*
