#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "tiff.h"


struct Header {
  uint16 byte_order;
  uint16 magic_number;
  uint32 offset;
};

struct IFD {
  uint16 tag;
  uint16 tag_type;
  uint32 count;
  uint32 value;
};

unsigned int scan_ifd(FILE *fp, unsigned int offset, int page_num, int delete){
   fseek(fp, offset, SEEK_SET);
   short int ifd_count;
   fread(&ifd_count, sizeof(ifd_count), 1, fp);
   printf("Image #%d\n", page_num);
   printf("Found %d IFDs\n", ifd_count);
   int tiles_found = 1;
   int strips_found = 1;
   struct IFD ifds[ifd_count];
   fread(&ifds, sizeof(struct IFD), ifd_count, fp);
   for(int i = 0; i < ifd_count; i++){
     printf("TAG: %d, Type: %d, Count: %d, Value: %d\n", ifds[i].tag,
                                                         ifds[i].tag_type,
                                                         ifds[i].count,
                                                         ifds[i].value);
     if(ifds[i].tag == TIFFTAG_STRIPOFFSETS){
       strips_found = 0;
     }
     if(ifds[i].tag == TIFFTAG_TILEOFFSETS){
       tiles_found = 0;
     }
   }
   unsigned int next_offset;
   fread(&next_offset, sizeof(next_offset), 1, fp);
   printf("Next Offet: 0x%x\n", next_offset);
   if(delete){
     printf("Need to delete some stuff........\n");
     // tiles found
     if (tiles_found){
       //first allocate sizes


       //then iterate through offsets
     }

     // strips found
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
   int to_delete = atoi(argv[2]);
   while (next_offset > 0) {
     page_count += 1;
     last_offset = next_offset;
     next_offset = scan_ifd(fp, next_offset, page_count, page_count == to_delete);
     if(page_count == to_delete){
       printf("Updating last offset to next offset\n");
       return 0;
     }
   }
   return 0;
}
