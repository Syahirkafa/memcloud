#ifndef MEMCLOUD_H
#define MEMCLOUD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the MemCloud SDK. Returns 0 on success.
int memcloud_init();

// Store data. 
// data: pointer to data to store.
// size: size of data in bytes.
// out_id: pointer to uint64_t to receive the block ID.
// Returns 0 on success, non-zero on error.
int memcloud_store(const void* data, size_t size, uint64_t* out_id);

// Load data.
// id: block ID to load.
// out_buffer: pointer to buffer to write data to.
// buffer_size: size of out_buffer.
// Returns bytes read on success, negative on error.
int memcloud_load(uint64_t id, void* out_buffer, size_t buffer_size);

// Free data.
// Returns 0 on success.
int memcloud_free(uint64_t id);

#ifdef __cplusplus
}
#endif

#endif // MEMCLOUD_H
