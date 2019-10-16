#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include "tiff.h"

bool DEBUG = false;

#define BUFFER_SIZE 2048
char ZEROS[BUFFER_SIZE] = {0};

struct Header {
    uint16 byte_order;
    uint16 magic_number;
};

struct BigHeader {
    uint16 offset_bytesize;
    uint16 zeros;
};

struct IFD {
    uint16 tag;
    uint16 tag_type;
    int32 count;
    uint32 value;
};

struct __attribute__((__packed__)) BIGIFD {
    uint16 tag;
    uint16 tag_type;
    uint64_t count;
    uint64_t value;
};

int IFD_ROW_SIZE = sizeof(struct IFD);
int OFFSET_SIZE = sizeof(uint32);
int IFD_COUNT_SIZE = sizeof(int16);
bool BIG_TIFF = false;

int ifd_value_size(uint16 tag_type){
    switch(tag_type){
        case TIFF_NOTYPE:
            return 0;      /* placeholder */
            break;
        case TIFF_BYTE:
            return sizeof(uint8);        /* 8-bit unsigned integer */
            break;
        case TIFF_ASCII:
            return sizeof(uint8);       /* 8-bit bytes w/ last byte null */
            break;
        case TIFF_SHORT:
            return sizeof(uint16);     /* 16-bit unsigned integer */
            break;
        case TIFF_LONG:
            return sizeof(uint32);
            break;  /* 32-bit unsigned integer */
        case TIFF_RATIONAL:
            return sizeof(uint64_t); /* 64-bit unsigned fraction */
            break;
        case TIFF_SBYTE:
            return sizeof(uint8);       /* !8-bit signed integer */
            break;
        case TIFF_UNDEFINED:
            return sizeof(uint8);
            break;   /* !8-bit untyped data */
        case TIFF_SSHORT:
            return sizeof(uint16);      /* !16-bit signed integer */
            break;
        case TIFF_SLONG:
            return sizeof(uint32);      /* !32-bit signed integer */
            break;
        case TIFF_SRATIONAL:
            return sizeof(uint64_t);  /* !64-bit signed fraction */
            break;
        case TIFF_FLOAT:
            return sizeof(uint32);      /* !32-bit IEEE floating point */
            break;
        case TIFF_DOUBLE:
            return sizeof(uint64_t);    /* !64-bit IEEE floating point */
            break;
        case TIFF_IFD:
            return sizeof(uint32);
            break;        /* %32-bit unsigned integer (offset) */
        case TIFF_LONG8:
            return sizeof(uint64_t);      /* BigTIFF 64-bit unsigned integer */
            break;
        case TIFF_SLONG8:
            return sizeof(uint64_t);
            break;     /* BigTIFF 64-bit signed integer */
        case TIFF_IFD8:
            return sizeof(uint64_t);
            break;       /* BigTIFF 64-bit unsigned integer (offset) */
    }
    return 0;
}

struct IFD* find_tag(struct IFD ifds[], int ifd_count, uint16 tag){
    for(int i = 0; i < ifd_count; i++){
        if(ifds[i].tag == tag){
            return &ifds[i];
        }
    }
    return 0;
}

struct BIGIFD* find_big_tag(struct BIGIFD ifds[], int64_t ifd_count, uint16 tag){
    for(int i = 0; i < ifd_count; i++){
        if(ifds[i].tag == tag){
            return &ifds[i];
        }
    }
    return 0;
}

void tiff_clear(FILE *fp, off_t start, int64_t size){
    if(DEBUG) printf("Clearing %lld at 0x%llx\n", size, start);
    fseeko(fp, start, SEEK_SET);
    int64_t remaining_size = size;
    while(remaining_size > 0){
        if(remaining_size > BUFFER_SIZE){
            fwrite(ZEROS, sizeof(char), BUFFER_SIZE, fp);
        } else {
            fwrite(ZEROS, sizeof(char), remaining_size, fp);
        }
        remaining_size -= BUFFER_SIZE;
    }
}

void overwrite_ifd_offset(FILE *fp, off_t offset, off_t final_offset){
    fseeko(fp, offset, SEEK_SET);
    int64_t ifd_count = 0;
    fread(&ifd_count, IFD_COUNT_SIZE, 1, fp);
    fseek(fp, IFD_ROW_SIZE * ifd_count, SEEK_CUR);
    fwrite(&final_offset, OFFSET_SIZE, 1, fp);
}

