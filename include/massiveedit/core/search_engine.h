#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace massiveedit::core {

struct SearchOptions {
  bool case_sensitive = false;
  bool regex = false;
};

struct SearchMatch {
  std::uint64_t offset = 0;
  std::uint32_t length = 0;
};

class SearchEngine {
 public:
  [[nodiscard]] std::vector<SearchMatch> findAll(std::string_view haystack,
                                                 std::string_view needle,
                                                 const SearchOptions& options,
                                                 std::size_t max_matches = 0) const;
};

}  // namespace massiveedit::core

