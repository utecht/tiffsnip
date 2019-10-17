prefix = /usr/local

all: tiffsnip

tiffsnip: tiffsnip.c tiff.h tiffconf.h
	gcc -o $@ $<

install: tiffsnip
	install tiffsnip $(DESTDIR)$(prefix)/bin/tiffsnip

clean:
	-rm -f tiffsnip

distclean: clean

uninstall:
	-rm -f $(DESTDIR)$(prefix)/bin/tiffsnip

.PHONY: all install clean distclean uninstall
