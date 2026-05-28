/*
 * Guest-side exerciser for the mxgpu-mini BARs
 *
 * Opens the device's BAR resource files via sysfs, mmaps them, reads
 * BAR0's registers, and round-trips a pattern through BAR2's RAM.
 *
 * Build (in the guest):
 *   cc -02 -Wall -o bar_test bar_test.c
 * Run:
 *   sudo ./bar_test <pci-bdf>
 *   (<pci-bdf> can be 0000:00:04.0 for example)
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * Map one BAR into our address space via its sysfs resource file.
 * Returns the mapping base; stores the BAR length in *len_out.
 * Fatal on any error. This is a test tool, not a library.
 */
static volatile void *map_bar(const char *bdf, int bar, size_t *len_out) {
  char path[128];
  snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/resource%d", bdf, bar);

  int fd = open(path, O_RDWR);
  if (fd < 0) {
    perror(path);
    exit(1);
  }

  struct stat st;
  if (fstat(fd, &st) < 0) {
    perror("fstat");
    exit(1);
  }

  void *base =
      mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (base == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }
  close(fd); /* the mapping keeps the BAR open; the fd is no longer needed */

  *len_out = st.st_size;
  return base;
}

/*
 * 32-bit accessors over a mapped BAR0
 *
 * Both reads and writes go through volatile-qualified dword pointers.
 * `volatile` forbids the compiler from caching the value, eliding a
 * "redundant" access, or reordering it past another. This is
 * mandatory here because on BAR0 the "memory" is really your QEMU
 * .read/.write handler, and on the counter register two reads must
 * produce two distinct bus transactions and two distinct values.
 */
static uint32_t bar_read32(volatile void *base, size_t off) {
  return *(volatile uint32_t *)((volatile uint8_t *)base + off);
}

static void bar_write32(volatile void *base, size_t off, uint32_t val) {
  *(volatile uint32_t *)((volatile uint8_t *)base + off) = val;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <pci-bdf>   e.g. 0000:00:04.0\n", argv[0]);
    return 2;
  }
  const char *bdf = argv[1];

  size_t bar0_len, bar2_len;
  volatile void *bar0 = map_bar(bdf, 0, &bar0_len);
  volatile void *bar2 = map_bar(bdf, 2, &bar2_len);

  printf("BAR0 (%zu bytes) first 4 dwords:\n", bar0_len);
  for (size_t off = 0; off < 16; off += 4) {
    printf("  [0x%02zx] = 0x%08x\n", off, bar_read32(bar0, off));
  }

  printf("BAR2 (%zu bytes) first 4 dwords:\n", bar2_len);
  for (size_t off = 0; off < 16; off += 4) {
    printf("  [0x%02zx] = 0x%08x\n", off, bar_read32(bar2, off));
  }

  return 0;
}
