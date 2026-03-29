#include "massiveedit/core/document_session.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

#include <QMetaObject>
#include <QFile>
#include <QDataStream>
#include <QSaveFile>
#include <QStringConverter>

namespace massiveedit::core {
namespace {

std::size_t cacheBytesFromEnv() {
  constexpr std::size_t kDefaultCacheBytes = 256ULL * 1024 * 1024;
  const char* env = std::getenv("MASSIVEEDIT_CACHE_MB");
  if (env == nullptr || *env == '\0') {
    return kDefaultCacheBytes;
  }

  char* end = nullptr;
  const unsigned long long value = std::strtoull(env, &end, 10);
  if (end == env || (end != nullptr && *end != '\0') || value == 0) {
    return kDefaultCacheBytes;
  }

  constexpr unsigned long long kMaxMb = 4096;
  const unsigned long long clamped = std::min<unsigned long long>(value, kMaxMb);
  return static_cast<std::size_t>(clamped * 1024ULL * 1024ULL);
}

std::optional<QStringConverter::Encoding> gbkQtEncoding() {
  const auto gb18030 = QStringConverter::encodingForName("GB18030");
  if (gb18030.has_value()) {
    return gb18030;
  }
  const auto gbk = QStringConverter::encodingForName("GBK");
  if (gbk.has_value()) {
    return gbk;
  }
  return std::nullopt;
}

constexpr quint32 kOperationLogMagic = 0x4D454F4C;    // MEOL
constexpr quint32 kOperationLogVersion = 1;

}  // namespace

DocumentSession::DocumentSession(QObject* parent)
    : QObject(parent),
      chunk_cache_(1ULL * 1024 * 1024, cacheBytesFromEnv()),
      search_pool_() {
  index_thread_ = std::thread([this]() { indexWorkerLoop(); });
}

DocumentSession::~DocumentSession() {
  cancelAllSearches();
  {
    std::lock_guard<std::mutex> lock(index_mutex_);
    stop_index_thread_ = true;
    ++index_generation_;
  }
  index_cv_.notify_all();
  if (index_thread_.joinable()) {
    index_thread_.join();
  }
}

bool DocumentSession::openFile(const QString& file_path, QString* error) {
  cancelAllSearches();

  std::string native_error;
  if (!backend_.open(file_path.toStdString(), &native_error)) {
    if (error != nullptr) {
      *error = QString::fromStdString(native_error);
    }
    return false;
  }

  {
    std::unique_lock<std::shared_mutex> lock(model_mutex_);
    source_path_ = backend_.path();
    chunk_cache_.setBackend(&backend_);
    piece_table_.loadFromOriginalSize(backend_.size());
    detectTextEncodingAndLineEndingLocked();
    resetLineIndexLocked();
    dirty_ = false;
    document_revision_ = 0;
    operation_log_base_is_file_ = true;
    operation_log_base_path_ = source_path_;
    operation_log_base_content_.clear();
    operation_log_ops_.clear();
    replaying_operation_log_ = false;
    undo_stack_.clear();
    redo_stack_.clear();
    active_transaction_.reset();
  }

  {
    std::lock_guard<std::mutex> lock(search_mutex_);
    completed_search_results_.clear();
  }

  emitChangedAndUndoState();
  return true;
}

bool DocumentSession::openFromBytes(const QByteArray& bytes,
                                    const QString& source_label,
                                    bool mark_dirty) {
  cancelAllSearches();

  {
    std::unique_lock<std::shared_mutex> lock(model_mutex_);
    source_path_ = source_label.isEmpty() ? std::filesystem::path()
                                          : std::filesystem::path(source_label.toStdString());
    chunk_cache_.setBackend(nullptr);
    piece_table_.loadFromOriginalSize(0);
    if (!bytes.isEmpty()) {
      piece_table_.insert(0,
                          std::string(bytes.constData(),
                                      static_cast<std::size_t>(bytes.size())));
    }
    detectTextEncodingAndLineEndingLocked();
    resetLineIndexLocked();
    dirty_ = mark_dirty;
    document_revision_ = mark_dirty ? 1 : 0;
    operation_log_base_is_file_ = false;
    operation_log_base_path_.clear();
    operation_log_base_content_.assign(bytes.constData(),
                                       static_cast<std::size_t>(bytes.size()));
    operation_log_ops_.clear();
    replaying_operation_log_ = false;
    undo_stack_.clear();
    redo_stack_.clear();
    active_transaction_.reset();
  }

  {
    std::lock_guard<std::mutex> lock(search_mutex_);
    completed_search_results_.clear();
  }

  emitChangedAndUndoState();
  return true;
}

bool DocumentSession::saveAs(const QString& file_path, QString* error) {
  std::ofstream out(file_path.toStdString(), std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = QStringLiteral("Failed to open output path for writing.");
    }
    return false;
  }

  constexpr std::size_t kWriteChunk = 4ULL * 1024 * 1024;
  const std::uint64_t total_size = byteSize();
  std::uint64_t offset = 0;
  while (offset < total_size) {
    const std::size_t take =
        static_cast<std::size_t>(std::min<std::uint64_t>(kWriteChunk, total_size - offset));
    const std::string chunk = readBytes(offset, take);
    if (chunk.size() != take) {
      if (error != nullptr) {
        *error = QStringLiteral("Failed while reading document content for save.");
      }
      return false;
    }

    out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    if (!out.good()) {
      if (error != nullptr) {
        *error = QStringLiteral("Failed while writing output file.");
      }
      return false;
    }

    offset += static_cast<std::uint64_t>(chunk.size());
  }

  {
    std::unique_lock<std::shared_mutex> lock(model_mutex_);
    source_path_ = std::filesystem::path(file_path.toStdString());
    dirty_ = false;
    operation_log_base_is_file_ = true;
    operation_log_base_path_ = source_path_;
    operation_log_base_content_.clear();
    operation_log_ops_.clear();
    replaying_operation_log_ = false;
  }

  return true;
}

