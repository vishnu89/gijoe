# Disksim SSD ADIVIM support
# Copyright (c) 2012 ADIVIM PROJECT team, lecture project1, Seoul National University. All right reserved.
# Makefile is modified from ../ssdmodel/Makefile.
include .paths

LDFLAGS = -lm -L. -ldisksim $(DISKMODEL_LDFLAGS) $(MEMSMODEL_LDFLAGS) \
                            $(LIBPARAM_LDFLAGS) $(LIBDDBG_LDFLAGS) 

HP_FAST_OFLAGS = +O4
NCR_FAST_OFLAGS = -O4 -Hoff=BEHAVED 
FREEBLOCKS_OFLAGS =
DEBUG_OFLAGS = -g -DASSERTS # -DDEBUG=1
PROF_OFLAGS = -g -DASSERTS -p
GPROF_OFLAGS = -g -DASSERTS -pg
CFLAGS = $(DEBUG_OFLAGS) $(DISKSIM_CFLAGS) $(DISKMODEL_CFLAGS) -I../ -I$(DISKSIM_PREFIX)/src

#CC = cc
CC = gcc -Wall -Wno-unused -MD
# because purify spits out warnings on stdout...
CC-DEP = gcc $(LIBPARAM_CFLAGS) $(DISKMODEL_CFLAGS)

MODULEDEPS = $(wildcard modules/*.h modules/*.c)
ifeq ($(MODULEDEPS),)
MODULEDEPS = modules
endif

all : libadivim.a

clean:
	rm -f TAGS *.o libadivim.a
	$(MAKE) -C modules clean

realclean: clean
	rm -f *.d .depend
	$(MAKE) -C modules clean

distclean: realclean
	rm -f *~ *.a
	rm -rf ../lib ../include
	$(MAKE) -C modules distclean

.PHONY: modules

#modules: modules/* ;

modules: 
	$(MAKE) -C modules
	mkdir -p include/adivim/modules
	cp -pR modules/*.h include/adivim/modules
	cp adivim.h include/adivim

modules/*.h modules/*.c:
	$(MAKE) -C modules

-include *.d

DISKSIM_ADIVIM_SRC = adivim.c

DISKSIM_ADIVIM_OBJ = $(DISKSIM_SSD_SRC:.c=.o) 

$(DISKSIM_ADIVIM_OBJ): %.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@


libadivim.a: $(MODULEDEPS) $(DISKSIM_ADIVIM_OBJ) 
	ar cru $@ $(DISKSIM_ADIVIM_OBJ)
	ranlib $@
	mkdir -p lib
	cp libadivim.a lib


.depend: *.c *.h
	$(MAKE) -C modules
	rm -f .depend
	$(foreach file, $(DISKSIM_ADIVIM_SRC), \
		$(CC-DEP) $(CFLAGS) -M $(file) >> .depend; )

