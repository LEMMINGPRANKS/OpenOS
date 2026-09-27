#include "heap.h"

// Kernel heap: a free-list allocator with block splitting and merging,
// living in a fixed pool for now. Drawing pages from the PMM dynamically
// comes with virtual memory (higher-half + vmap) later.

#define HEAP_SIZE (4 * 1024 * 1024)      // 4 MiB (Paint canvases + PNGs)
#define ALIGN 16

struct block {
    uint64_t size;                       // payload bytes, bit 0 = free flag
};

static unsigned char pool[HEAP_SIZE] __attribute__((aligned(16)));

#define HDR     sizeof(struct block)
#define IS_FREE(b)   ((b)->size & 1)
#define PAYLOAD(b)   ((b)->size & ~(uint64_t)1)

static uint64_t used_bytes;

void heap_init(void)
{
    struct block *b = (struct block *)pool;
    b->size = (HEAP_SIZE - HDR) | 1;     // one big free block
}

void *kmalloc(size_t size)
{
    if (size == 0)
        size = 1;
    uint64_t need = (size + ALIGN - 1) & ~(uint64_t)(ALIGN - 1);

    struct block *b = (struct block *)pool;
    for (;;) {
        uint64_t pay = PAYLOAD(b);
        if (IS_FREE(b) && pay >= need) {
            // split if the leftover can hold a header + something useful
            if (pay >= need + HDR + 32) {
                struct block *n = (struct block *)((char *)b + HDR + need);
                n->size = (pay - need - HDR) | 1;
                b->size = need;
            } else {
                b->size &= ~(uint64_t)1;
            }
            used_bytes += PAYLOAD(b);
            return (char *)b + HDR;
        }
        unsigned char *next = (unsigned char *)b + HDR + pay;
        if (next + HDR > pool + HEAP_SIZE)
            return 0;                    // out of heap
        b = (struct block *)next;
    }
}

void kfree(void *ptr)
{
    unsigned char *p = ptr;
    if (p < pool + HDR || p > pool + HEAP_SIZE)
        return;                          // not ours; refuse quietly
    struct block *b = (struct block *)(p - HDR);
    b->size |= 1;
    used_bytes -= PAYLOAD(b);

    // merge walk: coalesce any adjacent free blocks back together
    struct block *w = (struct block *)pool;
    for (;;) {
        uint64_t pay = PAYLOAD(w);
        unsigned char *next = (unsigned char *)w + HDR + pay;
        if (next + HDR > pool + HEAP_SIZE)
            break;
        struct block *n = (struct block *)next;
        if (IS_FREE(w) && IS_FREE(n)) {
            w->size = (pay + HDR + PAYLOAD(n)) | 1;
            continue;                    // re-examine w with its new size
        }
        w = n;
    }
}

uint64_t heap_used(void)
{
    return used_bytes;
}

uint64_t heap_total(void)
{
    return HEAP_SIZE;
}