bool DocumentSession::saveOperationLog(const QString& log_path, QString* error) const {
  if (log_path.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Operation log path is empty.");
    }
    return false;
  }

  QSaveFile out(log_path);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error != nullptr) {
      *error = QStringLiteral("Failed to open operation log for writing.");
    }
    return false;
  }

  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  QDataStream stream(&out);
  stream.setVersion(QDataStream::Qt_6_0);
  stream << static_cast<quint32>(kOperationLogMagic);
  stream << static_cast<quint32>(kOperationLogVersion);
  stream << operation_log_base_is_file_;
  stream << static_cast<qint32>(text_encoding_);
  stream << static_cast<qint32>(line_ending_);
  if (operation_log_base_is_file_) {
    stream << QString::fromStdString(operation_log_base_path_.string());
  } else {
    stream << QByteArray(operation_log_base_content_.data(),
                         static_cast<qsizetype>(operation_log_base_content_.size()));
  }
  stream << static_cast<quint64>(operation_log_ops_.size());
  for (const EditOperation& op : operation_log_ops_) {
    stream << static_cast<qint32>(op.kind);
    stream << static_cast<quint64>(op.offset);
    stream << QByteArray(op.text.data(), static_cast<qsizetype>(op.text.size()));
  }

  if (stream.status() != QDataStream::Ok) {
    out.cancelWriting();
    if (error != nullptr) {
      *error = QStringLiteral("Failed while serializing operation log.");
    }
    return false;
  }
  if (!out.commit()) {
    if (error != nullptr) {
      *error = QStringLiteral("Failed to commit operation log file.");
    }
    return false;
  }
  return true;
}

bool DocumentSession::restoreFromOperationLog(const QString& log_path, QString* error) {
  if (log_path.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Operation log path is empty.");
    }
    return false;
  }

  QFile in(log_path);
  if (!in.open(QIODevice::ReadOnly)) {
    if (error != nullptr) {
      *error = QStringLiteral("Failed to open operation log for reading.");
    }
    return false;
  }

  bool base_is_file = false;
  TextEncoding log_encoding = TextEncoding::kUtf8;
  LineEnding log_line_ending = LineEnding::kLf;
  QString base_file_path;
  QByteArray base_content;
  std::vector<EditOperation> ops;

  QDataStream stream(&in);
  stream.setVersion(QDataStream::Qt_6_0);
  quint32 magic = 0;
  quint32 version = 0;
  qint32 encoding = 0;
  qint32 line_ending = 0;
  quint64 op_count = 0;
  stream >> magic;
  stream >> version;
  if (magic != kOperationLogMagic || version != kOperationLogVersion) {
    if (error != nullptr) {
      *error = QStringLiteral("Unsupported operation log format.");
    }
    return false;
  }

  stream >> base_is_file;
  stream >> encoding;
  stream >> line_ending;
  if (base_is_file) {
    stream >> base_file_path;
  } else {
    stream >> base_content;
  }
  stream >> op_count;
  ops.reserve(static_cast<std::size_t>(op_count));
  for (quint64 i = 0; i < op_count; ++i) {
    qint32 kind_raw = 0;
    quint64 offset = 0;
    QByteArray bytes;
    stream >> kind_raw;
    stream >> offset;
    stream >> bytes;
    if (kind_raw != static_cast<qint32>(EditOperation::Kind::kInsert) &&
        kind_raw != static_cast<qint32>(EditOperation::Kind::kErase)) {
      if (error != nullptr) {
        *error = QStringLiteral("Operation log contains unknown operation kind.");
      }
      return false;
    }
    ops.push_back(EditOperation{
        .kind = static_cast<EditOperation::Kind>(kind_raw),
        .offset = static_cast<std::uint64_t>(offset),
        .text = std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())),
    });
  }

  if (stream.status() != QDataStream::Ok) {
    if (error != nullptr) {
      *error = QStringLiteral("Failed while parsing operation log.");
    }
    return false;
  }

  if (encoding == static_cast<qint32>(TextEncoding::kGbk)) {
    log_encoding = TextEncoding::kGbk;
  }
  if (line_ending == static_cast<qint32>(LineEnding::kCrlf)) {
    log_line_ending = LineEnding::kCrlf;
  }

  cancelAllSearches();

  QString open_error;
  bool opened = false;
  if (base_is_file) {
    if (base_file_path.isEmpty()) {
      if (error != nullptr) {
        *error = QStringLiteral("Operation log has empty base file path.");
      }
      return false;
    }
    opened = openFile(base_file_path, &open_error);
  } else {
    opened = openFromBytes(base_content, QString(), false);
  }
  if (!opened) {
    if (error != nullptr) {
      *error = open_error.isEmpty() ? QStringLiteral("Failed to open operation log base.") : open_error;
    }
    return false;
  }

  {
    std::unique_lock<std::shared_mutex> lock(model_mutex_);
    const bool saved_read_only = read_only_;
    read_only_ = false;
    replaying_history_ = true;
    replaying_operation_log_ = true;
    for (const EditOperation& op : ops) {
      if (op.kind == EditOperation::Kind::kInsert) {
        (void)insertInternal(op.offset, op.text);
      } else {
        (void)eraseInternal(op.offset, static_cast<std::uint64_t>(op.text.size()));
      }
    }
    replaying_operation_log_ = false;
    replaying_history_ = false;
    read_only_ = saved_read_only;

    text_encoding_ = log_encoding;
    line_ending_ = log_line_ending;
    dirty_ = !ops.empty();
    document_revision_ = dirty_ ? 1 : 0;
    undo_stack_.clear();
    redo_stack_.clear();
    active_transaction_.reset();
    resetLineIndexLocked();

    operation_log_base_is_file_ = base_is_file;
    operation_log_base_path_ = std::filesystem::path(base_file_path.toStdString());
    operation_log_base_content_.assign(base_content.constData(),
                                       static_cast<std::size_t>(base_content.size()));
    operation_log_ops_ = std::move(ops);
  }

  emitChangedAndUndoState();
  return true;
}

void DocumentSession::insertText(std::uint64_t offset, const QString& text) {
  if (text.isEmpty()) {
    return;
  }

  {
    std::unique_lock<std::shared_mutex> lock(model_mutex_);
    if (read_only_) {
      return;
    }
    const QByteArray encoded = encodeTextForStorageLocked(text);
    const std::string bytes(encoded.data(), static_cast<std::size_t>(encoded.size()));
    const std::uint64_t applied_offset = insertInternal(offset, bytes);
    if (!replaying_history_) {
      recordOperation(EditOperation{
          .kind = EditOperation::Kind::kInsert,
          .offset = applied_offset,
          .text = bytes,
      });
    }
    recordOperationLogLocked(EditOperation{
        .kind = EditOperation::Kind::kInsert,
        .offset = applied_offset,
        .text = bytes,
    });
    dirty_ = true;
    ++document_revision_;
    resetLineIndexLocked();
  }

  emitChangedAndUndoState();
}

