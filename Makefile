PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
ETCDIR ?= $(PREFIX)/etc
MANDIR ?= $(PREFIX)/man
EXAMPLESDIR ?= $(PREFIX)/share/examples/fluxsnap
CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -pipe
CFLAGS += -Wall -Wextra -pedantic -std=c11

X11_CFLAGS != $(PKG_CONFIG) --cflags x11 xext 2>/dev/null || echo -I$(PREFIX)/include
X11_LIBS != $(PKG_CONFIG) --libs x11 xext 2>/dev/null || echo -L$(PREFIX)/lib -lX11 -lXext

PROG = fluxsnap
SRCS = src/fluxsnap.c

all: $(PROG)

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) $(X11_CFLAGS) -o $@ $(SRCS) $(X11_LIBS)

install: $(PROG)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)
	install -m 0755 contrib/fluxbox/fluxsnap-profile.sh $(DESTDIR)$(BINDIR)/fluxsnap-profile
	install -m 0755 contrib/fluxbox/install-user.sh $(DESTDIR)$(BINDIR)/fluxsnap-fluxbox-install
	install -d $(DESTDIR)$(ETCDIR)
	install -m 0644 fluxsnap.conf $(DESTDIR)$(ETCDIR)/fluxsnap.conf.sample
	install -d $(DESTDIR)$(MANDIR)/man1
	install -m 0644 man/fluxsnap.1 $(DESTDIR)$(MANDIR)/man1/fluxsnap.1
	install -d $(DESTDIR)$(EXAMPLESDIR)
	install -d $(DESTDIR)$(EXAMPLESDIR)/configs
	install -d $(DESTDIR)$(EXAMPLESDIR)/fluxbox
	install -m 0644 fluxsnap.conf $(DESTDIR)$(EXAMPLESDIR)/fluxsnap.conf
	install -m 0644 configs/left-main-right-stack.conf $(DESTDIR)$(EXAMPLESDIR)/configs/left-main-right-stack.conf
	install -m 0644 contrib/fluxbox/menu.inc $(DESTDIR)$(EXAMPLESDIR)/fluxbox/menu.inc
	install -m 0644 contrib/fluxbox/keys.sample $(DESTDIR)$(EXAMPLESDIR)/fluxbox/keys.sample
	install -m 0644 contrib/fluxbox/init.sample $(DESTDIR)$(EXAMPLESDIR)/fluxbox/init.sample
	install -m 0755 contrib/fluxbox/install-user.sh $(DESTDIR)$(EXAMPLESDIR)/fluxbox/install-user.sh

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(PROG)
	rm -f $(DESTDIR)$(BINDIR)/fluxsnap-profile
	rm -f $(DESTDIR)$(BINDIR)/fluxsnap-fluxbox-install
	rm -f $(DESTDIR)$(ETCDIR)/fluxsnap.conf.sample
	rm -f $(DESTDIR)$(MANDIR)/man1/fluxsnap.1
	rm -rf $(DESTDIR)$(EXAMPLESDIR)

clean:
	rm -f $(PROG)

.PHONY: all install uninstall clean