off_t scan_ifd(FILE *fp, off_t offset, int page_num, bool delete){
    fseeko(fp, offset, SEEK_SET);
    int ifd_count;
    fread(&ifd_count, IFD_COUNT_SIZE, 1, fp);
    if(DEBUG) printf("Image #%d\n", page_num);
    if(DEBUG) printf("Found %d IFDs\n", ifd_count);
    bool tiles_found = false;
    bool strips_found = false;
    struct IFD ifds[ifd_count];
    fread(&ifds, IFD_ROW_SIZE, ifd_count, fp);
    for(int i = 0; i < ifd_count; i++){
        if(DEBUG) printf("TAG: %d, Type: %d, Count: %d, Value: %d\n",
               ifds[i].tag,
               ifds[i].tag_type,
               ifds[i].count,
               ifds[i].value);
        if(ifds[i].tag == TIFFTAG_STRIPOFFSETS){
            strips_found = true;
        }
        if(ifds[i].tag == TIFFTAG_TILEOFFSETS){
            tiles_found = true;
        }
    }
    off_t next_offset = 0;
    fread(&next_offset, OFFSET_SIZE, 1, fp);
    if(DEBUG) printf("Next Offset: 0x%llx\n", next_offset);
    if(delete){
        if(DEBUG) printf("Deleting IFD table\n");
        int64_t ifd_size = (IFD_ROW_SIZE * ifd_count) + IFD_COUNT_SIZE + OFFSET_SIZE;
        tiff_clear(fp, offset, ifd_size);
        if(tiles_found || strips_found){
            uint16 size_tag;
            uint16 offset_tag;
            if(tiles_found){
                offset_tag = TIFFTAG_TILEOFFSETS;
                size_tag = TIFFTAG_TILEBYTECOUNTS;
            } else {
                offset_tag = TIFFTAG_STRIPOFFSETS;
                size_tag = TIFFTAG_STRIPBYTECOUNTS;
            }
            struct IFD *size_row;
            struct IFD *offset_row;
            size_row = find_tag(ifds, ifd_count, size_tag);
            if(DEBUG) printf("Found this many tilesizes: %d\n", size_row->count);
            offset_row = find_tag(ifds, ifd_count, offset_tag);

            if(size_row->count != offset_row->count){
                printf("Bad Tile offset/size row found, exiting.\n");
                return 1;
            }

            //then iterate through offsets
            fseek(fp, offset_row->value, SEEK_SET);
            uint32 tile_addresses[offset_row->count];
            fread(&tile_addresses, OFFSET_SIZE, offset_row->count, fp);

            fseek(fp, size_row->value, SEEK_SET);
            uint32 tile_sizes[size_row->count];
            fread(&tile_sizes, OFFSET_SIZE, size_row->count, fp);
            // in the special case where a single tile/strip exists
            // we need to delete the offset from the value/offset field
            if(offset_row->count == 1){
              tiff_clear(fp, offset_row->value, size_row->value);
            } else {
              for(int i = 0; i < offset_row->count; i++){
                  tiff_clear(fp, tile_addresses[i], tile_sizes[i]);
              }
            }
        }

        // delete all off stored information
        for(int i = 0; i < ifd_count; i++){
            // TODO: change to something with size
            if(ifd_value_size(ifds[i].tag_type) * ifds[i].count > sizeof(uint32)){
                tiff_clear(fp, ifds[i].value, ifds[i].count * ifd_value_size(ifds[i].tag_type));
            }
        }
    }
    return next_offset;
}

