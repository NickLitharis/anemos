#include "limine_requests.h"

#define LIMINE_COMMON_MAGIC_0 0xc7b1dd30df4c8b88ull
#define LIMINE_COMMON_MAGIC_1 0x0a82e883a194f07bull
#define LIMINE_BASE_REVISION 6ull

struct limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_memmap_response *response;
};

struct limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_hhdm_response *response;
};

struct limine_paging_mode_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_paging_mode_response *response;
    uint64_t mode;
    uint64_t max_mode;
    uint64_t min_mode;
};

__attribute__((used, section(".limine_requests_start"), aligned(8)))
static volatile uint64_t request_start[] = {
    0xf6b8f4b39de7d1aeull,
    0xfab91a6940fcb9cfull,
    0x785c6ed015d3e316ull,
    0x181e920a7852b9d9ull
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile uint64_t base_revision[] = {
    0xf9562b2d5c95a6c8ull,
    0x6a7b384944536bdcull,
    LIMINE_BASE_REVISION
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_memmap_request memory_map_request = {
    .id = {
        LIMINE_COMMON_MAGIC_0,
        LIMINE_COMMON_MAGIC_1,
        0x67cf3d9d378a806full,
        0xe304acdfc50c3c62ull
    }
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = {
        LIMINE_COMMON_MAGIC_0,
        LIMINE_COMMON_MAGIC_1,
        0x48dcf1cb8ad2b852ull,
        0x63984e959a98244bull
    }
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_paging_mode_request paging_mode_request = {
    .id = {
        LIMINE_COMMON_MAGIC_0,
        LIMINE_COMMON_MAGIC_1,
        0x95c1a0edab0944cbull,
        0xa4e5cb3842f7488aull
    },
    .mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .max_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL
};

__attribute__((used, section(".limine_requests_end"), aligned(8)))
static volatile uint64_t request_end[] = {
    0xadc0e0531bb10d03ull,
    0x9572709f31764c62ull
};

struct limine_memmap_response *limine_memory_map(void)
{
    return memory_map_request.response;
}

struct limine_hhdm_response *limine_hhdm(void)
{
    return hhdm_request.response;
}

struct limine_paging_mode_response *limine_paging_mode(void)
{
    return paging_mode_request.response;
}

int limine_base_revision_supported(void)
{
    return base_revision[2] == 0u;
}
