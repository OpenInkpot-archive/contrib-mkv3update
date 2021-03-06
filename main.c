/*
 * mkv3update/main.c -- Hanlin V3 firmware builder.
 *
 * Part of OpenInkpot project (http://openinkpot.org/)
 *
 * Copyright © 2007 Yauhen Kharuzhy <jekhor@gmail.com>
 * Copyright © 2008-2009 Mikhail Gusarov <dottedmag@dottedmag.net>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <time.h>
#include <linux/byteorder/little_endian.h>

#include "md5.h"

typedef struct
{
  const char* name;
  const char* description;
  int offset; /* in blocks */
  int size; /* in blocks */
} partition_t;

const partition_t v3_orig_partitions[] =
{
  /* 0th block is the badblocks remapping space */
  { "kernel", "zImage", 1, 1 },
  { "rofs", "cramfs", 2, 6 },
  { "rootfs", "cramfs", 8, 44 },
  { "logo", "two 800x600 2-bit images", 52, 1 },
  { "userdata", "jffs2", 53, 2 },
  { "storage", "vfat", 55, 9 }
};

const partition_t v3_oi_partitions[] =
{
  /* 0th block is the badblocks remapping space */
  { "kernel", "zImage", 1, 1 },
  { "rootfs", "jffs2", 2, 50 },
  { "logo", "two 800x600 2-bit images", 52, 1 },
  { "userdata", "jffs2", 53, 2 },
  { "storage", "vfat", 55, 9 }
};

typedef struct
{
  const char* name;
  const char* tag;
  size_t size; /* in blocks, including remapping space */
  const partition_t *partitions;
  const size_t npartitions;
} layout_t;

const layout_t layouts[] =
{
  {
    "OpenInkpot V3 firmware", "oi", 64,
    v3_oi_partitions,
    sizeof(v3_oi_partitions) / sizeof(partition_t)
  },
  {
    "Original Hanlin V3 firmware", "hanlin", 64,
    v3_orig_partitions,
    sizeof(v3_orig_partitions) / sizeof(partition_t)
  }
};

const size_t nlayouts = sizeof(layouts) / sizeof(layout_t);

void describe_layout(const layout_t* layout)
{
  int i;

  printf("%s layout\n", layout->name);
  printf("Flash size: %d mb\n\n", layout->size);
  printf("offset   size  label      description\n");
  printf("  0 mb   1 mb  -- bad blocks remapping space --\n");

  for(i = 0; i < layout->npartitions; ++i)
  {
    printf("%3d mb %3d mb  %-10s %s\n",
           layout->partitions[i].offset,
           layout->partitions[i].size,
           layout->partitions[i].name,
           layout->partitions[i].description);
  }
}

const layout_t* get_layout(const char* layout_name)
{
  int i;
  for(i = 0; i < nlayouts; ++i)
    if(!strcmp(layouts[i].tag, layout_name))
      return &layouts[i];
  return NULL;
}


#define MEGABYTE (1024*1024)
#define BLOCK_SIZE MEGABYTE
#define HEADER_LEN 76

typedef struct {
  char version[32];
  char vendor[40];
  uint32_t data_blocks;
  unsigned char md5_sums[(BLOCK_SIZE - HEADER_LEN) / 16][16];
  unsigned char padding[(BLOCK_SIZE - HEADER_LEN) % 16];
} __attribute__((packed)) block0_t;


long div_ceil(long dividend, long divisor)
{
  return (dividend + divisor - 1) / divisor;
}

long max(long a, long b)
{
  return a > b ? a : b;
}

/*
 * Firmware contains [1,end_block) blocks.
 */
int firmware_end_block = 1;

int put_file_to_image(const partition_t* partition,
                      void* firmware,
                      const char* partition_filename)
{
  int f;
  char* buf;
  struct stat stat_info;
  off_t file_size;

  printf("Writing %s to partition %s from block %d...\n",
         partition_filename,
         partition->name,
         partition->offset);

  f = open(partition_filename, O_RDONLY);
  if(f == -1)
  {
    perror(partition_filename);
    return 1;
  }

  if(fstat(f, &stat_info) == -1)
  {
    perror("fstat");
    return 1;
  }

  file_size = stat_info.st_size;

  if(file_size > partition->size * BLOCK_SIZE)
  {
    fprintf(stderr, "%s is bigger than partition size: %lld > %lld bytes\n",
            partition_filename,
            (long long)file_size,
            (long long)partition->size * BLOCK_SIZE);
    return 1;
  }

  buf = mmap(NULL, file_size, PROT_READ, MAP_SHARED, f, 0);
  if(buf == MAP_FAILED)
  {
    perror(buf);
    return 1;
  }

  memcpy(firmware + partition->offset * BLOCK_SIZE, buf, file_size);

  if(-1 == munmap(buf, file_size))
  {
    perror("munmap");
    return 1;
  }

  if(-1 == close(f))
  {
    perror("close");
    return 1;
  }

  firmware_end_block = max(firmware_end_block,
                           partition->offset + div_ceil(file_size, BLOCK_SIZE));

  return 0;
}

