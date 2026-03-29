#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include "massiveedit/core/file/chunk_cache.h"
#include "massiveedit/core/file/large_file_backend.h"
#include "massiveedit/core/line_indexer.h"
#include "massiveedit/core/piece_table.h"
#include "massiveedit/core/search_engine.h"
#include "massiveedit/core/search_thread_pool.h"

namespace massiveedit::core {

class DocumentSession : public QObject {
  Q_OBJECT

 public:
  enum class TextEncoding {
    kUtf8,
    kGbk
  };

  enum class LineEnding {
    kLf,
    kCrlf
  };

  enum class IndexPriority {
    kBackground,
    kInteractive
  };

  struct ViewLine {
    std::size_t line_index = 0;
    QString text;
    std::string encoded;
    std::uint64_t start_offset = 0;
    std::uint64_t content_end_offset = 0;
    bool truncated = false;
  };

  explicit DocumentSession(QObject* parent = nullptr);
  ~DocumentSession() override;

  bool openFile(const QString& file_path, QString* error = nullptr);
  bool openFromBytes(const QByteArray& bytes,
                     const QString& source_label = QString(),
                     bool mark_dirty = true);
  bool saveAs(const QString& file_path, QString* error = nullptr);
  bool saveOperationLog(const QString& log_path, QString* error = nullptr) const;
  bool restoreFromOperationLog(const QString& log_path, QString* error = nullptr);

  void insertText(std::uint64_t offset, const QString& text);
  void removeText(std::uint64_t offset, std::uint64_t length);

  void beginTransaction(const QString& label = QString());
  void endTransaction();
  [[nodiscard]] bool undo();
  [[nodiscard]] bool redo();
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;

  [[nodiscard]] std::uint64_t startSearch(const QString& query,
                                          const SearchOptions& options,
                                          std::size_t max_matches = 0);
  [[nodiscard]] std::vector<SearchMatch> findAllMatches(const QString& query,
                                                        const SearchOptions& options,
                                                        std::size_t max_matches = 0) const;
  [[nodiscard]] bool replaceRange(std::uint64_t offset,
                                  std::uint64_t length,
                                  const QString& replacement,
                                  const QString& label = QStringLiteral("Replace"));
  [[nodiscard]] std::size_t replaceAll(const QString& query,
                                       const QString& replacement,
                                       const SearchOptions& options,
                                       std::size_t max_replacements = 0);
  void cancelSearch(std::uint64_t request_id);
  void cancelAllSearches();
  [[nodiscard]] std::vector<SearchMatch> takeSearchResults(std::uint64_t request_id);

  [[nodiscard]] QString filePath() const;
  [[nodiscard]] std::uint64_t byteSize() const;
  [[nodiscard]] std::size_t lineCount() const;
  [[nodiscard]] QString lineAt(std::size_t line_index) const;
  [[nodiscard]] QStringList lines(std::size_t start_line, std::size_t count) const;
  [[nodiscard]] std::vector<ViewLine> viewLines(std::size_t start_line,
                                                std::size_t count,
                                                std::size_t max_bytes_per_line = 64ULL * 1024,
                                                std::size_t max_chars_per_line = 4096) const;
  [[nodiscard]] bool offsetForLineColumn(std::size_t line_index,
                                         std::size_t column,
                                         std::uint64_t* offset) const;
  [[nodiscard]] bool lineColumnForOffset(std::uint64_t offset,
                                         std::size_t* line_index,
                                         std::size_t* column) const;
  [[nodiscard]] std::string bytesAt(std::uint64_t offset, std::size_t length) const;
  [[nodiscard]] bool isDirty() const;
  [[nodiscard]] bool isLineIndexComplete() const;
  [[nodiscard]] bool isReadOnly() const;
  void setReadOnly(bool read_only);
  void setIndexPriority(IndexPriority priority);
  [[nodiscard]] IndexPriority indexPriority() const;
  [[nodiscard]] std::size_t indexedLineCount() const;
  [[nodiscard]] file::ChunkCache::Stats chunkCacheStats() const;
  [[nodiscard]] TextEncoding textEncoding() const;
  void setTextEncoding(TextEncoding encoding);
  [[nodiscard]] QString textEncodingName() const;
  [[nodiscard]] LineEnding lineEnding() const;
  void setLineEnding(LineEnding line_ending);
  [[nodiscard]] QString lineEndingName() const;
  [[nodiscard]] QString lineEndingSequence() const;
  [[nodiscard]] QByteArray encodeTextForStorage(const QString& text) const;
  [[nodiscard]] QString decodeBytesFromStorage(std::string_view bytes) const;