void DocumentSession::removeText(std::uint64_t offset, std::uint64_t length) {
  if (length == 0) {
    return;
  }

  {
    std::unique_lock<std::shared_mutex> lock(model_mutex_);
    if (read_only_) {
      return;
    }
    const std::string removed = removedTextForErase(offset, length);
    if (removed.empty()) {
      return;
    }

    const std::uint64_t clamped_offset = std::min(offset, piece_table_.size());
    const std::uint64_t erased =
        eraseInternal(clamped_offset, static_cast<std::uint64_t>(removed.size()));
    if (erased == 0) {
      return;
    }
    if (!replaying_history_) {
      recordOperation(EditOperation{
          .kind = EditOperation::Kind::kErase,
          .offset = clamped_offset,
          .text = removed.substr(0, static_cast<std::size_t>(erased)),
      });
    }
    recordOperationLogLocked(EditOperation{
        .kind = EditOperation::Kind::kErase,
        .offset = clamped_offset,
        .text = removed.substr(0, static_cast<std::size_t>(erased)),
    });
    dirty_ = true;
    ++document_revision_;
    resetLineIndexLocked();
  }

  emitChangedAndUndoState();
}

QString DocumentSession::filePath() const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return QString::fromStdString(source_path_.string());
}

std::uint64_t DocumentSession::byteSize() const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return piece_table_.size();
}

std::size_t DocumentSession::lineCount() const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return line_indexer_.estimatedLineCount();
}

QString DocumentSession::lineAt(std::size_t line_index) const {
  std::unique_lock<std::shared_mutex> lock(model_mutex_);
  return const_cast<DocumentSession*>(this)->lineAtLocked(line_index);
}

QStringList DocumentSession::lines(std::size_t start_line, std::size_t count) const {
  QStringList out;
  if (count == 0) {
    return out;
  }

  std::unique_lock<std::shared_mutex> lock(model_mutex_);
  for (std::size_t i = 0; i < count; ++i) {
    const QString line = const_cast<DocumentSession*>(this)->lineAtLocked(start_line + i);
    if (line.isNull()) {
      break;
    }
    out.push_back(line);
  }
  return out;
}

std::vector<DocumentSession::ViewLine> DocumentSession::viewLines(std::size_t start_line,
                                                                  std::size_t count,
                                                                  std::size_t max_bytes_per_line,
                                                                  std::size_t max_chars_per_line) const {
  std::vector<ViewLine> out;
  if (count == 0 || max_bytes_per_line == 0 || max_chars_per_line == 0) {
    return out;
  }

  out.reserve(count);
  std::unique_lock<std::shared_mutex> lock(model_mutex_);
  if (piece_table_.size() == 0) {
    return out;
  }

  const auto reader = [this](std::uint64_t offset, std::size_t length) {
    return readBytesLocked(offset, length);
  };
  LineIndexer& indexer = const_cast<DocumentSession*>(this)->line_indexer_;
  const std::size_t known_before = indexer.knownLineCount();
  if (!indexer.isComplete() && start_line + count >= known_before) {
    const std::size_t step = std::max<std::size_t>(count * 4, 1024);
    (void)indexer.ensureLineIndexed(known_before + step, reader);
  }

  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t line_index = start_line + i;
    if (line_index >= indexer.knownLineCount()) {
      break;
    }
    out.push_back(const_cast<DocumentSession*>(this)->buildViewLineLocked(
        line_index, max_bytes_per_line, max_chars_per_line));
  }
  return out;
}

bool DocumentSession::offsetForLineColumn(std::size_t line_index,
                                          std::size_t column,
                                          std::uint64_t* offset) const {
  if (offset == nullptr) {
    return false;
  }
  *offset = 0;

  std::unique_lock<std::shared_mutex> lock(model_mutex_);
  const std::uint64_t size = piece_table_.size();
  if (size == 0) {
    return line_index == 0;
  }

  const auto reader = [this](std::uint64_t read_offset, std::size_t read_len) {
    return readBytesLocked(read_offset, read_len);
  };
  LineIndexer& indexer = const_cast<DocumentSession*>(this)->line_indexer_;
  indexer.ensureLineIndexed(line_index + 1, reader);
  if (line_index >= line_indexer_.knownLineCount()) {
    return false;
  }

  const std::uint64_t start = line_indexer_.lineStart(line_index);
  const std::uint64_t end =
      (line_index + 1 < line_indexer_.knownLineCount()) ? line_indexer_.lineStart(line_index + 1)
                                                         : size;
  if (end < start) {
    return false;
  }

  std::string line = piece_table_.read(start, end - start, [this](std::uint64_t source_offset,
                                                                   std::size_t take) {
    return chunk_cache_.read(source_offset, take);
  });
  if (!line.empty() && line.back() == '\n') {
    line.pop_back();
  }
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }

  const QString qline = decodeBytesFromStorageLocked(line);
  const std::size_t clamped_col =
      std::min<std::size_t>(column, static_cast<std::size_t>(qline.size()));
  const QByteArray prefix = encodeTextForStorageLocked(qline.left(static_cast<qsizetype>(clamped_col)));
  *offset = start + static_cast<std::uint64_t>(prefix.size());
  return true;
}

bool DocumentSession::lineColumnForOffset(std::uint64_t offset,
                                          std::size_t* line_index,
                                          std::size_t* column) const {
  if (line_index == nullptr || column == nullptr) {
    return false;
  }

  *line_index = 0;
  *column = 0;

  std::unique_lock<std::shared_mutex> lock(model_mutex_);
  const std::uint64_t size = piece_table_.size();
  if (size == 0) {
    return true;
  }

  const std::uint64_t clamped = std::min<std::uint64_t>(offset, size);
  const auto reader = [this](std::uint64_t read_offset, std::size_t read_len) {
    return readBytesLocked(read_offset, read_len);
  };
  (void)const_cast<DocumentSession*>(this)->line_indexer_.ensureOffsetIndexed(clamped, reader);

  const std::size_t resolved_line = line_indexer_.lineIndexForOffset(clamped);
  const std::uint64_t line_start = line_indexer_.lineStart(resolved_line);
  const std::uint64_t prefix_len = clamped - line_start;
  const std::string prefix = piece_table_.read(line_start, prefix_len, [this](std::uint64_t source_offset,
                                                                               std::size_t take) {
    return chunk_cache_.read(source_offset, take);
  });
  const qsizetype utf8_size = static_cast<qsizetype>(
      std::min<std::size_t>(prefix.size(), static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())));

  *line_index = resolved_line;
  *column = static_cast<std::size_t>(
      decodeBytesFromStorageLocked(std::string_view(prefix.data(), static_cast<std::size_t>(utf8_size))).size());
  return true;
}

