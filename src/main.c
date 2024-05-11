#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

/*
 *************************************************************************************************
 * Arena Implementation
 *************************************************************************************************
 */

// Represents a single mapped block of memory, with a link to the next block
typedef struct region_t region_t;
struct region_t
{
    // Pointer to the next region
    region_t *next;
    // Total size of memory managed by this region
    size_t cap;
    // Address of the current start of free space
    uintptr_t free;
    // Address to beginning of memory managed by this region
    uintptr_t begin;
};

// Linked list container with pointers to the first page in the arena (`start`) and the one
// currently in use (`current`)
typedef struct arena_t
{
    region_t *start;
    region_t *current;
    size_t region_size;
    size_t region_count;
} arena_t;

// Aligns a pointer `addr` up to the closest address that is aligned with `align`, assuming of
// course that `align` is a power of two.
#define align_up(addr, align) ((addr) + ((align - 1)) & ~((align - 1)))

void region_init(region_t *region, size_t cap, uintptr_t buf)
{
    region->next = NULL;
    region->cap = cap;
    region->free = buf;
    region->begin = buf;
}

void *region_alloc(region_t *region, size_t size, size_t align)
{
    // Get an `align` aligned address from the current free pointer
    uintptr_t start = align_up(region->free, align);

    // if that address + the requested size would be larger than the capacity that we own, return
    // NULL
    if (start + size > region->begin + region->cap)
    {
        return NULL;
    }

    uintptr_t offset = start - region->free;

    region->free = region->free + offset + size;
    return (void *)start;
}

bool region_has_space(region_t *region, size_t size, size_t align)
{
    uintptr_t start = align_up(region->free, align);
    if (start + size > region->begin + region->cap)
    {
        return false;
    }

    return true;
}

void region_release(region_t *region, size_t size)
{
    munmap(region, size);
}

region_t *alloc_new_region(size_t size)
{
    void *page = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(page != MAP_FAILED && "Could not map page of memory");

    // cast the first bit of the page as a region.  we don't have to care about alignment because
    // we know that *page is page aligned.
    region_t *region = (region_t *)page;
    uintptr_t start = (uintptr_t)page + sizeof(region_t);
    // offset from start of region buffer to beginning of page
    uintptr_t offset = start - (uintptr_t)page;
    region_init(region, size - offset, start);

    return region;
}

void arena_init(arena_t *a, size_t region_size)
{
    a->region_size = region_size;
    region_t *region = alloc_new_region(a->region_size);
    a->start = region;
    a->current = region;
    a->region_count = 1;
}

void *arena_alloc(arena_t *a, size_t size, size_t align)
{
    if (a->current == NULL)
    {
        return NULL;
    }

    // Anything larger than a region is fundamentally not allocatable
    if (size > a->region_size - sizeof(region_t))
    {
        return NULL;
    }

    if (region_has_space(a->current, size, align))
    {
        return region_alloc(a->current, size, align);
    }

    // the current region does not have space, so we need to allocate a new one:
    region_t *next_region = alloc_new_region(a->region_size);
    a->current->next = next_region;
    a->current = next_region;
    a->region_count++;

    return region_alloc(a->current, size, align);
}

void arena_release(arena_t *a)
{
    region_t *ptr = a->start;
    while (ptr != NULL)
    {
        region_t *next = ptr->next;
        region_release(ptr, a->region_size);
        ptr = next;
    }

    a->start = NULL;
    a->current = NULL;
    a->region_count = 0;
}

/*
 *************************************************************************************************
 * Arena Tests
 *************************************************************************************************
 */

#define DEBUG 1

#if defined(DEBUG) && DEBUG > 0
#define print_addr(addr) (fprintf(stderr, "%s:%d: 0x%016lx\n", __FILE__, __LINE__, (uintptr_t)addr));
#else
#define print_addr(...)
#endif

void test_region()
{
    // create a region from some block of memory
    char buf[10];
    print_addr(&buf);
    // create a region from said bloce
    region_t region;
    region_init(&region, sizeof(typeof(buf)), (uintptr_t)&buf);
    // allocate 3 bytes from the current region, 4 byte aligned
    void *mem = region_alloc(&region, 3, 4);
    print_addr(mem);

    // allocate 3 more bytes, 4 bytes aligned:
    void *next_mem = region_alloc(&region, 3, 4);
    print_addr(next_mem);
    assert((uintptr_t)next_mem == (uintptr_t)mem + 4);

    // allocate 3 more bytes, 4 bytes aligned (should run out of memory here):
    void *next_mem2 = region_alloc(&region, 3, 4);
    print_addr(next_mem2);
    assert((uintptr_t)next_mem2 == 0);

    printf("✅ test_region\n");
}

void test_arena()
{
    arena_t arena;
    // give each region 8 bytes more than the header block, for testing.  Normally this should be
    // the page size of your OS:
    arena_init(&arena, sizeof(region_t) + 8);
    printf("sizeof(region_t): %lu\n", sizeof(region_t));
    print_addr(arena.start);
    // first we allocate 5 bytes with an align of 8, which should occupy 8 bytes in the block
    void *mem = arena_alloc(&arena, 5, 8);
    print_addr(mem);
    assert(arena.region_count == 1);

    // then we allocate ANOTHER 5 bytes with an align of 8, which should cause a new block to be
    // allocated
    void *mem2 = arena_alloc(&arena, 5, 8);
    // On my ARM macbook this should be 1 16KB page away from the previous address
    print_addr(mem2);
    assert(arena.region_count == 2);

    // make sure that really big stuff can't be allocated at all, but that we shouldn't get
    // a new page because of it:
    assert(arena_alloc(&arena, 9, 16) == NULL);
    assert(arena.region_count == 2);

    // finally release all memory in the arena and check de-init
    arena_release(&arena);
    assert(arena.region_count == 0);
    assert(arena_alloc(&arena, 2, 2) == NULL);
    printf("✅ test_arena\n");
}

int main()
{
    test_region();
    test_arena();
}
