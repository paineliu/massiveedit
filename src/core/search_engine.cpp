#include "massiveedit/core/search_engine.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>

namespace massiveedit::core {
namespace {

char lowerChar(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

}  // namespace

std::vector<SearchMatch> SearchEngine::findAll(std::string_view haystack,
                                               std::string_view needle,
                                               const SearchOptions& options,
                                               std::size_t max_matches) const {
  std::vector<SearchMatch> matches;
  if (needle.empty() || haystack.empty()) {
    return matches;
  }

  if (options.regex) {
    try {
      const std::regex::flag_type flags =
          options.case_sensitive ? std::regex::ECMAScript
                                 : (std::regex::ECMAScript | std::regex::icase);
      const std::regex re(std::string(needle), flags);
      const std::string text(haystack);

      for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) {
        const auto& hit = *it;
        matches.push_back(SearchMatch{
            .offset = static_cast<std::uint64_t>(hit.position()),
            .length = static_cast<std::uint32_t>(hit.length()),
        });
        if (max_matches > 0 && matches.size() >= max_matches) {
          break;
        }
      }
      return matches;
    } catch (const std::regex_error&) {
      return {};
    }
  }

  std::string text(haystack);
  std::string key(needle);
  if (!options.case_sensitive) {
    std::transform(text.begin(), text.end(), text.begin(), lowerChar);
    std::transform(key.begin(), key.end(), key.begin(), lowerChar);
  }

  std::size_t pos = 0;
  while (pos < text.size()) {
    pos = text.find(key, pos);
    if (pos == std::string::npos) {
      break;
    }
    matches.push_back(SearchMatch{
        .offset = static_cast<std::uint64_t>(pos),
        .length = static_cast<std::uint32_t>(key.size()),
    });
    ++pos;

    if (max_matches > 0 && matches.size() >= max_matches) {
      break;
    }
  }

  return matches;
}

}  // namespace massiveedit::core