std::string DocumentSession::bytesAt(std::uint64_t offset, std::size_t length) const {
  return readBytes(offset, length);
}

bool DocumentSession::isDirty() const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return dirty_;
}

bool DocumentSession::isLineIndexComplete() const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return line_indexer_.isComplete();
}

bool DocumentSession::isReadOnly() const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return read_only_;
}

void DocumentSession::setReadOnly(bool read_only) {
  bool state_changed = false;
  {
    std::unique_lock<std::shared_mutex> lock(model_mutex_);
    if (read_only_ != read_only) {
      read_only_ = read_only;
      state_changed = true;
    }
  }
  if (state_changed) {
    emit changed();
    emit undoRedoStateChanged(canUndo(), canRedo());
  }
}

void DocumentSession::setIndexPriority(IndexPriority priority) {
  const IndexPriority previous = index_priority_.exchange(priority, std::memory_order_relaxed);
  if (previous != priority) {
    index_cv_.notify_one();
  }
}

DocumentSession::IndexPriority DocumentSession::indexPriority() const {
  return index_priority_.load(std::memory_order_relaxed);
}

std::size_t DocumentSession::indexedLineCount() const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return line_indexer_.knownLineCount();
}

file::ChunkCache::Stats DocumentSession::chunkCacheStats() const {
  return chunk_cache_.stats();
}

DocumentSession::TextEncoding DocumentSession::textEncoding() const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return text_encoding_;
}

void DocumentSession::setTextEncoding(TextEncoding encoding) {
  bool format_changed = false;
  {
    std::unique_lock<std::shared_mutex> lock(model_mutex_);
    if (text_encoding_ != encoding) {
      text_encoding_ = encoding;
      format_changed = true;
    }
  }
  if (format_changed) {
    emit changed();
  }
}

QString DocumentSession::textEncodingName() const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return (text_encoding_ == TextEncoding::kGbk) ? QStringLiteral("GBK")
                                                : QStringLiteral("UTF-8");
}

DocumentSession::LineEnding DocumentSession::lineEnding() const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return line_ending_;
}

void DocumentSession::setLineEnding(LineEnding line_ending) {
  bool format_changed = false;
  {
    std::unique_lock<std::shared_mutex> lock(model_mutex_);
    if (line_ending_ != line_ending) {
      line_ending_ = line_ending;
      format_changed = true;
    }
  }
  if (format_changed) {
    emit changed();
  }
}

QString DocumentSession::lineEndingName() const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return (line_ending_ == LineEnding::kCrlf) ? QStringLiteral("CRLF")
                                              : QStringLiteral("LF");
}

QString DocumentSession::lineEndingSequence() const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return (line_ending_ == LineEnding::kCrlf) ? QStringLiteral("\r\n")
                                              : QStringLiteral("\n");
}

QByteArray DocumentSession::encodeTextForStorage(const QString& text) const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return encodeTextForStorageLocked(text);
}

QString DocumentSession::decodeBytesFromStorage(std::string_view bytes) const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return decodeBytesFromStorageLocked(bytes);
}

void DocumentSession::beginTransaction(const QString& label) {
  std::unique_lock<std::shared_mutex> lock(model_mutex_);
  if (read_only_) {
    return;
  }
  if (!active_transaction_) {
    active_transaction_ = EditTransaction{.label = label, .operations = {}};
  }
}

void DocumentSession::endTransaction() {
  {
    std::unique_lock<std::shared_mutex> lock(model_mutex_);
    if (read_only_) {
      return;
    }
    if (!active_transaction_) {
      return;
    }
    if (!active_transaction_->operations.empty()) {
      pushTransaction(std::move(*active_transaction_));
    }
    active_transaction_.reset();
  }
  emitChangedAndUndoState();
}

bool DocumentSession::undo() {
  {
    std::unique_lock<std::shared_mutex> lock(model_mutex_);
    if (read_only_) {
      return false;
    }
    if (undo_stack_.empty()) {
      return false;
    }

    EditTransaction tx = std::move(undo_stack_.back());
    undo_stack_.pop_back();

    replaying_history_ = true;
    for (auto it = tx.operations.rbegin(); it != tx.operations.rend(); ++it) {
      if (it->kind == EditOperation::Kind::kInsert) {
        const std::uint64_t erased =
            eraseInternal(it->offset, static_cast<std::uint64_t>(it->text.size()));
        if (erased > 0) {
          recordOperationLogLocked(EditOperation{
              .kind = EditOperation::Kind::kErase,
              .offset = it->offset,
              .text = it->text.substr(0, static_cast<std::size_t>(erased)),
          });
        }
      } else {
        const std::uint64_t inserted_offset = insertInternal(it->offset, it->text);
        recordOperationLogLocked(EditOperation{
            .kind = EditOperation::Kind::kInsert,
            .offset = inserted_offset,
            .text = it->text,
        });
      }
    }
    replaying_history_ = false;

    redo_stack_.push_back(std::move(tx));
    dirty_ = true;
    ++document_revision_;
    resetLineIndexLocked();
  }

  emitChangedAndUndoState();
  return true;
}

bool DocumentSession::redo() {
  {
    std::unique_lock<std::shared_mutex> lock(model_mutex_);
    if (read_only_) {
      return false;
    }
    if (redo_stack_.empty()) {
      return false;
    }

    EditTransaction tx = std::move(redo_stack_.back());
    redo_stack_.pop_back();

    replaying_history_ = true;
    for (const EditOperation& op : tx.operations) {
      if (op.kind == EditOperation::Kind::kInsert) {
        const std::uint64_t inserted_offset = insertInternal(op.offset, op.text);
        recordOperationLogLocked(EditOperation{
            .kind = EditOperation::Kind::kInsert,
            .offset = inserted_offset,
            .text = op.text,
        });
      } else {
        const std::uint64_t erased =
            eraseInternal(op.offset, static_cast<std::uint64_t>(op.text.size()));
        if (erased > 0) {
          recordOperationLogLocked(EditOperation{
              .kind = EditOperation::Kind::kErase,
              .offset = op.offset,
              .text = op.text.substr(0, static_cast<std::size_t>(erased)),
          });
        }
      }
    }
    replaying_history_ = false;

    undo_stack_.push_back(std::move(tx));
    dirty_ = true;
    ++document_revision_;
    resetLineIndexLocked();
  }

  emitChangedAndUndoState();
  return true;
}

