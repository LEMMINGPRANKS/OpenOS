#include "mm.h"
#include "initrd.h"

#define FRAME_SIZE   4096
#define NFRAMES      (16384 * 64)          // 1M frames = 4 GiB of tracking
#define MAPPED_LIMIT (1ULL * 1024 * 1024 * 1024)   // identity-mapped area

struct mb2_tag {
    uint32_t type;
    uint32_t size;
};

struct mmap_entry {
    uint64_t base;
    uint64_t len;
    uint32_t type;                        // 1 = usable, else reserved
    uint32_t reserved;
} __attribute__((packed));

static uint64_t bmap[NFRAMES / 64];       // 1 = used, 0 = free
static uint64_t alloc_limit;              // highest frame we may hand out
static uint64_t usable_total;
static uint64_t free_count;

extern char __kernel_end[];

static void set_used(uint64_t frame)
{
    if (frame < NFRAMES && !(bmap[frame >> 6] & (1ULL << (frame & 63)))) {
        bmap[frame >> 6] |= 1ULL << (frame & 63);
        if (frame < alloc_limit)
            free_count--;
    }
}

static void set_free(uint64_t frame)
{
    if (frame < NFRAMES && (bmap[frame >> 6] & (1ULL << (frame & 63)))) {
        bmap[frame >> 6] &= ~(1ULL << (frame & 63));
        if (frame < alloc_limit)
            free_count++;
    }
}

static void mark_range_used(uint64_t start, uint64_t end)
{
    for (uint64_t f = start / FRAME_SIZE; f < (end + FRAME_SIZE - 1) / FRAME_SIZE; f++)
        set_used(f);
}

void mm_init(unsigned long mb2_addr)
{
    // everything starts "used"; usable regions get freed below
    for (uint64_t i = 0; i < NFRAMES / 64; i++)
        bmap[i] = ~(uint64_t)0;

    uint32_t total = *(volatile uint32_t *)mb2_addr;
    uint64_t p = mb2_addr + 8;
    uint64_t top_usable = 0;

    while (p < mb2_addr + total) {
        volatile struct mb2_tag *tag = (volatile struct mb2_tag *)p;
        if (tag->type == 0)
            break;
        if (tag->type == 6) {            // memory map
            uint32_t esize = *(volatile uint32_t *)(p + 8);
            uint64_t n = (tag->size - 16) / esize;
            for (uint64_t i = 0; i < n; i++) {
                volatile struct mmap_entry *e =
                    (volatile struct mmap_entry *)(p + 16 + i * esize);
                if (e->type != 1)
                    continue;            // reserved / ACPI / bad
                usable_total += e->len;
                if (e->base + e->len > top_usable)
                    top_usable = e->base + e->len;
                for (uint64_t f = e->base / FRAME_SIZE;
                     f < (e->base + e->len) / FRAME_SIZE && f < NFRAMES; f++)
                    set_free(f);
            }
        }
        p = (p + tag->size + 7) & ~(uint64_t)7;
    }

    // never hand out unmapped frames
    alloc_limit = (top_usable > MAPPED_LIMIT ? MAPPED_LIMIT : top_usable) / FRAME_SIZE;

    // low memory, the kernel, the initramfs and the mb2 info are all taken
    mark_range_used(0, 0x100000);
    mark_range_used(0x100000, (uint64_t)__kernel_end);
    uint64_t rd_start, rd_end;
    initrd_bounds(&rd_start, &rd_end);
    if (rd_end > rd_start)
        mark_range_used(rd_start, rd_end);
    mark_range_used(mb2_addr, mb2_addr + total);

    // recount from the bitmap: the init-time set/clear sequence above
    // ran before alloc_limit existed, so deltas are not trustworthy
    free_count = 0;
    for (uint64_t f = 0; f < alloc_limit; f++)
        if (!(bmap[f >> 6] & (1ULL << (f & 63))))
            free_count++;
}

uint64_t mm_total_usable(void)
{
    return usable_total;
}

uint64_t mm_free_bytes(void)
{
    return free_count * FRAME_SIZE;
}

uint64_t pmm_alloc_frame(void)
{
    for (uint64_t f = 0; f < alloc_limit; f++) {
        if (!(bmap[f >> 6] & (1ULL << (f & 63)))) {
            set_used(f);
            return f * FRAME_SIZE;
        }
    }
    return 0;                            // out of memory below 1 GiB
}

void pmm_free_frame(uint64_t addr)
{
    set_free(addr / FRAME_SIZE);
}