 signals:
  void changed();
  void undoRedoStateChanged(bool can_undo, bool can_redo);
  void searchCompleted(qulonglong request_id, qulonglong match_count);

 private:
  struct EditOperation {
    enum class Kind {
      kInsert,
      kErase
    };

    Kind kind = Kind::kInsert;
    std::uint64_t offset = 0;
    std::string text;
  };

  struct EditTransaction {
    QString label;
    std::vector<EditOperation> operations;
  };

  struct SearchRequestState {
    std::mutex mutex;
    std::vector<SearchMatch> matches;
    std::size_t pending_jobs = 0;
    std::size_t max_matches = 0;
    bool canceled = false;
    bool completion_emitted = false;
  };

  struct ActiveSearch {
    std::shared_ptr<SearchRequestState> state;
    std::vector<SearchThreadPool::JobId> job_ids;
  };

  [[nodiscard]] std::string readBytes(std::uint64_t offset, std::size_t length) const;
  [[nodiscard]] std::string readBytesLocked(std::uint64_t offset, std::size_t length) const;
  [[nodiscard]] QByteArray encodeTextForStorageLocked(const QString& text) const;
  [[nodiscard]] QString decodeBytesFromStorageLocked(std::string_view bytes) const;
  [[nodiscard]] std::string removedTextForErase(std::uint64_t offset, std::uint64_t length) const;
  [[nodiscard]] QString lineAtLocked(std::size_t line_index);
  [[nodiscard]] ViewLine buildViewLineLocked(std::size_t line_index,
                                             std::size_t max_bytes_per_line,
                                             std::size_t max_chars_per_line);
  [[nodiscard]] bool replaceRangeLocked(std::uint64_t offset,
                                        std::uint64_t length,
                                        std::string_view replacement);
  std::uint64_t insertInternal(std::uint64_t offset, const std::string& text);
  std::uint64_t eraseInternal(std::uint64_t offset, std::uint64_t length);
  void recordOperation(EditOperation op);
  void recordOperationLogLocked(EditOperation op);
  void pushTransaction(EditTransaction transaction);
  void resetLineIndexLocked();
  void requestBackgroundIndexing();
  void indexWorkerLoop();
  [[nodiscard]] bool isIndexGenerationChanged(std::uint64_t generation) const;
  void detectTextEncodingAndLineEndingLocked();
  void emitChangedAndUndoState();
  void finalizeSearch(std::uint64_t request_id, const std::shared_ptr<SearchRequestState>& state);

  file::LargeFileBackend backend_;
  file::ChunkCache chunk_cache_;
  PieceTable piece_table_;
  LineIndexer line_indexer_;
  SearchEngine search_engine_;
  SearchThreadPool search_pool_;
  std::filesystem::path source_path_;
  mutable std::shared_mutex model_mutex_;

  std::vector<EditTransaction> undo_stack_;
  std::vector<EditTransaction> redo_stack_;
  std::optional<EditTransaction> active_transaction_;
  bool replaying_history_ = false;

  std::atomic<std::uint64_t> next_search_request_id_{1};
  mutable std::mutex search_mutex_;
  std::unordered_map<std::uint64_t, ActiveSearch> active_searches_;
  std::unordered_map<std::uint64_t, std::vector<SearchMatch>> completed_search_results_;

  bool dirty_ = false;
  bool read_only_ = false;
  std::uint64_t document_revision_ = 0;
  bool operation_log_base_is_file_ = false;
  std::filesystem::path operation_log_base_path_;
  std::string operation_log_base_content_;
  std::vector<EditOperation> operation_log_ops_;
  bool replaying_operation_log_ = false;
  TextEncoding text_encoding_ = TextEncoding::kUtf8;
  LineEnding line_ending_ = LineEnding::kLf;

  mutable std::mutex index_mutex_;
  mutable std::condition_variable index_cv_;
  std::thread index_thread_;
  bool stop_index_thread_ = false;
  std::uint64_t index_generation_ = 0;
  std::atomic<IndexPriority> index_priority_{IndexPriority::kBackground};
};

}  // namespace massiveedit::core