int write_firmware_header(void *firmware, int end_block)
{
  block0_t* block0 = (block0_t*) firmware;
  time_t t;
  struct tm* local_t;
  int i;
  int first_block_size;

  printf("Writing firmware header...\n");

  t = time(NULL);
  local_t = localtime(&t);
  if(local_t == NULL)
  {
    perror("localtime");
    return 1;
  }

  memset(block0->version, 0, sizeof(block0->version));
  snprintf(block0->version, sizeof(block0->version) + 1,
           "JKV3:V3.01%04d%02d%02d",
           local_t->tm_year + 1900, local_t->tm_mon + 1, local_t->tm_mday);

  memset(block0->vendor, 0, sizeof(block0->vendor));
  snprintf(block0->vendor, sizeof(block0->vendor) + 1,
           "Nankai University and TianJin Jinke Corp");

  block0->data_blocks = __cpu_to_le32(end_block-1);

  /*
   * FIXME
   */
  printf("Calculating checksums...\n");

  /*
   * Checksums:
   *
   * sums[0..N-1] are checksums of the blocks 1..N.
   *
   * sums[N] is the checksum of the content of first block: version, vendor and
   * sums[0..N-1].
   */
  for(i = 1; i < end_block; ++i)
    __md5_buffer(firmware + i * BLOCK_SIZE, BLOCK_SIZE, block0->md5_sums[i-1]);

  first_block_size = (unsigned long)(block0->md5_sums[end_block-1])
    - (unsigned long)block0;

  printf("1st block is %d bytes long (%d block(s) in firmware, incl. header)\n",
         first_block_size, end_block);

  __md5_buffer(firmware, first_block_size, block0->md5_sums[end_block-1]);

  printf("Done.\n");

  return 0;
}

int build_firmware(const layout_t* layout,
                   int nfiles, char** filenames,
                   const char* output_file)
{
  int firmware_fd;
  void *firmware;
  int i;
  int res;

  printf("Building %s firmware in %s\n\n", layout->name, output_file);

  firmware_fd = open(output_file, O_RDWR | O_CREAT | O_TRUNC, 0644);
  if(firmware_fd == -1)
  {
    perror("creat");
    return 1;
  }

  if(ftruncate(firmware_fd, layout->size * BLOCK_SIZE) == -1)
  {
    perror("ftruncate");
    return 1;
  }

  firmware = mmap(NULL, layout->size * BLOCK_SIZE, PROT_READ | PROT_WRITE,
                  MAP_SHARED, firmware_fd, 0);
  if(firmware == MAP_FAILED)
  {
    perror("mmap");
    return 1;
  }

  memset(firmware, 0xff, layout->size * BLOCK_SIZE);

  for(i = 0; i < layout->npartitions; ++i)
  {
    const partition_t* partition = layout->partitions + i;

    if(i < nfiles)
    {
      res = put_file_to_image(partition, firmware, filenames[i]);
      if(res != 0)
      {
        fprintf(stderr, "Error during writing partition %s, bailing out.\n",
                partition->name);
        return res;
      }
    }
    else
    {
      printf("Skipping partiton %s: no file supplied\n", partition->name);
    }
  }

  res = write_firmware_header(firmware, firmware_end_block);
  if(res != 0)
  {
    fprintf(stderr, "Error during writing firmware header, bailing out.\n");
    return res;
  }

  if(munmap(firmware, layout->size * BLOCK_SIZE) == -1)
  {
    perror("munmap");
    return 1;
  }

  if(ftruncate(firmware_fd, firmware_end_block * BLOCK_SIZE) == -1)
  {
    perror("ftruncate");
    return 1;
  }

  if(close(firmware_fd) == -1)
  {
    perror("close");
    return 1;
  }

  return 0;
}

void usage(const char* progname)
{
  int i, j;

  printf("Hanlin v3 firmware builder.\n\nUsage:\n");
  printf("%s --describe-layout=(", progname);
  for(i = 0; i < nlayouts; ++i)
    printf("%s%s", i > 0 ? "|": "", layouts[i].tag);
  printf(")\n");

  for(i = 0; i < nlayouts; ++i)
  {
    printf("%s --write-%s=<outfile>", progname, layouts[i].tag);
    for(j = 0; j < layouts[i].npartitions; ++j)
      printf(" <%s>", layouts[i].partitions[j].name);
    printf("\n");
  }
  printf("\n");
  printf("Any file may be omitted. Resulting image will be truncated.\n");
}

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "Use %s --help to see usage informaiton.\n", argv[0]);
    exit(1);
  }

  if(argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")))
  {
    usage(argv[0]);
    exit(0);
  }

  if(argc == 2 && (!strncmp("--describe-layout=", argv[1], 18)))
  {
    const layout_t* layout = get_layout(argv[1] + 18);
    if(layout == NULL)
    {
      fprintf(stderr, "Unknown layout: %s\n", argv[1] + 18);
      exit(1);
    }
    describe_layout(layout);
    exit(0);
  }

  if(strncmp("--write-", argv[1], 8))
  {
    fprintf(stderr, "Use %s --help to see usage information.\n", argv[0]);
    exit(1);
  }
  else
  {
    const layout_t* layout;

    char *c = strchr(argv[1] + 8, '=');
    if(!c)
    {
      fprintf(stderr, "No output file name specified.\n");
      exit(1);
    }
    *c = 0;

    layout = get_layout(argv[1] + 8);
    if(layout == NULL)
    {
      fprintf(stderr, "Unknown layout: %s\n", argv[1] + 8);
      exit(1);
    }

    exit(build_firmware(layout, argc - 2, argv + 2, c+1));
  }
}
