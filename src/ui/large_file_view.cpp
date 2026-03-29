#include "massiveedit/ui/large_file_view.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <unordered_map>

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QPoint>
#include <QScrollBar>
#include <QStyleHints>

#include "massiveedit/ui/i18n.h"

namespace massiveedit::ui {
namespace {

QString trKey(const char* key) {
  if (key == nullptr || *key == '\0') {
    return {};
  }

  using Cache = std::unordered_map<const char*, QString>;
  static std::array<Cache, 6> caches;
  const std::size_t idx = std::min<std::size_t>(static_cast<std::size_t>(i18n::currentLanguage()),
                                                caches.size() - 1);
  Cache& cache = caches[idx];
  const auto it = cache.find(key);
  if (it != cache.end()) {
    return it->second;
  }

  const QString value = i18n::tr(QString::fromLatin1(key));
  cache.emplace(key, value);
  return value;
}

struct ViewColors {
  QColor background;
  QColor gutter_background;
  QColor gutter_border;
  QColor text;
  QColor line_number;
  QColor line_number_active;
  QColor current_row;
  QColor current_row_gutter;
  QColor selection;
  QColor match;
  QColor caret;
  QColor hint;
};

bool isSystemDarkMode(const QWidget* widget) {
  if (QGuiApplication::styleHints() != nullptr) {
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Dark) {
      return true;
    }
    if (scheme == Qt::ColorScheme::Light) {
      return false;
    }
  }
  if (widget != nullptr) {
    return widget->palette().color(QPalette::Window).lightness() < 128;
  }
  return false;
}

ViewColors colorsFor(LargeFileView::AppearanceMode mode, const QWidget* widget) {
  const bool dark = (mode == LargeFileView::AppearanceMode::kDark) ||
                    (mode == LargeFileView::AppearanceMode::kFollowSystem && isSystemDarkMode(widget));
  if (dark) {
    return {
        QColor("#0f141a"),  // background
        QColor("#111a24"),  // gutter background
        QColor("#243244"),  // gutter border
        QColor("#e6edf6"),  // text
        QColor("#8a97ab"),  // line number
        QColor("#7cc4ff"),  // active line number
        QColor("#1a2636"),  // current row
        QColor("#223449"),  // current row gutter
        QColor("#2b4d78"),  // selection
        QColor("#7a5a1d"),  // match
        QColor("#f8fafc"),  // caret
        QColor("#9aa8bb"),  // hint
    };
  }
  return {
      QColor("#f7f7f7"),  // background
      QColor("#eef2f7"),  // gutter background
      QColor("#d1d5db"),  // gutter border
      QColor("#1f2937"),  // text
      QColor("#6b7280"),  // line number
      QColor("#1d4ed8"),  // active line number
      QColor("#eaf3ff"),  // current row
      QColor("#dbeafe"),  // current row gutter
      QColor("#bfdbfe"),  // selection
      QColor("#fde68a"),  // match
      QColor("#0f172a"),  // caret
      QColor("#374151"),  // hint
  };
}

int normalizeTabWidth(int spaces) {
  if (spaces <= 0) {
    return 4;
  }
  return std::clamp(spaces, 1, 16);
}

std::size_t advanceVisualColumn(std::size_t visual_column, QChar ch, int tab_width) {
  if (ch == QLatin1Char('\t')) {
    const std::size_t width = static_cast<std::size_t>(std::max(1, tab_width));
    const std::size_t remainder = visual_column % width;
    return visual_column + ((remainder == 0) ? width : (width - remainder));
  }
  return visual_column + 1;
}

}  // namespace

LargeFileView::LargeFileView(QWidget* parent) : QAbstractScrollArea(parent) {
  setFont(QFont(QStringLiteral("Menlo"), 12));
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::IBeamCursor);
  setAttribute(Qt::WA_InputMethodEnabled, true);
  viewport()->setAttribute(Qt::WA_InputMethodEnabled, true);

  caret_blink_timer_.setSingleShot(false);
  connect(&caret_blink_timer_, &QTimer::timeout, this, [this]() {
    if (!hasFocus()) {
      return;
    }
    caret_visible_ = !caret_visible_;
    viewport()->update();
  });

  connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
    const std::size_t new_top_line = static_cast<std::size_t>(std::max(0, value));
    if (top_line_ == new_top_line) {
      return;
    }
    top_line_ = new_top_line;
    emit scrollActivity(top_line_);
    viewport()->update();
  });

  if (QGuiApplication::styleHints() != nullptr) {
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
      if (appearance_mode_ == AppearanceMode::kFollowSystem) {
        viewport()->update();
      }
    });
    connect(QGuiApplication::styleHints(), &QStyleHints::cursorFlashTimeChanged, this, [this](int) {
      restartCaretBlinkTimer();
    });
  }

  restartCaretBlinkTimer();
}

void LargeFileView::setSession(core::DocumentSession* session) {
  if (session_ == session) {
    return;
  }

  if (session_ != nullptr) {
    disconnect(session_, nullptr, this, nullptr);
  }

  session_ = session;
  top_line_ = 0;
  cursor_line_ = 0;
  cursor_column_ = 0;
  preferred_column_ = 0;
  selection_anchor_set_ = false;
  selection_anchor_offset_ = 0;
  selection_caret_offset_ = 0;
  mouse_selecting_ = false;
  rows_cache_valid_ = false;
  cached_rows_.clear();
  cached_start_line_ = 0;
  cached_visible_lines_ = 0;

  if (session_ != nullptr) {
    connect(session_, &core::DocumentSession::changed, this, [this]() {
      selection_anchor_set_ = false;
      selection_anchor_offset_ = 0;
      selection_caret_offset_ = 0;
      rows_cache_valid_ = false;
      cached_rows_.clear();
      clampCursor();
      updateScrollbars();
      ensureCursorVisible(false);
      viewport()->update();
      emit cursorMoved(cursor_line_, cursor_column_);
    });
  }

  clampCursor();
  updateScrollbars();
  ensureCursorVisible(false);
  viewport()->update();
  emit cursorMoved(cursor_line_, cursor_column_);
}

void LargeFileView::setAppearanceMode(AppearanceMode mode) {
  if (appearance_mode_ == mode) {
    return;
  }
  appearance_mode_ = mode;
  viewport()->update();
}

