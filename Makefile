PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
ETCDIR ?= $(PREFIX)/etc
MANDIR ?= $(PREFIX)/man
CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -pipe
CFLAGS += -Wall -Wextra -pedantic -std=c11

X11_CFLAGS != $(PKG_CONFIG) --cflags x11 2>/dev/null || echo -I$(PREFIX)/include
X11_LIBS != $(PKG_CONFIG) --libs x11 2>/dev/null || echo -L$(PREFIX)/lib -lX11

PROG = fluxsnap
SRCS = src/fluxsnap.c

all: $(PROG)

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) $(X11_CFLAGS) -o $@ $(SRCS) $(X11_LIBS)

install: $(PROG)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)
	install -d $(DESTDIR)$(ETCDIR)
	install -m 0644 fluxsnap.conf $(DESTDIR)$(ETCDIR)/fluxsnap.conf.sample
	install -d $(DESTDIR)$(MANDIR)/man1
	install -m 0644 man/fluxsnap.1 $(DESTDIR)$(MANDIR)/man1/fluxsnap.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(PROG)
	rm -f $(DESTDIR)$(ETCDIR)/fluxsnap.conf.sample
	rm -f $(DESTDIR)$(MANDIR)/man1/fluxsnap.1

clean:
	rm -f $(PROG)

.PHONY: all install uninstall clean