bool DocumentSession::canUndo() const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return !read_only_ && !undo_stack_.empty();
}

bool DocumentSession::canRedo() const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return !read_only_ && !redo_stack_.empty();
}

std::uint64_t DocumentSession::startSearch(const QString& query,
                                           const SearchOptions& options,
                                           std::size_t max_matches) {
  const QByteArray needle_bytes = encodeTextForStorage(query);
  const std::string needle(needle_bytes.data(), static_cast<std::size_t>(needle_bytes.size()));
  if (needle.empty()) {
    return 0;
  }

  const std::uint64_t request_id = next_search_request_id_.fetch_add(1);
  const std::uint64_t total_size = byteSize();
  if (total_size == 0) {
    QMetaObject::invokeMethod(
        this, [this, request_id]() { emit searchCompleted(request_id, 0); }, Qt::QueuedConnection);
    return request_id;
  }

  constexpr std::size_t kShardBytes = 4ULL * 1024 * 1024;
  const std::size_t overlap = options.regex ? 0 : std::max<std::size_t>(0, needle.size() - 1);
  const std::size_t hard_limit =
      (max_matches == 0) ? std::numeric_limits<std::size_t>::max() : max_matches;

  struct Shard {
    std::uint64_t start = 0;
    std::size_t scan_len = 0;
    std::size_t base_len = 0;
  };
  std::vector<Shard> shards;
  for (std::uint64_t start = 0; start < total_size; start += kShardBytes) {
    const std::size_t base_len =
        static_cast<std::size_t>(std::min<std::uint64_t>(kShardBytes, total_size - start));
    const std::size_t tail_overlap =
        (start + base_len < total_size) ? std::min(overlap, static_cast<std::size_t>(total_size - start - base_len))
                                        : 0;
    shards.push_back(Shard{
        .start = start,
        .scan_len = base_len + tail_overlap,
        .base_len = base_len,
    });
  }

  auto state = std::make_shared<SearchRequestState>();
  state->pending_jobs = shards.size();
  state->max_matches = hard_limit;

  {
    std::lock_guard<std::mutex> lock(search_mutex_);
    active_searches_[request_id] = ActiveSearch{.state = state, .job_ids = {}};
  }

  for (const Shard& shard : shards) {
    const SearchThreadPool::JobId job_id = search_pool_.submit(
        [this, request_id, state, shard, needle, options](std::atomic_bool& canceled) {
          if (canceled.load()) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->pending_jobs > 0) {
              --state->pending_jobs;
            }
            if (state->pending_jobs == 0 && !state->completion_emitted) {
              state->completion_emitted = true;
              finalizeSearch(request_id, state);
            }
            return;
          }

          const std::string data = readBytes(shard.start, shard.scan_len);
          if (data.empty()) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->pending_jobs > 0) {
              --state->pending_jobs;
            }
            if (state->pending_jobs == 0 && !state->completion_emitted) {
              state->completion_emitted = true;
              finalizeSearch(request_id, state);
            }
            return;
          }

          std::vector<SearchMatch> local_matches = search_engine_.findAll(data, needle, options);
          for (SearchMatch& match : local_matches) {
            match.offset += shard.start;
          }

          {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->canceled) {
              for (const SearchMatch& match : local_matches) {
                if (match.offset >= shard.start + shard.base_len) {
                  continue;
                }
                if (state->matches.size() >= state->max_matches) {
                  break;
                }
                state->matches.push_back(match);
              }
              if (state->matches.size() >= state->max_matches) {
                state->canceled = true;
              }
            }

            if (state->pending_jobs > 0) {
              --state->pending_jobs;
            }
            if (state->pending_jobs == 0 && !state->completion_emitted) {
              state->completion_emitted = true;
              finalizeSearch(request_id, state);
            }
          }
        });

    if (job_id == 0) {
      std::lock_guard<std::mutex> state_lock(state->mutex);
      if (state->pending_jobs > 0) {
        --state->pending_jobs;
      }
      continue;
    }

    std::lock_guard<std::mutex> lock(search_mutex_);
    auto it = active_searches_.find(request_id);
    if (it != active_searches_.end()) {
      it->second.job_ids.push_back(job_id);
    }
  }

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->pending_jobs == 0 && !state->completion_emitted) {
      state->completion_emitted = true;
      finalizeSearch(request_id, state);
    }
  }

  return request_id;
}

std::vector<SearchMatch> DocumentSession::findAllMatches(const QString& query,
                                                         const SearchOptions& options,
                                                         std::size_t max_matches) const {
  const QByteArray needle_bytes = encodeTextForStorage(query);
  const std::string needle(needle_bytes.data(), static_cast<std::size_t>(needle_bytes.size()));
  if (needle.empty()) {
    return {};
  }

  const std::uint64_t total_size = byteSize();
  if (total_size == 0) {
    return {};
  }

  constexpr std::size_t kShardBytes = 4ULL * 1024 * 1024;
  const std::size_t overlap = options.regex ? 0 : std::max<std::size_t>(0, needle.size() - 1);
  const std::size_t hard_limit =
      (max_matches == 0) ? std::numeric_limits<std::size_t>::max() : max_matches;

  std::vector<SearchMatch> matches;
  for (std::uint64_t start = 0; start < total_size; start += kShardBytes) {
    const std::size_t base_len =
        static_cast<std::size_t>(std::min<std::uint64_t>(kShardBytes, total_size - start));
    const std::size_t tail_overlap =
        (start + base_len < total_size)
            ? std::min(overlap, static_cast<std::size_t>(total_size - start - base_len))
            : 0;
    const std::size_t scan_len = base_len + tail_overlap;

    const std::string data = readBytes(start, scan_len);
    if (data.empty()) {
      break;
    }

    const std::size_t remaining_limit =
        (hard_limit == std::numeric_limits<std::size_t>::max()) ? 0 : (hard_limit - matches.size());
    std::vector<SearchMatch> local_matches = search_engine_.findAll(data, needle, options, remaining_limit);
    for (SearchMatch& match : local_matches) {
      match.offset += start;
    }

    for (const SearchMatch& match : local_matches) {
      if (match.offset >= start + base_len) {
        continue;
      }
      matches.push_back(match);
      if (matches.size() >= hard_limit) {
        break;
      }
    }
    if (matches.size() >= hard_limit) {
      break;
    }
  }

  std::sort(matches.begin(), matches.end(), [](const SearchMatch& lhs, const SearchMatch& rhs) {
    if (lhs.offset != rhs.offset) {
      return lhs.offset < rhs.offset;
    }
    return lhs.length < rhs.length;
  });
  return matches;
}