LargeFileView::AppearanceMode LargeFileView::appearanceMode() const {
  return appearance_mode_;
}

std::size_t LargeFileView::cursorLine() const {
  return cursor_line_;
}

std::size_t LargeFileView::cursorColumn() const {
  return cursor_column_;
}

bool LargeFileView::hasSelection() const {
  return selection_anchor_set_ && selection_anchor_offset_ != selection_caret_offset_;
}

QString LargeFileView::selectedText() const {
  if (session_ == nullptr) {
    return {};
  }

  std::uint64_t start = 0;
  std::uint64_t end = 0;
  if (!selectedOffsets(&start, &end) || end <= start) {
    return {};
  }

  const std::string bytes = session_->bytesAt(start, static_cast<std::size_t>(end - start));
  if (bytes.empty()) {
    return {};
  }
  return session_->decodeBytesFromStorage(bytes);
}

bool LargeFileView::selectedByteRange(std::uint64_t* start, std::uint64_t* end) const {
  return selectedOffsets(start, end);
}

void LargeFileView::setTabWidth(int spaces) {
  const int normalized = normalizeTabWidth(spaces);
  if (tab_width_spaces_ == normalized) {
    return;
  }
  tab_width_spaces_ = normalized;
  viewport()->update();
  if (QInputMethod* input_method = QGuiApplication::inputMethod(); input_method != nullptr) {
    input_method->update(Qt::ImCursorRectangle | Qt::ImCursorPosition);
  }
}

int LargeFileView::tabWidth() const {
  return tab_width_spaces_;
}

bool LargeFileView::deleteSelection() {
  return deleteSelectionRange(nullptr);
}

void LargeFileView::pasteText(const QString& text) {
  if (session_ == nullptr || session_->isReadOnly() || text.isEmpty()) {
    return;
  }

  std::uint64_t insertion_offset = 0;
  if (hasSelection()) {
    if (!deleteSelectionRange(&insertion_offset)) {
      return;
    }
  } else if (!cursorOffset(&insertion_offset)) {
    return;
  }

  session_->insertText(insertion_offset, text);
  const QByteArray encoded = session_->encodeTextForStorage(text);
  const std::uint64_t new_offset = insertion_offset + static_cast<std::uint64_t>(encoded.size());
  if (!moveCursorToOffset(new_offset)) {
    viewport()->update();
  }
}

void LargeFileView::clearSelection() {
  selection_anchor_set_ = false;
  selection_anchor_offset_ = 0;
  selection_caret_offset_ = 0;
  viewport()->update();
}

