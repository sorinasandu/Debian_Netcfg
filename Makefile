ifndef TARGETS
TARGETS=netcfg-dhcp netcfg-static
endif

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
	$(CC) $(CFLAGS) $@.c  -o $@ $(INCS) $(LDOPTS) utils.o netcfg.o
	$(STRIP) $@
	size $@ 

netcfg.o: netcfg.c
	$(CC) -c $(CFLAGS) netcfg.c  -o $@ $(INCS)


$(LIBNAME): netcfg.c
	@echo Creating $(LIBNAME)
	$(CC) -shared -Wl,-soname,$(SONAME) -o $@ $^ $(INCS)
	size $@ 

$(SONAME) $(LIB): $(LIBNAME)
	@ln -sf $^ $@


clean:
	rm -f netcfg-dhcp netcfg-static *.o $(LIBS) 
