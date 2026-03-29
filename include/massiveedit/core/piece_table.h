#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace massiveedit::core {

class PieceTable {
 public:
  using Offset = std::uint64_t;
  using OriginalReader = std::function<std::string(Offset, std::size_t)>;

  void clear();
  void load(std::string text);
  void loadFromOriginalSize(Offset original_size);
  void insert(Offset offset, std::string_view text);
  void erase(Offset offset, Offset length);

  [[nodiscard]] std::string read(Offset offset,
                                 Offset length,
                                 const OriginalReader& original_reader = nullptr) const;
  [[nodiscard]] std::string toString(const OriginalReader& original_reader = nullptr) const;
  [[nodiscard]] Offset size() const;

 private:
  enum class Source {
    kOriginal,
    kAdd
  };

  struct Piece {
    Source source = Source::kOriginal;
    Offset start = 0;
    Offset length = 0;
  };

  void coalesce();

  std::string add_buffer_;
  std::vector<Piece> pieces_;
};

}  // namespace massiveedit::core
