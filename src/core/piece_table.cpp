#include "massiveedit/core/piece_table.h"

#include <algorithm>
#include <cstddef>

namespace massiveedit::core {

void PieceTable::clear() {
  add_buffer_.clear();
  pieces_.clear();
}

void PieceTable::load(std::string text) {
  clear();
  if (text.empty()) {
    return;
  }

  add_buffer_ = std::move(text);
  pieces_.push_back(Piece{
      .source = Source::kAdd,
      .start = 0,
      .length = static_cast<Offset>(add_buffer_.size()),
  });
}

void PieceTable::loadFromOriginalSize(Offset original_size) {
  add_buffer_.clear();
  pieces_.clear();
  if (original_size > 0) {
    pieces_.push_back(Piece{
        .source = Source::kOriginal,
        .start = 0,
        .length = original_size,
    });
  }
}

void PieceTable::insert(Offset offset, std::string_view text) {
  if (text.empty()) {
    return;
  }

  offset = std::min(offset, size());
  const Offset add_start = static_cast<Offset>(add_buffer_.size());
  add_buffer_.append(text.data(), text.size());

  const Piece inserted{
      .source = Source::kAdd,
      .start = add_start,
      .length = static_cast<Offset>(text.size()),
  };

  if (pieces_.empty()) {
    pieces_.push_back(inserted);
    return;
  }

  Offset cursor = 0;
  for (std::size_t i = 0; i < pieces_.size(); ++i) {
    const Piece current = pieces_[i];
    const Offset piece_end = cursor + current.length;

    if (offset > piece_end) {
      cursor = piece_end;
      continue;
    }

    const Offset local = offset - cursor;
    if (local == 0) {
      pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(i), inserted);
    } else if (local == current.length) {
      pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(i + 1), inserted);
    } else {
      pieces_[i].length = local;
      Piece right = current;
      right.start += local;
      right.length -= local;
      pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(i + 1), inserted);
      pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(i + 2), right);
    }

    coalesce();
    return;
  }

  pieces_.push_back(inserted);
  coalesce();
}

void PieceTable::erase(Offset offset, Offset length) {
  if (length == 0 || offset >= size() || pieces_.empty()) {
    return;
  }

  const Offset erase_end = std::min<Offset>(offset + length, size());
  std::vector<Piece> retained;
  retained.reserve(pieces_.size());

  Offset cursor = 0;
  for (const Piece& piece : pieces_) {
    const Offset piece_start = cursor;
    const Offset piece_end = cursor + piece.length;
    cursor = piece_end;

    if (piece_end <= offset || piece_start >= erase_end) {
      retained.push_back(piece);
      continue;
    }

    const Offset left_len = (offset > piece_start) ? (offset - piece_start) : 0;
    if (left_len > 0) {
      retained.push_back(Piece{
          .source = piece.source,
          .start = piece.start,
          .length = left_len,
      });
    }

    const Offset right_start_global = std::max(piece_start, erase_end);
    if (right_start_global < piece_end) {
      const Offset skip = right_start_global - piece_start;
      retained.push_back(Piece{
          .source = piece.source,
          .start = piece.start + skip,
          .length = piece_end - right_start_global,
      });
    }
  }

  pieces_ = std::move(retained);
  coalesce();
}

std::string PieceTable::read(Offset offset,
                            Offset length,
                            const OriginalReader& original_reader) const {
  if (length == 0 || offset >= size()) {
    return {};
  }

  const Offset read_end = std::min<Offset>(offset + length, size());
  std::string out;
  out.reserve(static_cast<std::size_t>(read_end - offset));

  Offset cursor = 0;
  for (const Piece& piece : pieces_) {
    const Offset piece_start = cursor;
    const Offset piece_end = cursor + piece.length;
    cursor = piece_end;

    if (piece_end <= offset || piece_start >= read_end) {
      continue;
    }

    const Offset local_start = std::max(offset, piece_start) - piece_start;
    const Offset local_end = std::min(read_end, piece_end) - piece_start;
    const Offset local_len = local_end - local_start;

    const Offset source_offset = piece.start + local_start;
    if (piece.source == Source::kAdd) {
      out.append(add_buffer_.data() + static_cast<std::size_t>(source_offset),
                 static_cast<std::size_t>(local_len));
      continue;
    }

    if (!original_reader) {
      continue;
    }

    std::string part = original_reader(source_offset, static_cast<std::size_t>(local_len));
    if (part.size() > static_cast<std::size_t>(local_len)) {
      part.resize(static_cast<std::size_t>(local_len));
    }
    out.append(part);
  }

  return out;
}

std::string PieceTable::toString(const OriginalReader& original_reader) const {
  std::string out;
  out.reserve(static_cast<std::size_t>(size()));

  for (const Piece& piece : pieces_) {
    if (piece.source == Source::kAdd) {
      out.append(add_buffer_.data() + static_cast<std::size_t>(piece.start),
                 static_cast<std::size_t>(piece.length));
      continue;
    }

    if (!original_reader) {
      continue;
    }

    std::string part = original_reader(piece.start, static_cast<std::size_t>(piece.length));
    if (part.size() > static_cast<std::size_t>(piece.length)) {
      part.resize(static_cast<std::size_t>(piece.length));
    }
    out.append(part);
  }

  return out;
}

PieceTable::Offset PieceTable::size() const {
  Offset total = 0;
  for (const Piece& piece : pieces_) {
    total += piece.length;
  }
  return total;
}

void PieceTable::coalesce() {
  if (pieces_.empty()) {
    return;
  }

  std::vector<Piece> merged;
  merged.reserve(pieces_.size());
  merged.push_back(pieces_.front());

  for (std::size_t i = 1; i < pieces_.size(); ++i) {
    const Piece& current = pieces_[i];
    Piece& last = merged.back();

    if (last.source == current.source && last.start + last.length == current.start) {
      last.length += current.length;
      continue;
    }

    merged.push_back(current);
  }

  pieces_ = std::move(merged);
}

}  // namespace massiveedit::core