bool DocumentSession::replaceRange(std::uint64_t offset,
                                   std::uint64_t length,
                                   const QString& replacement,
                                   const QString& label) {
  if (length == 0 && replacement.isEmpty()) {
    return false;
  }

  bool changed = false;
  {
    std::unique_lock<std::shared_mutex> lock(model_mutex_);
    if (read_only_) {
      return false;
    }
    const bool created_transaction = !active_transaction_.has_value();
    if (created_transaction) {
      active_transaction_ = EditTransaction{.label = label, .operations = {}};
    }

    const QByteArray replacement_bytes = encodeTextForStorageLocked(replacement);
    changed = replaceRangeLocked(offset,
                                 length,
                                 std::string_view(replacement_bytes.data(),
                                                  static_cast<std::size_t>(replacement_bytes.size())));
    if (changed) {
      dirty_ = true;
      ++document_revision_;
      resetLineIndexLocked();
    }

    if (created_transaction) {
      if (active_transaction_ && !active_transaction_->operations.empty()) {
        pushTransaction(std::move(*active_transaction_));
      }
      active_transaction_.reset();
    }
  }

  if (changed) {
    emitChangedAndUndoState();
  }
  return changed;
}

std::size_t DocumentSession::replaceAll(const QString& query,
                                        const QString& replacement,
                                        const SearchOptions& options,
                                        std::size_t max_replacements) {
  {
    std::shared_lock<std::shared_mutex> lock(model_mutex_);
    if (read_only_) {
      return 0;
    }
  }

  std::vector<SearchMatch> matches = findAllMatches(query, options);
  if (matches.empty()) {
    return 0;
  }

  std::vector<SearchMatch> non_overlapping;
  non_overlapping.reserve(matches.size());

  std::uint64_t next_allowed_offset = 0;
  bool has_last = false;
  for (const SearchMatch& match : matches) {
    if (match.length == 0) {
      continue;
    }
    if (!has_last || match.offset >= next_allowed_offset) {
      non_overlapping.push_back(match);
      next_allowed_offset = match.offset + static_cast<std::uint64_t>(match.length);
      has_last = true;
      if (max_replacements > 0 && non_overlapping.size() >= max_replacements) {
        break;
      }
    }
  }

  if (non_overlapping.empty()) {
    return 0;
  }

  const QByteArray replacement_bytes = encodeTextForStorage(replacement);
  const std::string replacement_encoded(replacement_bytes.data(),
                                        static_cast<std::size_t>(replacement_bytes.size()));
  std::size_t replaced = 0;
  {
    std::unique_lock<std::shared_mutex> lock(model_mutex_);
    const bool created_transaction = !active_transaction_.has_value();
    if (created_transaction) {
      active_transaction_ = EditTransaction{
          .label = QStringLiteral("Replace All"),
          .operations = {},
      };
    }

    for (auto it = non_overlapping.rbegin(); it != non_overlapping.rend(); ++it) {
      if (replaceRangeLocked(it->offset,
                             static_cast<std::uint64_t>(it->length),
                             replacement_encoded)) {
        ++replaced;
      }
    }

    if (replaced > 0) {
      dirty_ = true;
      ++document_revision_;
      resetLineIndexLocked();
    }

    if (created_transaction) {
      if (active_transaction_ && !active_transaction_->operations.empty()) {
        pushTransaction(std::move(*active_transaction_));
      }
      active_transaction_.reset();
    }
  }

  if (replaced > 0) {
    emitChangedAndUndoState();
  }
  return replaced;
}

void DocumentSession::cancelSearch(std::uint64_t request_id) {
  ActiveSearch search;
  {
    std::lock_guard<std::mutex> lock(search_mutex_);
    auto it = active_searches_.find(request_id);
    if (it == active_searches_.end()) {
      return;
    }
    search = it->second;
  }

  {
    std::lock_guard<std::mutex> state_lock(search.state->mutex);
    search.state->canceled = true;
  }
  for (const SearchThreadPool::JobId job_id : search.job_ids) {
    search_pool_.cancel(job_id);
  }
}

void DocumentSession::cancelAllSearches() {
  std::vector<std::uint64_t> request_ids;
  {
    std::lock_guard<std::mutex> lock(search_mutex_);
    request_ids.reserve(active_searches_.size());
    for (const auto& [request_id, _] : active_searches_) {
      (void)_;
      request_ids.push_back(request_id);
    }
  }
  for (const std::uint64_t request_id : request_ids) {
    cancelSearch(request_id);
  }
  search_pool_.cancelAll();
}

std::vector<SearchMatch> DocumentSession::takeSearchResults(std::uint64_t request_id) {
  std::lock_guard<std::mutex> lock(search_mutex_);
  auto it = completed_search_results_.find(request_id);
  if (it == completed_search_results_.end()) {
    return {};
  }
  std::vector<SearchMatch> out = std::move(it->second);
  completed_search_results_.erase(it);
  return out;
}

QString DocumentSession::lineAtLocked(std::size_t line_index) {
  if (piece_table_.size() == 0) {
    return (line_index == 0) ? QStringLiteral("") : QString();
  }

  const auto reader = [this](std::uint64_t offset, std::size_t length) {
    return readBytesLocked(offset, length);
  };

  line_indexer_.ensureLineIndexed(line_index + 1, reader);
  if (line_index >= line_indexer_.knownLineCount()) {
    return QString();
  }

  const std::uint64_t start = line_indexer_.lineStart(line_index);
  line_indexer_.ensureLineIndexed(line_index + 1, reader);
  const std::uint64_t end =
      (line_index + 1 < line_indexer_.knownLineCount()) ? line_indexer_.lineStart(line_index + 1)
                                                         : piece_table_.size();
  if (end < start) {
    return QString();
  }
  if (end == start) {
    return QStringLiteral("");
  }

  std::string line = piece_table_.read(start, end - start, [this](std::uint64_t source_offset,
                                                                   std::size_t take) {
    return chunk_cache_.read(source_offset, take);
  });
  if (!line.empty() && line.back() == '\n') {
    line.pop_back();
  }
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  if (line.empty()) {
    return QStringLiteral("");
  }
  return decodeBytesFromStorageLocked(line);
}

