#include "massiveedit/core/line_indexer.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace massiveedit::core {

void LineIndexer::reset(std::uint64_t document_size) {
  line_starts_.clear();
  line_starts_.push_back(0);
  document_size_ = document_size;
  scan_offset_ = 0;
  complete_ = (document_size_ == 0);
}

bool LineIndexer::ensureLineIndexed(std::size_t line_index,
                                    const Reader& reader,
                                    std::size_t chunk_size,
                                    std::uint64_t max_scan_bytes) {
  if (line_starts_.empty()) {
    reset(document_size_);
  }
  if (line_index < line_starts_.size() || complete_) {
    return line_index < line_starts_.size();
  }
  if (!reader || chunk_size == 0) {
    return false;
  }

  std::uint64_t scanned_this_call = 0;
  while (!complete_ && line_starts_.size() <= line_index) {
    if (scan_offset_ >= document_size_) {
      complete_ = true;
      break;
    }

    const std::size_t scan_len = static_cast<std::size_t>(
        std::min<std::uint64_t>(chunk_size, document_size_ - scan_offset_));
    std::string bytes = reader(scan_offset_, scan_len);
    if (bytes.empty()) {
      complete_ = true;
      break;
    }

    for (std::size_t i = 0; i < bytes.size(); ++i) {
      if (bytes[i] == '\n') {
        line_starts_.push_back(scan_offset_ + static_cast<std::uint64_t>(i + 1));
      }
    }

    scan_offset_ += bytes.size();
    scanned_this_call += static_cast<std::uint64_t>(bytes.size());
    if (max_scan_bytes > 0 && scanned_this_call >= max_scan_bytes) {
      break;
    }
    if (bytes.size() < scan_len) {
      complete_ = true;
    }
  }

  if (scan_offset_ >= document_size_) {
    complete_ = true;
  }

  return line_index < line_starts_.size();
}

bool LineIndexer::ensureOffsetIndexed(std::uint64_t offset,
                                      const Reader& reader,
                                      std::size_t chunk_size,
                                      std::uint64_t max_scan_bytes) {
  if (line_starts_.empty()) {
    reset(document_size_);
  }
  if (complete_) {
    return true;
  }
  if (!reader || chunk_size == 0) {
    return false;
  }

  const std::uint64_t target = std::min<std::uint64_t>(offset, document_size_);
  std::uint64_t scanned_this_call = 0;
  while (!complete_ && scan_offset_ <= target) {
    if (scan_offset_ >= document_size_) {
      complete_ = true;
      break;
    }

    const std::size_t scan_len = static_cast<std::size_t>(
        std::min<std::uint64_t>(chunk_size, document_size_ - scan_offset_));
    std::string bytes = reader(scan_offset_, scan_len);
    if (bytes.empty()) {
      complete_ = true;
      break;
    }

    for (std::size_t i = 0; i < bytes.size(); ++i) {
      if (bytes[i] == '\n') {
        line_starts_.push_back(scan_offset_ + static_cast<std::uint64_t>(i + 1));
      }
    }

    scan_offset_ += bytes.size();
    scanned_this_call += static_cast<std::uint64_t>(bytes.size());
    if (max_scan_bytes > 0 && scanned_this_call >= max_scan_bytes) {
      break;
    }
    if (bytes.size() < scan_len) {
      complete_ = true;
    }
  }

  if (scan_offset_ >= document_size_) {
    complete_ = true;
  }

  return complete_ || scan_offset_ > target;
}

bool LineIndexer::indexNextChunk(const Reader& reader, std::size_t chunk_size) {
  if (line_starts_.empty()) {
    reset(document_size_);
  }
  if (complete_) {
    return false;
  }
  if (!reader || chunk_size == 0) {
    return false;
  }
  if (scan_offset_ >= document_size_) {
    complete_ = true;
    return false;
  }

  const std::size_t scan_len = static_cast<std::size_t>(
      std::min<std::uint64_t>(chunk_size, document_size_ - scan_offset_));
  std::string bytes = reader(scan_offset_, scan_len);
  if (bytes.empty()) {
    complete_ = true;
    return false;
  }

  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (bytes[i] == '\n') {
      line_starts_.push_back(scan_offset_ + static_cast<std::uint64_t>(i + 1));
    }
  }

  scan_offset_ += bytes.size();
  if (scan_offset_ >= document_size_ || bytes.size() < scan_len) {
    complete_ = true;
  }
  return true;
}

void LineIndexer::ensureComplete(const Reader& reader, std::size_t chunk_size) {
  if (complete_) {
    return;
  }
  const std::size_t target = std::numeric_limits<std::size_t>::max();
  (void)ensureLineIndexed(target, reader, chunk_size);
}

bool LineIndexer::isComplete() const {
  return complete_;
}

bool LineIndexer::isOffsetIndexed(std::uint64_t offset) const {
  if (complete_) {
    return true;
  }
  return offset < scan_offset_;
}

std::uint64_t LineIndexer::scannedByteOffset() const {
  return scan_offset_;
}

std::size_t LineIndexer::knownLineCount() const {
  return line_starts_.size();
}

std::size_t LineIndexer::estimatedLineCount() const {
  if (complete_) {
    return line_starts_.size();
  }
  if (line_starts_.size() <= 1 || scan_offset_ == 0) {
    return static_cast<std::size_t>(std::max<std::uint64_t>(1, document_size_ / 80 + 1));
  }

  const double avg_bytes_per_line = static_cast<double>(scan_offset_) /
                                    static_cast<double>(line_starts_.size() - 1);
  if (avg_bytes_per_line <= 0.0001) {
    return line_starts_.size();
  }

  const std::size_t estimated =
      static_cast<std::size_t>(static_cast<double>(document_size_) / avg_bytes_per_line) + 1;
  return std::max(line_starts_.size(), estimated);
}

std::uint64_t LineIndexer::lineStart(std::size_t line) const {
  if (line_starts_.empty()) {
    return 0;
  }

  const std::size_t clamped = std::min(line, line_starts_.size() - 1);
  return line_starts_[clamped];
}

std::size_t LineIndexer::lineIndexForOffset(std::uint64_t offset) const {
  if (line_starts_.empty()) {
    return 0;
  }

  const std::uint64_t clamped = std::min<std::uint64_t>(offset, document_size_);
  const auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), clamped);
  if (it == line_starts_.begin()) {
    return 0;
  }

  return static_cast<std::size_t>(std::distance(line_starts_.begin(), it) - 1);
}

}  // namespace massiveedit::core