void LargeFileView::scrollToLine(std::size_t line, bool center) {
  updateScrollbars();
  if (session_ == nullptr) {
    top_line_ = 0;
    return;
  }

  const int max_value = verticalScrollBar()->maximum();
  const int requested = (line > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                            ? std::numeric_limits<int>::max()
                            : static_cast<int>(line);
  int target = std::clamp(requested, 0, max_value);
  if (center) {
    const int half_page = std::max(1, verticalScrollBar()->pageStep() / 2);
    target = std::max(0, target - half_page);
  }

  top_line_ = static_cast<std::size_t>(target);
  emit scrollActivity(top_line_);
  verticalScrollBar()->setValue(target);
  viewport()->update();
}

void LargeFileView::setActiveMatch(std::size_t line, std::size_t column, std::size_t length) {
  has_active_match_ = true;
  active_match_line_ = line;
  active_match_column_ = column;
  active_match_length_ = std::max<std::size_t>(length, 1);
  viewport()->update();
}

void LargeFileView::clearActiveMatch() {
  has_active_match_ = false;
  active_match_line_ = 0;
  active_match_column_ = 0;
  active_match_length_ = 0;
  viewport()->update();
}

bool LargeFileView::goToLineColumn(std::size_t line, std::size_t column, bool center) {
  if (session_ == nullptr) {
    return false;
  }

  QString target_line = session_->lineAt(line);
  if (target_line.isNull()) {
    return false;
  }

  const std::size_t clamped_col =
      std::min<std::size_t>(column, static_cast<std::size_t>(target_line.size()));
  setCursorPosition(line, clamped_col);
  ensureCursorVisible(center);
  clearSelection();
  return true;
}

bool LargeFileView::goToOffset(std::uint64_t offset, bool center) {
  if (session_ == nullptr) {
    return false;
  }
  if (!moveCursorToOffset(offset)) {
    return false;
  }
  ensureCursorVisible(center);
  clearSelection();
  return true;
}

void LargeFileView::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);

  const ViewColors colors = colorsFor(appearance_mode_, this);
  QPainter painter(viewport());
  painter.fillRect(rect(), colors.background);
  const int gutter_width = gutterWidth();
  const int text_x = textOriginX();
  painter.fillRect(0, 0, gutter_width, viewport()->height(), colors.gutter_background);
  painter.setPen(colors.gutter_border);
  painter.drawLine(gutter_width - 1, 0, gutter_width - 1, viewport()->height());
  painter.setPen(colors.text);
  painter.setFont(font());

  const int line_height = fontMetrics().lineSpacing();
  const int text_baseline = fontMetrics().ascent();
  const QFontMetrics metrics = fontMetrics();
  const int visible_lines = std::max(1, viewport()->height() / line_height + 1);
  const std::size_t visible_line_count = static_cast<std::size_t>(visible_lines);

  if (session_ == nullptr) {
    painter.setPen(colors.hint);
    painter.drawText(text_x + 4, 24, trKey("hint.open_file_start"));
    return;
  }

  if (session_->byteSize() == 0) {
    painter.fillRect(gutter_width, 0, viewport()->width() - gutter_width, line_height, colors.current_row);
    painter.fillRect(0, 0, gutter_width, line_height, colors.current_row_gutter);
    painter.setPen(colors.line_number_active);
    painter.drawText(std::max(2, gutter_width - 6 - fontMetrics().horizontalAdvance(QStringLiteral("1"))),
                     text_baseline,
                     QStringLiteral("1"));
    if (hasFocus() && caret_visible_) {
      painter.setPen(colors.caret);
      const int caret_x = text_x;
      painter.drawLine(caret_x, 2, caret_x, line_height - 2);
    }
    return;
  }

  updateScrollbars();
  const std::size_t prefetch_back = std::max<std::size_t>(visible_line_count, 64);
  const std::size_t prefetch_ahead = std::max<std::size_t>(visible_line_count * 3, 192);
  const std::size_t requested_start =
      (top_line_ > prefetch_back) ? (top_line_ - prefetch_back) : static_cast<std::size_t>(0);
  const std::size_t requested_count = visible_line_count + prefetch_back + prefetch_ahead;

  const std::size_t cached_end =
      (cached_start_line_ > std::numeric_limits<std::size_t>::max() - cached_rows_.size())
          ? std::numeric_limits<std::size_t>::max()
          : (cached_start_line_ + cached_rows_.size());
  const std::size_t requested_end =
      (top_line_ > std::numeric_limits<std::size_t>::max() - visible_line_count)
          ? std::numeric_limits<std::size_t>::max()
          : (top_line_ + visible_line_count);
  const bool cache_covers_viewport =
      rows_cache_valid_ && cached_visible_lines_ == visible_line_count && top_line_ >= cached_start_line_ &&
      requested_end <= cached_end;

  if (!cache_covers_viewport) {
    cached_rows_ = session_->viewLines(requested_start, requested_count, 64ULL * 1024, 4096);
    cached_start_line_ = requested_start;
    cached_visible_lines_ = visible_line_count;
    rows_cache_valid_ = true;
  }
  const auto& rows = cached_rows_;
  const std::size_t first_visible_index =
      (top_line_ > cached_start_line_) ? (top_line_ - cached_start_line_) : static_cast<std::size_t>(0);
  std::uint64_t selection_start = 0;
  std::uint64_t selection_end = 0;
  const bool has_selection = selectedOffsets(&selection_start, &selection_end);
  for (int i = 0; i < visible_lines; ++i) {
    const std::size_t cache_index = first_visible_index + static_cast<std::size_t>(i);
    if (cache_index >= rows.size()) {
      break;
    }
    const auto& row = rows[cache_index];
    const std::size_t line_index = row.line_index;
    const int row_top = i * line_height;
    const QString& text = row.text;
    const QString display_text = expandTabsForDisplay(text);

    if (line_index == cursor_line_) {
      painter.fillRect(
          gutter_width, row_top, viewport()->width() - gutter_width, line_height, colors.current_row);
      painter.fillRect(0, row_top, gutter_width, line_height, colors.current_row_gutter);
    }

    const QString line_label = QString::number(static_cast<qulonglong>(line_index + 1));
    const int line_label_x = gutter_width - 6 - fontMetrics().horizontalAdvance(line_label);
    painter.setPen(line_index == cursor_line_ ? colors.line_number_active : colors.line_number);
    painter.drawText(std::max(2, line_label_x), row_top + text_baseline, line_label);
    painter.setPen(colors.text);

    if (has_selection) {
      const std::uint64_t line_start_offset = row.start_offset;
      const std::uint64_t line_end_offset = row.content_end_offset;
      if (selection_start < line_end_offset && selection_end > line_start_offset) {
        const auto columnForOffset = [this, &row](std::uint64_t offset) -> std::size_t {
          if (offset <= row.start_offset) {
            return 0;
          }
          if (offset >= row.content_end_offset) {
            return static_cast<std::size_t>(row.text.size());
          }
          const std::size_t take = static_cast<std::size_t>(offset - row.start_offset);
          const std::size_t available = std::min<std::size_t>(take, row.encoded.size());
          return static_cast<std::size_t>(
              session_
                  ->decodeBytesFromStorage(
                      std::string_view(row.encoded.data(), available))
                  .size());
        };

        const std::uint64_t clamped_start = std::max(selection_start, line_start_offset);
        const std::uint64_t clamped_end = std::min(selection_end, line_end_offset);
        const std::size_t sel_start_col = columnForOffset(clamped_start);
        const std::size_t sel_end_col = columnForOffset(clamped_end);

        if (sel_end_col > sel_start_col) {
          const int start_x = text_x + xForBufferColumn(text, sel_start_col, metrics);
          const int end_x = text_x + xForBufferColumn(text, sel_end_col, metrics);
          const int width = std::max(2, end_x - start_x);
          painter.fillRect(start_x, row_top + 1, width, line_height, colors.selection);
        }
      }
    }

    if (has_active_match_ && line_index == active_match_line_) {
      const std::size_t clamped_col =
          std::min<std::size_t>(active_match_column_, static_cast<std::size_t>(text.size()));
      const qsizetype remaining =
          text.size() - static_cast<qsizetype>(clamped_col);
      const std::size_t available =
          (remaining > 0) ? static_cast<std::size_t>(remaining) : 0;
      const std::size_t clamped_len = std::max<std::size_t>(
          1, std::min<std::size_t>(active_match_length_, available == 0 ? 1 : available));
      const std::size_t clamped_end_col = clamped_col + clamped_len;
      const int start_x = text_x + xForBufferColumn(text, clamped_col, metrics);
      const int end_x = text_x + xForBufferColumn(text, clamped_end_col, metrics);
      const int width = std::max(2, end_x - start_x);

      painter.fillRect(start_x, row_top + 1, width, line_height, colors.match);
    }

    painter.drawText(text_x, row_top + text_baseline, display_text);

    if (line_index == cursor_line_ && hasFocus() && caret_visible_) {
      const std::size_t clamped_col =
          std::min<std::size_t>(cursor_column_, static_cast<std::size_t>(text.size()));
      const int caret_x = text_x + xForBufferColumn(text, clamped_col, metrics);
      painter.setPen(colors.caret);
      painter.drawLine(caret_x, row_top + 2, caret_x, row_top + line_height - 2);
      painter.setPen(colors.text);
    }
  }
}

void LargeFileView::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  updateScrollbars();
  ensureCursorVisible(false);
}

