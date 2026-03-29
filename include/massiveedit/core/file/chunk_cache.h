#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

#include "massiveedit/core/file/large_file_backend.h"

namespace massiveedit::core::file {

class ChunkCache {
 public:
  struct Stats {
    std::uint64_t reads = 0;
    std::uint64_t cache_hits = 0;
    std::uint64_t cache_misses = 0;
    std::uint64_t bytes_served = 0;
    std::size_t cached_bytes = 0;
    std::size_t cached_chunks = 0;
  };

  explicit ChunkCache(std::size_t chunk_size = 1ULL * 1024 * 1024,
                      std::size_t max_cache_bytes = 256ULL * 1024 * 1024);

  void setBackend(const LargeFileBackend* backend);
  void clear();
  [[nodiscard]] std::string read(std::uint64_t offset, std::size_t length) const;
  [[nodiscard]] Stats stats() const;
  void resetStats();

 private:
  struct ChunkEntry {
    std::string bytes;
    std::list<std::uint64_t>::iterator lru_it;
  };

  void touch(const std::uint64_t chunk_index) const;
  const ChunkEntry* ensureChunk(std::uint64_t chunk_index) const;
  void evictIfNeeded() const;

  std::size_t chunk_size_;
  std::size_t max_cache_bytes_;
  const LargeFileBackend* backend_ = nullptr;

  mutable std::size_t current_bytes_ = 0;
  mutable std::unordered_map<std::uint64_t, ChunkEntry> cache_;
  mutable std::list<std::uint64_t> lru_;
  mutable std::mutex mutex_;
  mutable Stats stats_;
};

}  // namespace massiveedit::core::file
