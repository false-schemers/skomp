# skomp -- a standalone whole-program Scheme compiler.
#
#   make            build skomp and the private tree beside it
#   make test       compile and run the end-to-end tests
#   make install    install skomp and its private tree
#   make clean      remove build products
#
# `make' leaves a runnable tree in the repository: ./skomp finds ./skomp-home
# beside itself, so nothing has to be installed to try it out.

-include    config.mk
PREFIX     ?= /usr/local
LIBEXECDIR ?= $(PREFIX)/libexec
SKOMPHOME   = $(LIBEXECDIR)/skomp

CFLAGS    = -O2 -DNDEBUG
LDFLAGS   =
LDLIBS    = -lm
MKPATH    = install -d
INSTALL   = install -m 755 -s
INSTALLD  = install -m 644
CPR       = cp -a
RM        = rm -f
RMR       = rm -rf
ARCH      = unknown
VERSION   = $(shell sed -n 's/^.define SKOMP_VERSION "\([0-9.]*\)"/\1/p' skomp.c)
HOME_DIR  = skomp-home

$(info version is $(VERSION))
$(info prefix is set to $(PREFIX))
$(info private tree will be installed in $(SKOMPHOME))

ifneq ($(shell which clang 2>/dev/null),)
  CC = clang
else
  CC = gcc
endif

# NaN boxing in the hosted interpreter assumes the upper 16 bits of a heap
# pointer are zero, which holds on these 64-bit targets.  Same test as skint's
# own makefile makes.
ifeq ($(OS),Windows_NT)
    ifeq ($(PROCESSOR_ARCHITEW6432),AMD64)
        ARCH = AMD64
    else
        ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
            ARCH = AMD64
        endif
    endif
else
    ifeq ($(shell uname),Darwin)
        UNAME := $(shell uname -m)
        ifeq ($(UNAME),x86_64)
            ARCH = AMD64
        endif
        ifeq ($(UNAME),arm64)
            ARCH = ARM64
        endif
    else
        UNAME := $(shell uname -p)
        ifeq ($(UNAME),unknown)
            UNAME := $(shell uname -m)
        endif
        ifeq ($(UNAME),x86_64)
            ARCH = AMD64
        endif
        ifeq ($(UNAME),aarch64)
            ARCH = ARM64
        endif
        ifeq ($(UNAME),ppc64le)
            ARCH = POWER64
        endif
        ifeq ($(UNAME),riscv64)
            ARCH = RV64
        endif
    endif
endif

NANBOX =
ifneq ($(filter $(ARCH),AMD64 ARM64 POWER64 RV64),)
  $(info 64-bit architecture is detected)
  NANBOX = -D NAN_BOXING
endif

.PHONY: all clean realclean distclean test install uninstall home

# The hosted interpreter: full numeric tower and full Unicode are NOT
# optional.  srt strings are arrays of codepoints, so an ASCII host would
# read a multi-byte literal in a source file as several characters and
# compile a different program; and the code generator carries 32-bit
# constants that want exact arithmetic.  OPT_ENHTTY is absent on purpose --
# skomp never enters a REPL, and opt/n_enhtty.* is not imported.
SKINT_CFLAGS = $(CFLAGS) $(NANBOX) -D OPT_TOWER -D OPT_UNICODE
SKINT_SRC    = skint/s.c skint/k.c skint/i.c skint/n.c skint/t.c
MARV_SRC     = marv/m.c marv/r.c marv/v.c marv/a.c
WCPL_SRC     = wcpl/c.c wcpl/l.c wcpl/p.c wcpl/w.c

# marv is built WITHOUT -D PREFIX: every path skomp hands it is absolute,
# including the .mvt, so it needs no lookup defaults of its own.

all: skomp home

skomp: skomp.c skomp_src.h
	$(CC) $(LDFLAGS) $(CFLAGS) -D SKOMP_HOME_DIR=\"$(SKOMPHOME)\" \
	    -o $@ skomp.c

home: $(HOME_DIR)/bin/skint $(HOME_DIR)/bin/marv $(HOME_DIR)/bin/wcpl \
      $(HOME_DIR)/.stamp-files

$(HOME_DIR)/bin/skint: $(SKINT_SRC)
	mkdir -p $(HOME_DIR)/bin
	cd skint && $(CC) $(LDFLAGS) $(SKINT_CFLAGS) -o ../$@ \
	    s.c k.c i.c n.c t.c $(LDLIBS)

$(HOME_DIR)/bin/marv: $(MARV_SRC)
	mkdir -p $(HOME_DIR)/bin
	cd marv && $(CC) $(LDFLAGS) $(CFLAGS) -o ../$@ \
	    m.c r.c v.c a.c $(LDLIBS)

$(HOME_DIR)/bin/wcpl: $(WCPL_SRC)
	mkdir -p $(HOME_DIR)/bin
	cd wcpl && $(CC) $(LDFLAGS) $(CFLAGS) -o ../$@ \
	    c.c l.c p.c w.c $(LDLIBS)

# The data half of the private tree: the compiler library and the target
# descriptions, copied rather than compiled.
$(HOME_DIR)/.stamp-files: $(shell find lib target -type f 2>/dev/null)
	mkdir -p $(HOME_DIR)
	$(RMR) $(HOME_DIR)/lib $(HOME_DIR)/target
	$(CPR) lib $(HOME_DIR)/lib
	$(CPR) target $(HOME_DIR)/target
	touch $@

test: all
	bash test/run-tests.sh

clean:
	$(RM) skomp
	$(RMR) $(HOME_DIR)

realclean: clean
	$(RM) config.mk

distclean: realclean

install: all
	$(MKPATH) $(PREFIX)/bin
	$(INSTALL) skomp $(PREFIX)/bin
	$(MKPATH) $(SKOMPHOME)/bin
	$(INSTALL) $(HOME_DIR)/bin/skint $(SKOMPHOME)/bin
	$(INSTALL) $(HOME_DIR)/bin/marv $(SKOMPHOME)/bin
	$(INSTALL) $(HOME_DIR)/bin/wcpl $(SKOMPHOME)/bin
	$(MKPATH) $(SKOMPHOME)/lib
	$(CPR) lib/. $(SKOMPHOME)/lib/
	$(MKPATH) $(SKOMPHOME)/target
	$(CPR) target/. $(SKOMPHOME)/target/
	find $(SKOMPHOME)/lib $(SKOMPHOME)/target -type f -exec chmod 644 {} +
	find $(SKOMPHOME)/lib $(SKOMPHOME)/target -type d -exec chmod 755 {} +
	$(MKPATH) $(PREFIX)/share/man/man1
	sed -e 's|@VERSION@|$(VERSION)|g' -e 's|@SKOMPHOME@|$(SKOMPHOME)|g' \
	    -e "s|@DATE@|$(shell date '+%d %b %Y')|g" \
	    doc/skomp.1.in > skomp.1
	$(INSTALLD) skomp.1 $(PREFIX)/share/man/man1/skomp.1
	$(RM) skomp.1

uninstall:
	$(RM) $(PREFIX)/bin/skomp
	$(RM) $(PREFIX)/share/man/man1/skomp.1
	$(RMR) $(SKOMPHOME)