off_t scan_big_ifd(FILE *fp, off_t offset, int page_num, bool delete){
    fseeko(fp, offset, SEEK_SET);
    int64_t ifd_count = 0;
    fread(&ifd_count, IFD_COUNT_SIZE, 1, fp);
    if(DEBUG) printf("Image #%d\n", page_num);
    if(DEBUG) printf("Found %lld IFDs\n", ifd_count);
    bool tiles_found = false;
    bool strips_found = false;
    struct BIGIFD ifds[ifd_count];
    fread(&ifds, IFD_ROW_SIZE, ifd_count, fp);
    for(int i = 0; i < ifd_count; i++){
        if(DEBUG) printf("TAG: %d, Type: %d, Count: %lld, Value: %lld\n",
               ifds[i].tag,
               ifds[i].tag_type,
               ifds[i].count,
               ifds[i].value);
        if(ifds[i].tag == TIFFTAG_STRIPOFFSETS){
            strips_found = true;
        }
        if(ifds[i].tag == TIFFTAG_TILEOFFSETS){
            tiles_found = true;
        }
    }
    off_t next_offset = 0;
    fread(&next_offset, OFFSET_SIZE, 1, fp);
    if(DEBUG) printf("Next Offset: 0x%llx\n", next_offset);
    if(delete){
        if(DEBUG) printf("Deleting IFD table\n");
        int64_t ifd_size = (IFD_ROW_SIZE * ifd_count) + IFD_COUNT_SIZE + OFFSET_SIZE;
        tiff_clear(fp, offset, ifd_size);
        if(tiles_found || strips_found){
            uint16 size_tag;
            uint16 offset_tag;
            if(tiles_found){
                offset_tag = TIFFTAG_TILEOFFSETS;
                size_tag = TIFFTAG_TILEBYTECOUNTS;
            } else {
                offset_tag = TIFFTAG_STRIPOFFSETS;
                size_tag = TIFFTAG_STRIPBYTECOUNTS;
            }
            struct BIGIFD *size_row;
            struct BIGIFD *offset_row;
            size_row = find_big_tag(ifds, ifd_count, size_tag);
            if(DEBUG) printf("Found this many tilesizes: %lld\n", size_row->count);
            offset_row = find_big_tag(ifds, ifd_count, offset_tag);

            if(size_row->count != offset_row->count){
                printf("Bad Tile offset/size row found, exiting.\n");
                return 1;
            }

            //then iterate through offsets
            fseek(fp, offset_row->value, SEEK_SET);
            uint64_t tile_addresses[offset_row->count];
            fread(&tile_addresses, OFFSET_SIZE, offset_row->count, fp);

            fseek(fp, size_row->value, SEEK_SET);
            uint64_t tile_sizes[size_row->count];
            fread(&tile_sizes, OFFSET_SIZE, size_row->count, fp);

            // in the special case where a single tile/strip exists
            // we need to delete the offset from the value/offset field
            if(offset_row->count == 1){
              tiff_clear(fp, offset_row->value, size_row->value);
            } else {
              for(int i = 0; i < offset_row->count; i++){
                  tiff_clear(fp, tile_addresses[i], tile_sizes[i]);
              }
            }
        }

        // delete all off stored information
        for(int i = 0; i < ifd_count; i++){
            // TODO: change to something with size
            if(ifd_value_size(ifds[i].tag_type) * ifds[i].count > sizeof(uint64_t)){
                tiff_clear(fp, ifds[i].value, ifds[i].count * ifd_value_size(ifds[i].tag_type));
            }
        }
    }
    return next_offset;
}

int main(int argc, char *argv[]) {
    FILE *fp;
    fp = fopen(argv[1], "r+b");
    if(fp == NULL){
        printf("Opening file failed\n");
        return 1;
    }
    struct Header header;
    fread(&header, sizeof(struct Header), 1, fp);

    off_t first_offset = 0;
    if (header.byte_order != TIFF_LITTLEENDIAN){
        printf("Non-little endian byte order found, exiting.");
        return 1;
    }
    if (header.magic_number == TIFF_VERSION_BIG){
        struct BigHeader big_header;
        fread(&big_header, sizeof(struct BigHeader), 1, fp);
        IFD_ROW_SIZE = 20;
        OFFSET_SIZE = sizeof(uint64_t);
        IFD_COUNT_SIZE = sizeof(uint64_t);
        BIG_TIFF = true;
    }
    fread(&first_offset, OFFSET_SIZE, 1, fp);
    if(DEBUG) printf("BO: %x\nMN: %d\nOffset: 0x%llx\n", header.byte_order,
           header.magic_number,
           first_offset);
    if(DEBUG) printf("Offsetsize %d\n", OFFSET_SIZE);
    off_t next_offset = first_offset;
    off_t last_offset = 0;
    int page_count = 0;
    int to_delete = -1;
    if(argc == 3){
        to_delete = atoi(argv[2]);
    }
    while (next_offset > 0) {
        page_count += 1;
        last_offset = next_offset;
        if(BIG_TIFF){
            next_offset = scan_big_ifd(fp, next_offset, page_count, page_count == to_delete);
        } else {
            next_offset = scan_ifd(fp, next_offset, page_count, page_count == to_delete);
        }
        if(page_count == to_delete){
            if(DEBUG) printf("Updating last offset to next offset\n");
            if(to_delete == 1){
                // need to update offset from first header
                if(DEBUG) printf("Overwriting Header Offset: 0x%llx\n", next_offset);
                fseek(fp, sizeof(struct Header), SEEK_SET);
                if(BIG_TIFF){
                    fseek(fp, sizeof(struct BigHeader), SEEK_CUR);
                }
                fwrite(&next_offset, OFFSET_SIZE, 1, fp);
                fclose(fp);
                return 0;
            } else {
                // need to scan to correct offset
                off_t final_offset = next_offset;
                next_offset = first_offset;
                page_count = 1;
                while (page_count < to_delete - 1){
                    if(BIG_TIFF){
                        next_offset = scan_big_ifd(fp, next_offset, page_count, false);
                    } else {
                        next_offset = scan_ifd(fp, next_offset, page_count, false);
                    }
                    page_count += 1;
                }
                if(DEBUG) printf("Overwriting IFD Offset: 0x%llx -> 0x%llx\n", next_offset, final_offset);
                overwrite_ifd_offset(fp, next_offset, final_offset);
                fclose(fp);
                return 0;
            }
            return 0;
        }
    }
    return 0;
}
