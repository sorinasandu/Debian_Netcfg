# this is the size of the non-shared udebs
#-rw-r--r--    1 davidw   root         7114 Jan 16 20:49 netcfg-dhcp_0.04_i386.udeb
#-rw-r--r--    1 davidw   root         7556 Jan 16 20:49 netcfg-static_0.04_i386.udeb



ifndef TARGETS
TARGETS=netcfg-dhcp netcfg-static
endif
DHCP_CLIENT=-DPUMP
#-DDHCLIENT
#-DDHCPCD

MAJOR=0
MINOR=1
MICRO=0
LIB=libnetcfg.so
LIBNAME=libnetcfg.so.$(MAJOR).$(MINOR).$(MICRO)
SONAME=libnetcfg.so.$(MAJOR).$(MINOR)

LIBS=$(LIB) $(SONAME) $(LIBNAME)

INCS=-I../cdebconf/src/
LDOPTS=-L../cdebconf/src -ldebconf -Wl,-rpath,../cdebconf/src 
#-L. -lnetcfg
PREFIX=$(DESTDIR)/usr/
CFLAGS=-Wall  -Os -fomit-frame-pointer
INSTALL=install
STRIPTOOL=strip
STRIP = $(STRIPTOOL) --remove-section=.note --remove-section=.comment

all: $(TARGETS)
#$(LIBS)
netcfg-dhcp netcfg-static: netcfg-dhcp.c utils.o netcfg.o
	$(CC) $(CFLAGS) $@.c  -o $@ $(INCS) $(LDOPTS) $(DHCP_CLIENT) utils.o netcfg.o
	$(STRIP) $@
	size $@ 

netcfg.o:
	$(CC) -c $(CFLAGS) netcfg.c  -o $@ $(INCS)


$(LIBNAME): netcfg.c
	@echo Creating $(LIBNAME)
	$(CC) -shared -Wl,-soname,$(SONAME) -o $@ $^ $(INCS)
	size $@ 

$(SONAME) $(LIB): $(LIBNAME)
	@ln -sf $^ $@


clean:
	rm -f netcfg-dhcp netcfg-static *.o $(LIBS) 
