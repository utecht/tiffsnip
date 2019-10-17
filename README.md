# tiffsnip

Remove pages from TIFF file without loading into memory.

This is a small command line utility for deleting pages from a multipage tiff file quickly.
This is accomplished by finding the requested page and zeroing out the data from both the IFD table and the referenced memory locations, 
after which the next offset from the previous page is forwarded to the page after.

## Installation
Tiffsnip has no requirements outside of the standard c library and should build on any platform with a simple make command.

## Usage
```
Usage: tiffsnip file page_index
	file: the tiff file to be snipped
	page_index: the page number to be snipped (1 indexed)
```

## Notes
Tiffsnip is still early and there may be edge cases in the tiff format that have not been anticipated.
If you find any crashes or incorrect behavior please open an issue.
