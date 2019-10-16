all: tiffsnip


tiffsnip: tiffsnip.c tiff.h tiffconf.h
	gcc -o $@ $<
