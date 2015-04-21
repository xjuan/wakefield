
CLEANFILES =
PKGS = gtk+-3.0 wayland-server wayland-client
CFLAGS = $(shell pkg-config --cflags $(PKGS)) -Wall -Werror -g -O0 -Wno-deprecated-declarations -D_GNU_SOURCE
LDFLAGS = $(shell pkg-config --libs $(PKGS))

FILES = wakefield-compositor.c wakefield-surface.c wakefield-compositor.h wakefield-private.h

all: libwakefield.so test-compositor test-client test-client-2

INTROSPECTION_GIRS = Wakefield-1.0.gir
INTROSPECTION_SCANNER_ARGS = --warn-all --warn-error --no-libtool

Wakefield-1.0.gir: libwakefield.so
Wakefield_1_0_gir_INCLUDES = Gtk-3.0
Wakefield_1_0_gir_CFLAGS = $(CFLAGS)
Wakefield_1_0_gir_LIBS = wakefield
Wakefield_1_0_gir_FILES = $(FILES)

CLEANFILES += Wakefield-1.0.gir Wakefield-1.0.typelib

include Makefile.introspection

xdg-shell-protocol.c : protocol/xdg-shell.xml
	wayland-scanner code < $< > $@
	sed -i -e 's/WL_EXPORT //' $@

xdg-shell-server-protocol.h : protocol/xdg-shell.xml
	wayland-scanner server-header < $< > $@

libwakefield.so: CFLAGS += -fPIC -shared
libwakefield.so: wakefield-compositor.o wakefield-surface.o xdg-shell-server-protocol.h xdg-shell-protocol.o
	$(CC) $(CFLAGS) -o $@ wakefield-compositor.o wakefield-surface.o $(LDFLAGS)

CLEANFILES += libwakefield.so wakefield-compositor.o wakefield-surface.o

test-compositor: LDFLAGS += -L. -lwakefield

clean:
	rm -f $(CLEANFILES)
