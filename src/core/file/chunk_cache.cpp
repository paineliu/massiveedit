#include "massiveedit/core/file/chunk_cache.h"

#include <algorithm>

namespace massiveedit::core::file {

ChunkCache::ChunkCache(std::size_t chunk_size, std::size_t max_cache_bytes)
    : chunk_size_(chunk_size), max_cache_bytes_(max_cache_bytes) {}

void ChunkCache::setBackend(const LargeFileBackend* backend) {
  std::lock_guard<std::mutex> lock(mutex_);
  backend_ = backend;
  current_bytes_ = 0;
  cache_.clear();
  lru_.clear();
  stats_.cached_bytes = 0;
  stats_.cached_chunks = 0;
}

void ChunkCache::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  current_bytes_ = 0;
  cache_.clear();
  lru_.clear();
  stats_.cached_bytes = 0;
  stats_.cached_chunks = 0;
}

std::string ChunkCache::read(std::uint64_t offset, std::size_t length) const {
  std::lock_guard<std::mutex> lock(mutex_);
  ++stats_.reads;
  if (backend_ == nullptr || !backend_->isOpen() || length == 0 || offset >= backend_->size()) {
    return {};
  }

  const std::uint64_t available = backend_->size() - offset;
  std::size_t remaining = static_cast<std::size_t>(std::min<std::uint64_t>(available, length));
  std::uint64_t cursor = offset;

  std::string out;
  out.reserve(remaining);

  while (remaining > 0) {
    const std::uint64_t chunk_index = cursor / chunk_size_;
    const std::size_t in_chunk = static_cast<std::size_t>(cursor % chunk_size_);
    const ChunkEntry* entry = ensureChunk(chunk_index);
    if (entry == nullptr || in_chunk >= entry->bytes.size()) {
      break;
    }

    const std::size_t can_take = std::min(remaining, entry->bytes.size() - in_chunk);
    out.append(entry->bytes.data() + in_chunk, can_take);
    stats_.bytes_served += static_cast<std::uint64_t>(can_take);
    cursor += can_take;
    remaining -= can_take;
  }

  return out;
}

ChunkCache::Stats ChunkCache::stats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Stats copy = stats_;
  copy.cached_bytes = current_bytes_;
  copy.cached_chunks = cache_.size();
  return copy;
}

void ChunkCache::resetStats() {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::size_t cached_bytes = current_bytes_;
  const std::size_t cached_chunks = cache_.size();
  stats_ = {};
  stats_.cached_bytes = cached_bytes;
  stats_.cached_chunks = cached_chunks;
}

void ChunkCache::touch(const std::uint64_t chunk_index) const {
  auto it = cache_.find(chunk_index);
  if (it == cache_.end()) {
    return;
  }

  lru_.erase(it->second.lru_it);
  lru_.push_front(chunk_index);
  it->second.lru_it = lru_.begin();
}

const ChunkCache::ChunkEntry* ChunkCache::ensureChunk(std::uint64_t chunk_index) const {
  auto hit = cache_.find(chunk_index);
  if (hit != cache_.end()) {
    ++stats_.cache_hits;
    touch(chunk_index);
    return &hit->second;
  }
  ++stats_.cache_misses;

  if (backend_ == nullptr || !backend_->isOpen()) {
    return nullptr;
  }

  const std::uint64_t chunk_offset = chunk_index * chunk_size_;
  const std::string bytes = backend_->read(chunk_offset, chunk_size_);
  if (bytes.empty()) {
    return nullptr;
  }

  lru_.push_front(chunk_index);
  ChunkEntry entry{
      .bytes = bytes,
      .lru_it = lru_.begin(),
  };
  current_bytes_ += entry.bytes.size();
  auto [it, inserted] = cache_.emplace(chunk_index, std::move(entry));
  if (!inserted) {
    lru_.erase(it->second.lru_it);
    lru_.push_front(chunk_index);
    current_bytes_ -= it->second.bytes.size();
    it->second.bytes = bytes;
    it->second.lru_it = lru_.begin();
    current_bytes_ += it->second.bytes.size();
  }

  evictIfNeeded();
  stats_.cached_bytes = current_bytes_;
  stats_.cached_chunks = cache_.size();

  auto now = cache_.find(chunk_index);
  return now == cache_.end() ? nullptr : &now->second;
}

void ChunkCache::evictIfNeeded() const {
  while (current_bytes_ > max_cache_bytes_ && !lru_.empty()) {
    const std::uint64_t evict_key = lru_.back();
    lru_.pop_back();

    auto it = cache_.find(evict_key);
    if (it == cache_.end()) {
      continue;
    }

    current_bytes_ -= it->second.bytes.size();
    cache_.erase(it);
  }
  stats_.cached_bytes = current_bytes_;
  stats_.cached_chunks = cache_.size();
}

}  // namespace massiveedit::core::file
