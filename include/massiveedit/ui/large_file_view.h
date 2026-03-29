#pragma once

#include <QAbstractScrollArea>
#include <QPointer>
#include <QTimer>
#include <QVariant>

#include "massiveedit/core/document_session.h"

class QKeyEvent;
class QContextMenuEvent;
class QFocusEvent;
class QInputMethodEvent;
class QMouseEvent;
class QPoint;
class QFontMetrics;

namespace massiveedit::ui {

class LargeFileView : public QAbstractScrollArea {
  Q_OBJECT

 public:
  enum class AppearanceMode {
    kFollowSystem,
    kLight,
    kDark,
  };

  explicit LargeFileView(QWidget* parent = nullptr);

  void setSession(core::DocumentSession* session);
  void setAppearanceMode(AppearanceMode mode);
  [[nodiscard]] AppearanceMode appearanceMode() const;
  void scrollToLine(std::size_t line, bool center = true);
  void setActiveMatch(std::size_t line, std::size_t column, std::size_t length);
  void clearActiveMatch();
  [[nodiscard]] bool goToLineColumn(std::size_t line, std::size_t column, bool center = true);
  [[nodiscard]] bool goToOffset(std::uint64_t offset, bool center = true);
  [[nodiscard]] std::size_t cursorLine() const;
  [[nodiscard]] std::size_t cursorColumn() const;
  [[nodiscard]] bool hasSelection() const;
  [[nodiscard]] QString selectedText() const;
  [[nodiscard]] bool selectedByteRange(std::uint64_t* start, std::uint64_t* end) const;
  void setTabWidth(int spaces);
  [[nodiscard]] int tabWidth() const;
  bool deleteSelection();
  void pasteText(const QString& text);
  void clearSelection();

signals:
  void cursorMoved(std::size_t line, std::size_t column);
  void scrollActivity(std::size_t top_line);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  bool focusNextPrevChild(bool next) override;
  void inputMethodEvent(QInputMethodEvent* event) override;
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  void updateScrollbars();
  void clampCursor();
  void setCursorPosition(std::size_t line, std::size_t column, bool keep_preferred_column = false);
  void ensureCursorVisible(bool center = false);
  [[nodiscard]] int gutterWidth() const;
  [[nodiscard]] int textOriginX() const;
  [[nodiscard]] QString expandTabsForDisplay(const QString& text) const;
  [[nodiscard]] std::size_t visualColumnForBufferColumn(const QString& text, std::size_t column) const;
  [[nodiscard]] std::size_t bufferColumnForVisualColumn(const QString& text, std::size_t visual_column) const;
  [[nodiscard]] int xForBufferColumn(const QString& text,
                                     std::size_t column,
                                     const QFontMetrics& metrics) const;
  [[nodiscard]] std::size_t bufferColumnForPixelX(const QString& text,
                                                  int x,
                                                  const QFontMetrics& metrics) const;
  [[nodiscard]] QString cursorLineText() const;
  [[nodiscard]] bool cursorOffset(std::uint64_t* offset) const;
  [[nodiscard]] bool removeLineBreakBeforeCursor();
  [[nodiscard]] bool removeLineBreakAfterCursor();
  [[nodiscard]] bool positionToLineColumn(const QPoint& position,
                                          std::size_t* line,
                                          std::size_t* column) const;
  [[nodiscard]] bool positionToOffset(const QPoint& position, std::uint64_t* offset) const;
  bool moveCursorToOffset(std::uint64_t offset, bool keep_preferred_column = false);
  [[nodiscard]] bool selectedOffsets(std::uint64_t* start, std::uint64_t* end) const;
  [[nodiscard]] bool deleteSelectionRange(std::uint64_t* start_offset = nullptr);
  bool insertTextAtCursor(const QString& text);
  void restartCaretBlinkTimer();
  void resetCaretBlink();

  QPointer<core::DocumentSession> session_;
  std::size_t top_line_ = 0;
  std::size_t cursor_line_ = 0;
  std::size_t cursor_column_ = 0;
  std::size_t preferred_column_ = 0;
  bool has_active_match_ = false;
  std::size_t active_match_line_ = 0;
  std::size_t active_match_column_ = 0;
  std::size_t active_match_length_ = 0;
  bool selection_anchor_set_ = false;
  std::uint64_t selection_anchor_offset_ = 0;
  std::uint64_t selection_caret_offset_ = 0;
  bool mouse_selecting_ = false;
  AppearanceMode appearance_mode_ = AppearanceMode::kFollowSystem;
  bool rows_cache_valid_ = false;
  std::size_t cached_start_line_ = 0;
  std::size_t cached_visible_lines_ = 0;
  std::vector<core::DocumentSession::ViewLine> cached_rows_;
  QTimer caret_blink_timer_;
  bool caret_visible_ = true;
  int tab_width_spaces_ = 4;
};

}  // namespace massiveedit::ui