DocumentSession::ViewLine DocumentSession::buildViewLineLocked(std::size_t line_index,
                                                               std::size_t max_bytes_per_line,
                                                               std::size_t max_chars_per_line) {
  ViewLine view;
  view.line_index = line_index;
  if (max_bytes_per_line == 0 || max_chars_per_line == 0 || piece_table_.size() == 0) {
    return view;
  }
  if (line_index >= line_indexer_.knownLineCount()) {
    return view;
  }

  const std::uint64_t start = line_indexer_.lineStart(line_index);
  const std::uint64_t total_size = piece_table_.size();
  bool line_end_known = false;
  std::uint64_t line_end = total_size;
  if (line_index + 1 < line_indexer_.knownLineCount()) {
    line_end_known = true;
    line_end = line_indexer_.lineStart(line_index + 1);
  } else if (line_indexer_.isComplete()) {
    line_end_known = true;
    line_end = total_size;
  }

  const std::uint64_t natural_end = line_end_known ? line_end : total_size;
  const std::uint64_t max_end =
      std::min<std::uint64_t>(natural_end, start + static_cast<std::uint64_t>(max_bytes_per_line));
  const std::size_t read_len =
      static_cast<std::size_t>(std::max<std::uint64_t>(0, max_end - start));

  std::string bytes = piece_table_.read(start, read_len, [this](std::uint64_t source_offset,
                                                                 std::size_t take) {
    return chunk_cache_.read(source_offset, take);
  });
  std::uint64_t content_end = start + static_cast<std::uint64_t>(bytes.size());

  if (!bytes.empty() && bytes.back() == '\n') {
    bytes.pop_back();
    if (content_end > start) {
      --content_end;
    }
  }
  if (!bytes.empty() && bytes.back() == '\r') {
    bytes.pop_back();
    if (content_end > start) {
      --content_end;
    }
  }

  bool truncated = (!line_end_known) || (line_end_known && max_end < line_end);
  QString text = decodeBytesFromStorageLocked(bytes);
  if (text.isNull()) {
    text = QStringLiteral("");
  }
  if (static_cast<std::size_t>(text.size()) > max_chars_per_line) {
    text = text.left(static_cast<qsizetype>(max_chars_per_line));
    const QByteArray trimmed = encodeTextForStorageLocked(text);
    bytes.assign(trimmed.data(), static_cast<std::size_t>(trimmed.size()));
    content_end = start + static_cast<std::uint64_t>(bytes.size());
    truncated = true;
  }

  view.text = text;
  view.encoded = std::move(bytes);
  view.start_offset = start;
  view.content_end_offset = content_end;
  view.truncated = truncated;
  return view;
}

std::string DocumentSession::readBytes(std::uint64_t offset, std::size_t length) const {
  std::shared_lock<std::shared_mutex> lock(model_mutex_);
  return readBytesLocked(offset, length);
}

std::string DocumentSession::readBytesLocked(std::uint64_t offset, std::size_t length) const {
  return piece_table_.read(offset, length, [this](std::uint64_t source_offset, std::size_t take) {
    return chunk_cache_.read(source_offset, take);
  });
}

QByteArray DocumentSession::encodeTextForStorageLocked(const QString& text) const {
  if (text.isEmpty()) {
    return {};
  }

  if (text_encoding_ == TextEncoding::kUtf8) {
    return text.toUtf8();
  }

  const auto gbk_encoding = gbkQtEncoding();
  if (gbk_encoding.has_value()) {
    QStringEncoder encoder(*gbk_encoding);
    QByteArray bytes = encoder.encode(text);
    if (!encoder.hasError()) {
      return bytes;
    }
  }
  return text.toLocal8Bit();
}

QString DocumentSession::decodeBytesFromStorageLocked(std::string_view bytes) const {
  if (bytes.empty()) {
    return {};
  }

  const qsizetype size = static_cast<qsizetype>(
      std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())));
  if (size <= 0) {
    return {};
  }

  if (text_encoding_ == TextEncoding::kUtf8) {
    return QString::fromUtf8(bytes.data(), size);
  }

  const auto gbk_encoding = gbkQtEncoding();
  if (gbk_encoding.has_value()) {
    QStringDecoder decoder(*gbk_encoding);
    QString text = decoder.decode(QByteArrayView(bytes.data(), size));
    if (!decoder.hasError()) {
      return text;
    }
  }
  return QString::fromLocal8Bit(bytes.data(), size);
}

std::string DocumentSession::removedTextForErase(std::uint64_t offset, std::uint64_t length) const {
  if (length == 0 || offset >= piece_table_.size()) {
    return {};
  }
  const std::uint64_t clamped_len = std::min<std::uint64_t>(length, piece_table_.size() - offset);
  return piece_table_.read(offset, clamped_len, [this](std::uint64_t source_offset,
                                                        std::size_t take) {
    return chunk_cache_.read(source_offset, take);
  });
}

bool DocumentSession::replaceRangeLocked(std::uint64_t offset,
                                         std::uint64_t length,
                                         std::string_view replacement) {
  const std::uint64_t clamped_offset = std::min<std::uint64_t>(offset, piece_table_.size());
  bool changed = false;

  if (length > 0) {
    const std::string removed = removedTextForErase(clamped_offset, length);
    if (!removed.empty()) {
      const std::uint64_t erased =
          eraseInternal(clamped_offset, static_cast<std::uint64_t>(removed.size()));
      if (erased > 0) {
        changed = true;
        const std::string erased_text = removed.substr(0, static_cast<std::size_t>(erased));
        if (!replaying_history_) {
          recordOperation(EditOperation{
              .kind = EditOperation::Kind::kErase,
              .offset = clamped_offset,
              .text = erased_text,
          });
        }
        recordOperationLogLocked(EditOperation{
            .kind = EditOperation::Kind::kErase,
            .offset = clamped_offset,
            .text = erased_text,
        });
      }
    }
  }

  if (!replacement.empty()) {
    const std::string replacement_text(replacement);
    const std::uint64_t inserted_offset = insertInternal(clamped_offset, replacement_text);
    changed = true;
    if (!replaying_history_) {
      recordOperation(EditOperation{
          .kind = EditOperation::Kind::kInsert,
          .offset = inserted_offset,
          .text = replacement_text,
      });
    }
    recordOperationLogLocked(EditOperation{
        .kind = EditOperation::Kind::kInsert,
        .offset = inserted_offset,
        .text = replacement_text,
    });
  }

  return changed;
}

std::uint64_t DocumentSession::insertInternal(std::uint64_t offset, const std::string& text) {
  if (text.empty()) {
    return std::min<std::uint64_t>(offset, piece_table_.size());
  }
  const std::uint64_t clamped = std::min<std::uint64_t>(offset, piece_table_.size());
  piece_table_.insert(clamped, text);
  return clamped;
}

