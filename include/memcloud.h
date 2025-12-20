#ifndef MEMCLOUD_H
#define MEMCLOUD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int memcloud_init();

int memcloud_store(const void* data, size_t size, uint64_t* out_id);

int memcloud_load(uint64_t id, void* out_buffer, size_t buffer_size);

int memcloud_free(uint64_t id);

int memcloud_vm_alloc(uint64_t size, uint64_t* out_region_id);
int memcloud_vm_fetch(uint64_t region_id, uint64_t page_index, void* out_buffer, size_t buffer_size);
int memcloud_vm_store(uint64_t region_id, uint64_t page_index, const void* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif // MEMCLOUD_H
