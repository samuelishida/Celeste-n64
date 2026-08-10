#pragma once

#include <cstdint>
#include <cstddef>

namespace madeline_cube {

// Deterministic content hash (CRC32) over an artifact or the source map.
// The writer (tools/artifact_hash.py) records hash + byte size; the N64
// loader uses this same implementation to reject stale/truncated DFS files.
// The runtime contract does not depend on a host-only SHA-256.
namespace artifact_hash {

// CRC32 of a byte buffer (matches zlib.crc32 in tools/artifact_hash.py).
uint32_t Crc32(const uint8_t* data, size_t len);

// CRC32 of a file's contents. Returns false on read error.
bool Crc32File(const char* path, uint32_t& out_crc, uint32_t& out_size);

}  // namespace artifact_hash
}  // namespace madeline_cube