std::uint64_t DocumentSession::eraseInternal(std::uint64_t offset, std::uint64_t length) {
  if (length == 0) {
    return 0;
  }
  const std::uint64_t clamped = std::min<std::uint64_t>(offset, piece_table_.size());
  if (clamped >= piece_table_.size()) {
    return 0;
  }
  const std::uint64_t clamped_len = std::min<std::uint64_t>(length, piece_table_.size() - clamped);
  piece_table_.erase(clamped, clamped_len);
  return clamped_len;
}

void DocumentSession::recordOperation(EditOperation op) {
  if (op.text.empty()) {
    return;
  }
  if (active_transaction_) {
    active_transaction_->operations.push_back(std::move(op));
    return;
  }
  EditTransaction tx;
  tx.operations.push_back(std::move(op));
  pushTransaction(std::move(tx));
}

void DocumentSession::recordOperationLogLocked(EditOperation op) {
  if (op.text.empty() || replaying_operation_log_) {
    return;
  }
  operation_log_ops_.push_back(std::move(op));
}

void DocumentSession::pushTransaction(EditTransaction transaction) {
  if (transaction.operations.empty()) {
    return;
  }
  undo_stack_.push_back(std::move(transaction));
  redo_stack_.clear();
}

void DocumentSession::detectTextEncodingAndLineEndingLocked() {
  text_encoding_ = TextEncoding::kUtf8;
  line_ending_ = LineEnding::kLf;

  const std::uint64_t size = piece_table_.size();
  if (size == 0) {
    return;
  }

  constexpr std::size_t kProbeBytes = 1ULL * 1024 * 1024;
  const std::size_t probe_size = static_cast<std::size_t>(std::min<std::uint64_t>(size, kProbeBytes));
  if (probe_size == 0) {
    return;
  }

  const std::string probe = piece_table_.read(0, probe_size, [this](std::uint64_t source_offset,
                                                                     std::size_t take) {
    return chunk_cache_.read(source_offset, take);
  });
  if (probe.empty()) {
    return;
  }

  if (probe.find("\r\n") != std::string::npos) {
    line_ending_ = LineEnding::kCrlf;
  }

  QStringDecoder utf8_decoder(QStringConverter::Encoding::Utf8);
  (void)utf8_decoder.decode(
      QByteArrayView(probe.data(),
                     static_cast<qsizetype>(
                         std::min<std::size_t>(probe.size(),
                                               static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())))));
  if (utf8_decoder.hasError()) {
    text_encoding_ = TextEncoding::kGbk;
  }
}

void DocumentSession::resetLineIndexLocked() {
  line_indexer_.reset(piece_table_.size());
  requestBackgroundIndexing();
}

void DocumentSession::requestBackgroundIndexing() {
  {
    std::lock_guard<std::mutex> lock(index_mutex_);
    ++index_generation_;
  }
  index_cv_.notify_one();
}

bool DocumentSession::isIndexGenerationChanged(std::uint64_t generation) const {
  std::lock_guard<std::mutex> lock(index_mutex_);
  return index_generation_ != generation;
}

void DocumentSession::indexWorkerLoop() {
  auto stopRequested = [this]() -> bool {
    std::lock_guard<std::mutex> lock(index_mutex_);
    return stop_index_thread_;
  };

  std::uint64_t observed_generation = 0;
  while (true) {
    {
      std::unique_lock<std::mutex> lock(index_mutex_);
      index_cv_.wait(lock, [this, &observed_generation]() {
        return stop_index_thread_ || index_generation_ != observed_generation;
      });
      if (stop_index_thread_) {
        return;
      }
      observed_generation = index_generation_;
    }

    std::size_t ticks_without_notification = 0;
    while (true) {
      if (stopRequested()) {
        return;
      }
      if (isIndexGenerationChanged(observed_generation)) {
        break;
      }

      bool complete = false;
      bool progressed = false;
      const IndexPriority priority = index_priority_.load(std::memory_order_relaxed);
      const std::size_t max_scan_bytes =
          (priority == IndexPriority::kInteractive) ? (64ULL * 1024) : (256ULL * 1024);
      const std::size_t notify_threshold =
          (priority == IndexPriority::kInteractive) ? static_cast<std::size_t>(24)
                                                    : static_cast<std::size_t>(8);
      {
        std::unique_lock<std::shared_mutex> lock(model_mutex_);
        if (line_indexer_.isComplete()) {
          complete = true;
        } else {
          const std::size_t known_before = line_indexer_.knownLineCount();
          const auto reader = [this](std::uint64_t offset, std::size_t length) {
            return readBytesLocked(offset, length);
          };
          progressed = line_indexer_.indexNextChunk(reader, max_scan_bytes);
          progressed = progressed || (line_indexer_.knownLineCount() > known_before);
          complete = line_indexer_.isComplete();
        }
      }

      if (progressed || complete) {
        ++ticks_without_notification;
        if (ticks_without_notification >= notify_threshold || complete) {
          ticks_without_notification = 0;
          QMetaObject::invokeMethod(this, [this]() { emit changed(); }, Qt::QueuedConnection);
        }
      }

      if (complete) {
        break;
      }
      if (stopRequested()) {
        return;
      }
      if (!progressed) {
        const auto wait_ms =
            (priority == IndexPriority::kInteractive) ? std::chrono::milliseconds(5)
                                                      : std::chrono::milliseconds(2);
        std::this_thread::sleep_for(wait_ms);
      } else if (priority == IndexPriority::kInteractive) {
        // Keep UI operations responsive while users are actively scrolling/editing.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
  }
}

void DocumentSession::emitChangedAndUndoState() {
  emit changed();
  emit undoRedoStateChanged(canUndo(), canRedo());
}

void DocumentSession::finalizeSearch(std::uint64_t request_id,
                                     const std::shared_ptr<SearchRequestState>& state) {
  QMetaObject::invokeMethod(
      this,
      [this, request_id, state]() {
        std::vector<SearchMatch> matches;
        {
          std::lock_guard<std::mutex> lock(state->mutex);
          matches = state->matches;
        }
        {
          std::lock_guard<std::mutex> lock(search_mutex_);
          completed_search_results_[request_id] = matches;
          active_searches_.erase(request_id);
        }
        emit searchCompleted(request_id, static_cast<qulonglong>(matches.size()));
      },
      Qt::QueuedConnection);
}

}  // namespace massiveedit::core