void LargeFileView::updateScrollbars() {
  const int line_height = std::max(1, fontMetrics().lineSpacing());
  const int visible_lines = std::max(1, viewport()->height() / line_height);

  if (session_ == nullptr) {
    verticalScrollBar()->setRange(0, 0);
    verticalScrollBar()->setPageStep(1);
    return;
  }

  const std::size_t total_lines_size = session_->lineCount();
  const int total_lines =
      (total_lines_size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
          ? std::numeric_limits<int>::max()
          : static_cast<int>(total_lines_size);
  const int max_value = std::max(0, total_lines - visible_lines);

  verticalScrollBar()->setRange(0, max_value);
  verticalScrollBar()->setPageStep(visible_lines);

  if (static_cast<int>(top_line_) > max_value) {
    top_line_ = static_cast<std::size_t>(max_value);
    verticalScrollBar()->setValue(max_value);
  }
}

void LargeFileView::keyPressEvent(QKeyEvent* event) {
  if (session_ == nullptr) {
    QAbstractScrollArea::keyPressEvent(event);
    return;
  }

  const QString line_text = cursorLineText();
  const std::size_t line_len =
      line_text.isNull() ? 0 : static_cast<std::size_t>(line_text.size());
  const bool extend_selection = (event->modifiers() & Qt::ShiftModifier) != 0;
  const bool read_only = session_->isReadOnly();
  bool handled = false;

  auto ensureSelectionAnchorFromCursor = [this]() {
    if (selection_anchor_set_) {
      return;
    }
    std::uint64_t offset = 0;
    if (cursorOffset(&offset)) {
      selection_anchor_set_ = true;
      selection_anchor_offset_ = offset;
      selection_caret_offset_ = offset;
    }
  };
  auto updateSelectionCaretFromCursor = [this]() {
    if (!selection_anchor_set_) {
      return;
    }
    std::uint64_t offset = 0;
    if (cursorOffset(&offset)) {
      selection_caret_offset_ = offset;
    }
  };
  auto collapseSelection = [this](bool to_start) -> bool {
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    if (!selectedOffsets(&start, &end)) {
      return false;
    }
    (void)moveCursorToOffset(to_start ? start : end);
    clearSelection();
    return true;
  };

  switch (event->key()) {
    case Qt::Key_Left: {
      if (!extend_selection && collapseSelection(true)) {
        handled = true;
        break;
      }
      if (extend_selection) {
        ensureSelectionAnchorFromCursor();
      }

      if (cursor_column_ > 0) {
        setCursorPosition(cursor_line_, cursor_column_ - 1);
      } else if (cursor_line_ > 0) {
        const QString prev_line = session_->lineAt(cursor_line_ - 1);
        if (!prev_line.isNull()) {
          setCursorPosition(cursor_line_ - 1, static_cast<std::size_t>(prev_line.size()));
        }
      }
      if (extend_selection) {
        updateSelectionCaretFromCursor();
      } else {
        clearSelection();
      }
      handled = true;
      break;
    }
    case Qt::Key_Right: {
      if (!extend_selection && collapseSelection(false)) {
        handled = true;
        break;
      }
      if (extend_selection) {
        ensureSelectionAnchorFromCursor();
      }

      if (cursor_column_ < line_len) {
        setCursorPosition(cursor_line_, cursor_column_ + 1);
      } else {
        const QString next_line = session_->lineAt(cursor_line_ + 1);
        if (!next_line.isNull()) {
          setCursorPosition(cursor_line_ + 1, 0);
        }
      }
      if (extend_selection) {
        updateSelectionCaretFromCursor();
      } else {
        clearSelection();
      }
      handled = true;
      break;
    }
    case Qt::Key_Up: {
      if (extend_selection) {
        ensureSelectionAnchorFromCursor();
      }

      if (cursor_line_ > 0) {
        const QString target_line = session_->lineAt(cursor_line_ - 1);
        if (!target_line.isNull()) {
          const std::size_t target_col =
              std::min<std::size_t>(preferred_column_, static_cast<std::size_t>(target_line.size()));
          setCursorPosition(cursor_line_ - 1, target_col, true);
        }
      }
      if (extend_selection) {
        updateSelectionCaretFromCursor();
      } else {
        clearSelection();
      }
      handled = true;
      break;
    }
    case Qt::Key_Down: {
      if (extend_selection) {
        ensureSelectionAnchorFromCursor();
      }

      const QString target_line = session_->lineAt(cursor_line_ + 1);
      if (!target_line.isNull()) {
        const std::size_t target_col =
            std::min<std::size_t>(preferred_column_, static_cast<std::size_t>(target_line.size()));
        setCursorPosition(cursor_line_ + 1, target_col, true);
      }
      if (extend_selection) {
        updateSelectionCaretFromCursor();
      } else {
        clearSelection();
      }
      handled = true;
      break;
    }
    case Qt::Key_Home: {
      if (extend_selection) {
        ensureSelectionAnchorFromCursor();
      }
      setCursorPosition(cursor_line_, 0);
      if (extend_selection) {
        updateSelectionCaretFromCursor();
      } else {
        clearSelection();
      }
      handled = true;
      break;
    }
    case Qt::Key_End: {
      if (extend_selection) {
        ensureSelectionAnchorFromCursor();
      }
      setCursorPosition(cursor_line_, line_len);
      if (extend_selection) {
        updateSelectionCaretFromCursor();
      } else {
        clearSelection();
      }
      handled = true;
      break;
    }
    case Qt::Key_Backspace: {
      if (read_only) {
        handled = true;
        break;
      }
      if (hasSelection()) {
        (void)deleteSelectionRange(nullptr);
        handled = true;
        break;
      }

      if (cursor_column_ > 0) {
        const std::size_t target_line = cursor_line_;
        const std::size_t target_column = cursor_column_ - 1;
        std::uint64_t prev_offset = 0;
        std::uint64_t cur_offset = 0;
        if (session_->offsetForLineColumn(target_line, target_column, &prev_offset) &&
            cursorOffset(&cur_offset) && cur_offset >= prev_offset) {
          session_->removeText(prev_offset, cur_offset - prev_offset);
          setCursorPosition(target_line, target_column);
        }
      } else if (cursor_line_ > 0) {
        const std::size_t target_line = cursor_line_ - 1;
        const QString prev_line = session_->lineAt(target_line);
        if (!prev_line.isNull() && removeLineBreakBeforeCursor()) {
          setCursorPosition(target_line, static_cast<std::size_t>(prev_line.size()));
        }
      }
      clearSelection();
      handled = true;
      break;
    }
    case Qt::Key_Delete: {
      if (read_only) {
        handled = true;
        break;
      }
      if (hasSelection()) {
        (void)deleteSelectionRange(nullptr);
        handled = true;
        break;
      }
      const std::size_t target_line = cursor_line_;
      const std::size_t target_column = cursor_column_;

      if (cursor_column_ < line_len) {
        std::uint64_t cur_offset = 0;
        std::uint64_t next_offset = 0;
        if (cursorOffset(&cur_offset) &&
            session_->offsetForLineColumn(cursor_line_, cursor_column_ + 1, &next_offset) &&
            next_offset >= cur_offset) {
          session_->removeText(cur_offset, next_offset - cur_offset);
          setCursorPosition(target_line, target_column, true);
        }
      } else if (removeLineBreakAfterCursor()) {
        setCursorPosition(target_line, target_column, true);
      }
      clearSelection();
      handled = true;
      break;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter: {
      if (read_only) {
        handled = true;
        break;
      }
      handled = insertTextAtCursor(session_->lineEndingSequence());
      break;
    }
    case Qt::Key_Tab:
    case Qt::Key_Backtab: {
      if (read_only) {
        handled = true;
        break;
      }
      handled = insertTextAtCursor(QStringLiteral("\t"));
      break;
    }
    default:
      break;
  }

  if (!handled) {
    const Qt::KeyboardModifiers blocked = Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    const QString text = event->text();
    if (!read_only && (event->modifiers() & blocked) == 0 && !text.isEmpty() && text.at(0).isPrint()) {
      handled = insertTextAtCursor(text);
    }
  }

  if (handled) {
    event->accept();
    return;
  }
  QAbstractScrollArea::keyPressEvent(event);
}

void LargeFileView::inputMethodEvent(QInputMethodEvent* event) {
  if (event == nullptr) {
    return;
  }
  if (session_ == nullptr || session_->isReadOnly()) {
    event->ignore();
    return;
  }

  bool changed = false;
  const QString commit = event->commitString();
  if (!commit.isEmpty()) {
    changed = insertTextAtCursor(commit);
  }
  if (!event->preeditString().isEmpty()) {
    viewport()->update();
    changed = true;
  }

  if (changed) {
    event->accept();
  } else {
    event->ignore();
  }
}

QVariant LargeFileView::inputMethodQuery(Qt::InputMethodQuery query) const {
  switch (query) {
    case Qt::ImEnabled:
      return session_ != nullptr && !session_->isReadOnly();
    case Qt::ImCursorPosition:
      return static_cast<int>(cursor_column_);
    case Qt::ImAnchorPosition: {
      if (!hasSelection()) {
        return static_cast<int>(cursor_column_);
      }
      std::uint64_t start = 0;
      std::uint64_t end = 0;
      if (!selectedOffsets(&start, &end)) {
        return static_cast<int>(cursor_column_);
      }
      std::size_t anchor_line = 0;
      std::size_t anchor_column = 0;
      const std::uint64_t cursor_offset = selection_caret_offset_;
      const std::uint64_t anchor_offset =
          (cursor_offset >= selection_anchor_offset_) ? start : end;
      if (session_ == nullptr ||
          !session_->lineColumnForOffset(anchor_offset, &anchor_line, &anchor_column) ||
          anchor_line != cursor_line_) {
        return static_cast<int>(cursor_column_);
      }
      return static_cast<int>(anchor_column);
    }
    case Qt::ImCursorRectangle: {
      const int line_height = std::max(1, fontMetrics().lineSpacing());
      const QFontMetrics metrics = fontMetrics();
      const QString line_text = cursorLineText();
      const std::size_t line_len =
          line_text.isNull() ? 0 : static_cast<std::size_t>(line_text.size());
      const std::size_t clamped_col = std::min<std::size_t>(cursor_column_, line_len);
      const int caret_x = textOriginX() + xForBufferColumn(line_text, clamped_col, metrics);
      const std::size_t visual_row = (cursor_line_ >= top_line_) ? (cursor_line_ - top_line_) : 0;
      const int row = (visual_row > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                          ? std::numeric_limits<int>::max()
                          : static_cast<int>(visual_row);
      const int caret_y = row * line_height;
      const int caret_width = std::max(1, metrics.horizontalAdvance(QLatin1Char(' ')) / 8);
      const QRect viewport_rect(caret_x, caret_y, caret_width, line_height);
      const QPoint widget_top_left = viewport()->mapTo(this, viewport_rect.topLeft());
      return QRect(widget_top_left, viewport_rect.size());
    }
    case Qt::ImSurroundingText:
      return cursorLineText();
    case Qt::ImCurrentSelection:
      return selectedText();
    case Qt::ImFont:
      return font();
    default:
      break;
  }
  return QAbstractScrollArea::inputMethodQuery(query);
}

void LargeFileView::contextMenuEvent(QContextMenuEvent* event) {
  if (event == nullptr || session_ == nullptr) {
    QAbstractScrollArea::contextMenuEvent(event);
    return;
  }

  setFocus();

  const QPoint view_pos = viewport()->mapFromGlobal(event->globalPos());
  std::uint64_t click_offset = 0;
  if (positionToOffset(view_pos, &click_offset)) {
    std::uint64_t selection_start = 0;
    std::uint64_t selection_end = 0;
    const bool has_selection = selectedOffsets(&selection_start, &selection_end);
    if (!has_selection || click_offset < selection_start || click_offset >= selection_end) {
      (void)moveCursorToOffset(click_offset);
      clearSelection();
    }
  }

  QMenu menu(this);
  QAction* undo_action = menu.addAction(trKey("action.undo"));
  const bool read_only = session_->isReadOnly();
  undo_action->setEnabled(!read_only && session_->canUndo());
  QAction* redo_action = menu.addAction(trKey("action.redo"));
  redo_action->setEnabled(!read_only && session_->canRedo());
  menu.addSeparator();

  QAction* cut_action = menu.addAction(trKey("action.cut"));
  QAction* copy_action = menu.addAction(trKey("action.copy"));
  QAction* paste_action = menu.addAction(trKey("action.paste"));
  QAction* delete_action = menu.addAction(trKey("action.delete"));
  menu.addSeparator();
  QAction* select_all_action = menu.addAction(trKey("action.select_all"));

  const bool has_selection = hasSelection();
  cut_action->setEnabled(!read_only && has_selection);
  copy_action->setEnabled(has_selection);
  delete_action->setEnabled(!read_only && has_selection);

  const QClipboard* clipboard = QApplication::clipboard();
  paste_action->setEnabled(!read_only && clipboard != nullptr && !clipboard->text().isEmpty());
  select_all_action->setEnabled(session_->byteSize() > 0);

  QAction* chosen = menu.exec(event->globalPos());
  if (chosen == nullptr) {
    return;
  }

  if (chosen == undo_action) {
    (void)session_->undo();
    clearSelection();
    return;
  }
  if (chosen == redo_action) {
    (void)session_->redo();
    clearSelection();
    return;
  }
  if (chosen == cut_action) {
    QClipboard* editable_clipboard = QApplication::clipboard();
    if (editable_clipboard != nullptr) {
      editable_clipboard->setText(selectedText());
    }
    (void)deleteSelectionRange(nullptr);
    return;
  }
  if (chosen == copy_action) {
    QClipboard* editable_clipboard = QApplication::clipboard();
    if (editable_clipboard != nullptr) {
      editable_clipboard->setText(selectedText());
    }
    return;
  }
  if (chosen == paste_action) {
    const QClipboard* read_clipboard = QApplication::clipboard();
    if (read_clipboard != nullptr) {
      pasteText(read_clipboard->text());
    }
    return;
  }
  if (chosen == delete_action) {
    (void)deleteSelectionRange(nullptr);
    return;
  }
  if (chosen == select_all_action) {
    const std::uint64_t end = session_->byteSize();
    if (end == 0) {
      return;
    }
    selection_anchor_set_ = true;
    selection_anchor_offset_ = 0;
    selection_caret_offset_ = end;
    (void)moveCursorToOffset(end, true);
    viewport()->update();
  }
}

void LargeFileView::focusInEvent(QFocusEvent* event) {
  QAbstractScrollArea::focusInEvent(event);
  resetCaretBlink();
}

void LargeFileView::focusOutEvent(QFocusEvent* event) {
  QAbstractScrollArea::focusOutEvent(event);
  caret_blink_timer_.stop();
  caret_visible_ = true;
  viewport()->update();
}

bool LargeFileView::focusNextPrevChild(bool next) {
  Q_UNUSED(next);
  // Keep Tab/Backtab in the editor instead of focus traversal.
  return false;
}

void LargeFileView::clampCursor() {
  if (session_ == nullptr) {
    cursor_line_ = 0;
    cursor_column_ = 0;
    preferred_column_ = 0;
    return;
  }

  if (session_->byteSize() == 0) {
    cursor_line_ = 0;
    cursor_column_ = 0;
    preferred_column_ = 0;
    return;
  }

  const std::size_t total_lines = std::max<std::size_t>(1, session_->lineCount());
  if (cursor_line_ >= total_lines) {
    cursor_line_ = total_lines - 1;
  }

  QString line = session_->lineAt(cursor_line_);
  while (line.isNull() && cursor_line_ > 0) {
    --cursor_line_;
    line = session_->lineAt(cursor_line_);
  }
  if (line.isNull()) {
    cursor_line_ = 0;
    cursor_column_ = 0;
    preferred_column_ = 0;
    return;
  }

  cursor_column_ = std::min<std::size_t>(cursor_column_, static_cast<std::size_t>(line.size()));
  preferred_column_ = cursor_column_;
}

void LargeFileView::setCursorPosition(std::size_t line,
                                      std::size_t column,
                                      bool keep_preferred_column) {
  cursor_line_ = line;
  cursor_column_ = column;
  clampCursor();
  if (!keep_preferred_column) {
    preferred_column_ = cursor_column_;
  }
  ensureCursorVisible(false);
  viewport()->update();
  emit cursorMoved(cursor_line_, cursor_column_);
  resetCaretBlink();
  if (QInputMethod* input_method = QGuiApplication::inputMethod(); input_method != nullptr) {
    input_method->update(Qt::ImEnabled | Qt::ImCursorRectangle | Qt::ImCursorPosition |
                         Qt::ImAnchorPosition | Qt::ImCurrentSelection | Qt::ImSurroundingText);
  }
}

void LargeFileView::ensureCursorVisible(bool center) {
  if (session_ == nullptr) {
    return;
  }
  if (center) {
    scrollToLine(cursor_line_, true);
    return;
  }

  updateScrollbars();
  const int visible_lines = std::max(1, verticalScrollBar()->pageStep());
  int target_top = static_cast<int>(top_line_);
  const int cursor_line = (cursor_line_ > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                              ? std::numeric_limits<int>::max()
                              : static_cast<int>(cursor_line_);

  if (cursor_line < target_top) {
    target_top = cursor_line;
  } else if (cursor_line >= target_top + visible_lines) {
    target_top = cursor_line - visible_lines + 1;
  }
  target_top = std::clamp(target_top, 0, verticalScrollBar()->maximum());
  if (target_top != static_cast<int>(top_line_)) {
    top_line_ = static_cast<std::size_t>(target_top);
    verticalScrollBar()->setValue(target_top);
  }
}

int LargeFileView::gutterWidth() const {
  std::size_t total_lines = 1;
  if (session_ != nullptr) {
    total_lines = std::max<std::size_t>(1, session_->lineCount());
  }

  int digits = 1;
  while (total_lines >= 10) {
    total_lines /= 10;
    ++digits;
  }
  const int number_width = fontMetrics().horizontalAdvance(QString(digits, QLatin1Char('9')));
  return std::max(38, number_width + 14);
}

int LargeFileView::textOriginX() const {
  return gutterWidth() + 8;
}

QString LargeFileView::expandTabsForDisplay(const QString& text) const {
  if (!text.contains(QLatin1Char('\t'))) {
    return text;
  }

  QString expanded;
  expanded.reserve(text.size());
  std::size_t visual_column = 0;
  for (QChar ch : text) {
    if (ch == QLatin1Char('\t')) {
      const std::size_t next = advanceVisualColumn(visual_column, ch, tab_width_spaces_);
      const std::size_t count = next - visual_column;
      expanded.append(QString(static_cast<qsizetype>(count), QLatin1Char(' ')));
      visual_column = next;
      continue;
    }
    expanded.append(ch);
    ++visual_column;
  }
  return expanded;
}

std::size_t LargeFileView::visualColumnForBufferColumn(const QString& text, std::size_t column) const {
  const std::size_t clamped = std::min<std::size_t>(column, static_cast<std::size_t>(text.size()));
  std::size_t visual_column = 0;
  for (std::size_t i = 0; i < clamped; ++i) {
    visual_column = advanceVisualColumn(visual_column, text.at(static_cast<qsizetype>(i)), tab_width_spaces_);
  }
  return visual_column;
}

std::size_t LargeFileView::bufferColumnForVisualColumn(const QString& text,
                                                       std::size_t visual_column) const {
  if (visual_column == 0 || text.isEmpty()) {
    return 0;
  }

  std::size_t current_visual = 0;
  for (std::size_t i = 0; i < static_cast<std::size_t>(text.size()); ++i) {
    const std::size_t next_visual =
        advanceVisualColumn(current_visual, text.at(static_cast<qsizetype>(i)), tab_width_spaces_);
    if (visual_column < next_visual) {
      return (visual_column - current_visual < next_visual - visual_column) ? i : (i + 1);
    }
    if (visual_column == next_visual) {
      return i + 1;
    }
    current_visual = next_visual;
  }
  return static_cast<std::size_t>(text.size());
}

int LargeFileView::xForBufferColumn(const QString& text,
                                    std::size_t column,
                                    const QFontMetrics& metrics) const {
  const std::size_t clamped = std::min<std::size_t>(column, static_cast<std::size_t>(text.size()));
  std::size_t visual_column = 0;
  int x = 0;
  for (std::size_t i = 0; i < clamped; ++i) {
    const QChar ch = text.at(static_cast<qsizetype>(i));
    if (ch == QLatin1Char('\t')) {
      const std::size_t next_visual = advanceVisualColumn(visual_column, ch, tab_width_spaces_);
      const std::size_t space_count = next_visual - visual_column;
      x += metrics.horizontalAdvance(QString(static_cast<qsizetype>(space_count), QLatin1Char(' ')));
      visual_column = next_visual;
      continue;
    }
    x += metrics.horizontalAdvance(ch);
    visual_column = advanceVisualColumn(visual_column, ch, tab_width_spaces_);
  }
  return x;
}

std::size_t LargeFileView::bufferColumnForPixelX(const QString& text,
                                                 int x,
                                                 const QFontMetrics& metrics) const {
  if (x <= 0 || text.isEmpty()) {
    return 0;
  }

  std::size_t visual_column = 0;
  int current_x = 0;
  for (std::size_t i = 0; i < static_cast<std::size_t>(text.size()); ++i) {
    const QChar ch = text.at(static_cast<qsizetype>(i));
    int char_width = 0;
    if (ch == QLatin1Char('\t')) {
      const std::size_t next_visual = advanceVisualColumn(visual_column, ch, tab_width_spaces_);
      const std::size_t space_count = next_visual - visual_column;
      char_width = metrics.horizontalAdvance(QString(static_cast<qsizetype>(space_count), QLatin1Char(' ')));
      visual_column = next_visual;
    } else {
      char_width = metrics.horizontalAdvance(ch);
      visual_column = advanceVisualColumn(visual_column, ch, tab_width_spaces_);
    }
    const int next_x = current_x + std::max(0, char_width);
    if (x < next_x) {
      return (x - current_x < next_x - x) ? i : (i + 1);
    }
    if (x == next_x) {
      return i + 1;
    }
    current_x = next_x;
  }
  return static_cast<std::size_t>(text.size());
}

QString LargeFileView::cursorLineText() const {
  if (session_ == nullptr) {
    return QString();
  }
  if (session_->byteSize() == 0) {
    return QStringLiteral("");
  }
  const auto lines = session_->viewLines(cursor_line_, 1, 128ULL * 1024, 16384);
  if (lines.empty()) {
    return QStringLiteral("");
  }
  return lines.front().text;
}

bool LargeFileView::cursorOffset(std::uint64_t* offset) const {
  if (session_ == nullptr || offset == nullptr) {
    return false;
  }
  return session_->offsetForLineColumn(cursor_line_, cursor_column_, offset);
}

bool LargeFileView::removeLineBreakBeforeCursor() {
  if (session_ == nullptr || session_->isReadOnly()) {
    return false;
  }
  std::uint64_t offset = 0;
  if (!cursorOffset(&offset) || offset == 0) {
    return false;
  }

  std::uint64_t erase_offset = offset - 1;
  std::uint64_t erase_length = 1;
  const std::string prev = session_->bytesAt(offset - 1, 1);
  if (prev.empty()) {
    return false;
  }
  if (prev.front() == '\n' && offset >= 2) {
    const std::string prior = session_->bytesAt(offset - 2, 1);
    if (!prior.empty() && prior.front() == '\r') {
      erase_offset = offset - 2;
      erase_length = 2;
    }
  }

  session_->removeText(erase_offset, erase_length);
  return true;
}

bool LargeFileView::removeLineBreakAfterCursor() {
  if (session_ == nullptr || session_->isReadOnly()) {
    return false;
  }
  std::uint64_t offset = 0;
  if (!cursorOffset(&offset)) {
    return false;
  }

  const std::string bytes = session_->bytesAt(offset, 2);
  if (bytes.empty()) {
    return false;
  }

  if (bytes.size() >= 2 && bytes[0] == '\r' && bytes[1] == '\n') {
    session_->removeText(offset, 2);
    return true;
  }
  if (bytes[0] == '\n' || bytes[0] == '\r') {
    session_->removeText(offset, 1);
    return true;
  }
  return false;
}

void LargeFileView::mousePressEvent(QMouseEvent* event) {
  if (session_ == nullptr) {
    QAbstractScrollArea::mousePressEvent(event);
    return;
  }

  if (event->button() != Qt::LeftButton) {
    QAbstractScrollArea::mousePressEvent(event);
    return;
  }

  setFocus();
  std::uint64_t offset = 0;
  if (!positionToOffset(event->position().toPoint(), &offset)) {
    QAbstractScrollArea::mousePressEvent(event);
    return;
  }

  (void)moveCursorToOffset(offset);
  selection_anchor_set_ = true;
  selection_anchor_offset_ = offset;
  selection_caret_offset_ = offset;
  mouse_selecting_ = true;
  viewport()->update();
  event->accept();
}

void LargeFileView::mouseMoveEvent(QMouseEvent* event) {
  if (!mouse_selecting_ || session_ == nullptr) {
    QAbstractScrollArea::mouseMoveEvent(event);
    return;
  }

  std::uint64_t offset = 0;
  if (!positionToOffset(event->position().toPoint(), &offset)) {
    QAbstractScrollArea::mouseMoveEvent(event);
    return;
  }

  selection_caret_offset_ = offset;
  (void)moveCursorToOffset(offset, true);
  viewport()->update();
  event->accept();
}

void LargeFileView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !mouse_selecting_) {
    QAbstractScrollArea::mouseReleaseEvent(event);
    return;
  }

  mouse_selecting_ = false;
  std::uint64_t offset = 0;
  if (positionToOffset(event->position().toPoint(), &offset)) {
    selection_caret_offset_ = offset;
    (void)moveCursorToOffset(offset, true);
  }

  if (selection_anchor_set_ && selection_anchor_offset_ == selection_caret_offset_) {
    selection_anchor_set_ = false;
    selection_anchor_offset_ = 0;
    selection_caret_offset_ = 0;
  }

  viewport()->update();
  event->accept();
}

bool LargeFileView::positionToLineColumn(const QPoint& position,
                                         std::size_t* line,
                                         std::size_t* column) const {
  if (session_ == nullptr || line == nullptr || column == nullptr) {
    return false;
  }

  const int line_height = std::max(1, fontMetrics().lineSpacing());
  const int row = std::max(0, position.y() / line_height);
  std::size_t target_line = top_line_ + static_cast<std::size_t>(row);

  const auto lines = session_->viewLines(target_line, 1, 128ULL * 1024, 16384);
  if (lines.empty()) {
    *line = 0;
    *column = 0;
    return true;
  }
  const QString& text = lines.front().text;
  target_line = lines.front().line_index;

  const int left_padding = textOriginX();
  const int x = std::max(0, position.x() - left_padding);
  const std::size_t approx_col = bufferColumnForPixelX(text, x, fontMetrics());

  *line = target_line;
  *column = std::min<std::size_t>(approx_col, static_cast<std::size_t>(text.size()));
  return true;
}

bool LargeFileView::positionToOffset(const QPoint& position, std::uint64_t* offset) const {
  if (offset == nullptr) {
    return false;
  }
  *offset = 0;

  std::size_t line = 0;
  std::size_t column = 0;
  if (!positionToLineColumn(position, &line, &column)) {
    return false;
  }
  const auto lines = session_->viewLines(line, 1, 128ULL * 1024, 16384);
  if (lines.empty()) {
    return false;
  }

  const auto& row = lines.front();
  const std::size_t clamped_col =
      std::min<std::size_t>(column, static_cast<std::size_t>(row.text.size()));
  const QByteArray prefix = session_->encodeTextForStorage(
      row.text.left(static_cast<qsizetype>(clamped_col)));
  *offset = row.start_offset + static_cast<std::uint64_t>(prefix.size());
  return true;
}

bool LargeFileView::moveCursorToOffset(std::uint64_t offset, bool keep_preferred_column) {
  if (session_ == nullptr) {
    return false;
  }
  std::size_t line = 0;
  std::size_t column = 0;
  if (!session_->lineColumnForOffset(offset, &line, &column)) {
    return false;
  }
  setCursorPosition(line, column, keep_preferred_column);
  return true;
}

bool LargeFileView::selectedOffsets(std::uint64_t* start, std::uint64_t* end) const {
  if (!hasSelection()) {
    return false;
  }

  const std::uint64_t s = std::min(selection_anchor_offset_, selection_caret_offset_);
  const std::uint64_t e = std::max(selection_anchor_offset_, selection_caret_offset_);
  if (start != nullptr) {
    *start = s;
  }
  if (end != nullptr) {
    *end = e;
  }
  return true;
}

bool LargeFileView::deleteSelectionRange(std::uint64_t* start_offset) {
  if (session_ == nullptr || session_->isReadOnly()) {
    return false;
  }

  std::uint64_t start = 0;
  std::uint64_t end = 0;
  if (!selectedOffsets(&start, &end) || end <= start) {
    return false;
  }

  session_->removeText(start, end - start);
  selection_anchor_set_ = false;
  selection_anchor_offset_ = 0;
  selection_caret_offset_ = 0;

  if (start_offset != nullptr) {
    *start_offset = start;
  }
  (void)moveCursorToOffset(start);
  viewport()->update();
  return true;
}

bool LargeFileView::insertTextAtCursor(const QString& text) {
  if (session_ == nullptr || session_->isReadOnly() || text.isEmpty()) {
    return false;
  }

  std::uint64_t insertion_offset = 0;
  if (hasSelection()) {
    if (!deleteSelectionRange(&insertion_offset)) {
      return false;
    }
  } else if (!cursorOffset(&insertion_offset)) {
    return false;
  }

  session_->insertText(insertion_offset, text);
  const QByteArray encoded = session_->encodeTextForStorage(text);
  const std::uint64_t new_offset = insertion_offset + static_cast<std::uint64_t>(encoded.size());
  if (!moveCursorToOffset(new_offset)) {
    viewport()->update();
  }
  clearSelection();
  return true;
}

void LargeFileView::restartCaretBlinkTimer() {
  const int flash_time = QApplication::cursorFlashTime();
  if (flash_time <= 0) {
    caret_blink_timer_.stop();
    caret_visible_ = true;
    viewport()->update();
    return;
  }

  const int interval = std::max(150, flash_time / 2);
  caret_blink_timer_.setInterval(interval);
  if (hasFocus()) {
    caret_blink_timer_.start();
  } else {
    caret_blink_timer_.stop();
  }
}

void LargeFileView::resetCaretBlink() {
  caret_visible_ = true;
  restartCaretBlinkTimer();
  viewport()->update();
}

}  // namespace massiveedit::ui
