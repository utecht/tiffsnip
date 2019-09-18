#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "tiff.h"

#define TRUE 1
#define FALSE 0

#define BUFFER_SIZE 2048

struct Header {
  uint16 byte_order;
  uint16 magic_number;
  uint32 offset;
};

struct IFD {
  uint16 tag;
  uint16 tag_type;
  int32 count;
  uint32 value;
};

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

void clear(FILE *fp, uint32 start, int32 size){
   fseek(fp, start, SEEK_SET);
   int32 remaining_size = size;
   char buff[BUFFER_SIZE] = {0};
   while(remaining_size > 0){
     if(remaining_size > BUFFER_SIZE){
       fwrite(buff, sizeof(char), BUFFER_SIZE, fp);
     } else {
       fwrite(buff, sizeof(char), remaining_size, fp);
     }
     remaining_size -= BUFFER_SIZE;
   }
}

void overwrite_ifd_offset(FILE *fp, uint32 offset, uint32 final_offset){
   fseek(fp, offset, SEEK_SET);
   int16 ifd_count;
   fread(&ifd_count, sizeof(ifd_count), 1, fp);
   fseek(fp, sizeof(struct IFD) * ifd_count, SEEK_CUR);
   fwrite(&final_offset, sizeof(uint32), 1, fp);
}

uint32 scan_ifd(FILE *fp, uint32 offset, int page_num, int delete){
   fseek(fp, offset, SEEK_SET);
   int16 ifd_count;
   fread(&ifd_count, sizeof(ifd_count), 1, fp);
   printf("Image #%d\n", page_num);
   printf("Found %d IFDs\n", ifd_count);
   int tiles_found = FALSE;
   int strips_found = FALSE;
   struct IFD ifds[ifd_count];
   fread(&ifds, sizeof(struct IFD), ifd_count, fp);
   for(int i = 0; i < ifd_count; i++){
     printf("TAG: %d, Type: %d, Count: %d, Value: %d\n", ifds[i].tag,
                                                         ifds[i].tag_type,
                                                         ifds[i].count,
                                                         ifds[i].value);
     if(ifds[i].tag == TIFFTAG_STRIPOFFSETS){
       strips_found = TRUE;
     }
     if(ifds[i].tag == TIFFTAG_TILEOFFSETS){
       tiles_found = TRUE;
     }
   }
   uint32 next_offset;
   fread(&next_offset, sizeof(next_offset), 1, fp);
   printf("Next Offet: 0x%x\n", next_offset);
   if(delete){
     printf("Deleting IFD table\n");
     int32 ifd_size = (sizeof(struct IFD) * ifd_count) + sizeof(int16) + sizeof(uint32);
     clear(fp, offset, ifd_size);
     if(tiles_found == TRUE || strips_found == TRUE){
       uint16 size_tag;
       uint16 offset_tag;
       if(tiles_found == TRUE){
         offset_tag = TIFFTAG_TILEOFFSETS;
         size_tag = TIFFTAG_TILEBYTECOUNTS;
       } else {
         offset_tag = TIFFTAG_STRIPOFFSETS;
         size_tag = TIFFTAG_STRIPBYTECOUNTS;
       }
       struct IFD *size_row = find_tag(ifds, ifd_count, size_tag);
       printf("Found this many tilesizes: %d\n", size_row->count);
       struct IFD *offset_row = find_tag(ifds, ifd_count, offset_tag);

       if(size_row->count != offset_row->count){
         printf("Bad Tile offset/size row found, exiting.\n");
         return 1;
       }

       //then iterate through offsets
       fseek(fp, offset_row->value, SEEK_SET);
       uint32 tile_addresses[offset_row->count];
       fread(&tile_addresses, sizeof(uint32), offset_row->count, fp);

       fseek(fp, size_row->value, SEEK_SET);
       uint32 tile_sizes[size_row->count];
       fread(&tile_sizes, sizeof(uint32), size_row->count, fp);

       for(int i = 0; i < offset_row->count; i++){
         clear(fp, tile_addresses[i], tile_sizes[i]);
       }
     }

     // delete all off stored information
     for(int i = 0; i < ifd_count; i++){
       if(ifds[i].count > 1){
         clear(fp, ifds[i].value, ifds[i].count * ifd_value_size(ifds[i].tag_type));
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
   printf("BO: %x\nMN: %d\nOffset: 0x%x\n", header.byte_order,
                                            header.magic_number,
                                            header.offset);

   if (header.byte_order != TIFF_LITTLEENDIAN){
     printf("Non-little endian byte order found, exiting.");
     return 1;
   }
   if (header.magic_number != TIFF_VERSION_CLASSIC){
     printf("Non-classic TIFF Version found, exiting");
     return 1;
   }
   uint32 next_offset = header.offset;
   uint32 last_offset;
   int page_count = 0;
   int to_delete = -1;
   printf("%d", argc);
   if(argc == 3){
     to_delete = atoi(argv[2]);
   }
   while (next_offset > 0) {
     page_count += 1;
     last_offset = next_offset;
     next_offset = scan_ifd(fp, next_offset, page_count, page_count == to_delete);
     if(page_count == to_delete){
       printf("Updating last offset to next offset\n");
       if(to_delete == 1){
         // need to update offset from first header
         printf("Overwriting Header Offset: 0x%x\n", next_offset);
         fseek(fp, sizeof(struct Header) - sizeof(uint32), SEEK_SET);
         fwrite(&next_offset, sizeof(uint32), 1, fp);
         fclose(fp);
         return 0;
       } else {
         // need to scan to correct offset
         uint32 final_offset = next_offset;
         next_offset = header.offset;
         page_count = 1;
         while (page_count < to_delete - 1){
           next_offset = scan_ifd(fp, next_offset, page_count, FALSE);
           page_count += 1;
         }
         printf("Overwriting IFD Offset: 0x%x -> 0x%x\n", next_offset, final_offset);
         overwrite_ifd_offset(fp, next_offset, final_offset);
         fclose(fp);
         return 0;
       }
       return 0;
     }
   }
   return 0;
}
