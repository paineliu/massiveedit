#include "massiveedit/ui/main_window.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDataStream>
#include <QDate>
#include <QDir>
#include <QDirIterator>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSysInfo>
#include <QTableWidget>
#include <QTabBar>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

#include "massiveedit/ui/large_file_view.h"
#include "massiveedit/ui/i18n.h"

namespace massiveedit::ui {
namespace {

#ifndef MASSIVEEDIT_VERSION
#define MASSIVEEDIT_VERSION "dev"
#endif

#ifndef MASSIVEEDIT_BUILD_CONFIG
#define MASSIVEEDIT_BUILD_CONFIG "Unknown"
#endif

constexpr int kMaxRecentFiles = 12;
constexpr auto kSettingsOrg = "MassiveEdit";
constexpr auto kSettingsApp = "MassiveEdit";
constexpr auto kRecentFilesKey = "recent_files";
constexpr auto kUiLanguageKey = "ui_language";
constexpr auto kUiAppearanceKey = "ui_appearance";
constexpr auto kUiTabWidthKey = "ui_tab_width_spaces";
constexpr auto kStartupRestoreSessionKey = "startup_restore_session";
constexpr auto kLastExitCleanKey = "last_exit_clean_v1";
constexpr auto kSessionTabsKey = "session_tabs_v1";
constexpr auto kSessionActiveTabKey = "session_active_tab_v1";
constexpr auto kSessionNextUntitledKey = "session_next_untitled_v1";
constexpr auto kShortcutOverridesKey = "shortcut_overrides_v1";
constexpr std::uint32_t kSnapshotMagic = 0x4D454453;  // MEDS
constexpr std::uint32_t kSnapshotVersion = 1;
constexpr std::uint64_t kMaxSwitchSnapshotBytes = 64ULL * 1024 * 1024;
constexpr int kMaxFindInFilesResults = 5000;
constexpr std::uint64_t kLargePasteConfirmBytes = 8ULL * 1024 * 1024;
constexpr std::uint64_t kLargeReplaceConfirmBytes = 32ULL * 1024 * 1024;
constexpr auto kAboutGithubUser = "paineliu";
constexpr auto kAboutContactEmail = "liutingchao@hotmail.com";
constexpr auto kAboutProjectUrl = "https://github.com/paineliu/massiveedit";
constexpr auto kAboutIssuesUrl = "https://github.com/paineliu/massiveedit/issues";
constexpr auto kAboutLicenseUrl = "https://github.com/paineliu/massiveedit/blob/main/LICENSE";

LargeFileView::AppearanceMode appearanceModeFromCode(const QString& code) {
  const QString normalized = code.trimmed();
  if (normalized.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
    return LargeFileView::AppearanceMode::kDark;
  }
  if (normalized.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0) {
    return LargeFileView::AppearanceMode::kLight;
  }
  return LargeFileView::AppearanceMode::kFollowSystem;
}

QString appearanceCode(LargeFileView::AppearanceMode mode) {
  switch (mode) {
    case LargeFileView::AppearanceMode::kDark:
      return QStringLiteral("dark");
    case LargeFileView::AppearanceMode::kLight:
      return QStringLiteral("light");
    case LargeFileView::AppearanceMode::kFollowSystem:
    default:
      return QStringLiteral("system");
  }
}

int normalizeTabWidthValue(int value) {
  if (value == 2 || value == 4 || value == 8) {
    return value;
  }
  return 4;
}

QString normalizedSnippet(QString line) {
  line.replace(QLatin1Char('\t'), QLatin1Char(' '));
  line.replace(QLatin1Char('\r'), QLatin1Char(' '));
  line.replace(QLatin1Char('\n'), QLatin1Char(' '));
  return line.simplified();
}

enum class ReplaceScopeChoice {
  kSelection = 0,
  kCurrentFile = 1,
  kAllOpenTabs = 2,
};

std::vector<core::SearchMatch> nonOverlappingMatchesInRange(const std::vector<core::SearchMatch>& matches,
                                                            std::uint64_t range_start,
                                                            std::uint64_t range_end,
                                                            std::size_t limit = 0) {
  if (range_end <= range_start) {
    return {};
  }

  std::vector<core::SearchMatch> sorted = matches;
  std::sort(sorted.begin(), sorted.end(), [](const core::SearchMatch& lhs, const core::SearchMatch& rhs) {
    if (lhs.offset != rhs.offset) {
      return lhs.offset < rhs.offset;
    }
    return lhs.length < rhs.length;
  });

  std::vector<core::SearchMatch> out;
  out.reserve(sorted.size());
  std::uint64_t next_allowed = range_start;
  for (const core::SearchMatch& match : sorted) {
    if (match.length == 0) {
      continue;
    }
    const std::uint64_t match_end =
        (match.offset > std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(match.length))
            ? std::numeric_limits<std::uint64_t>::max()
            : (match.offset + static_cast<std::uint64_t>(match.length));
    if (match.offset < range_start || match_end > range_end) {
      continue;
    }
    if (match.offset < next_allowed) {
      continue;
    }
    out.push_back(match);
    next_allowed = match_end;
    if (limit > 0 && out.size() >= limit) {
      break;
    }
  }
  return out;
}

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

bool looksBinary(const QByteArray& probe) {
  if (probe.isEmpty()) {
    return false;
  }
  const int nul = probe.indexOf('\0');
  if (nul >= 0) {
    return true;
  }
  int suspicious = 0;
  for (const char c : probe) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 0x09 && uc != 0x00) {
      ++suspicious;
    }
  }
  return suspicious > (probe.size() / 16);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  QSettings settings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
  const bool last_exit_clean =
      settings.value(QString::fromLatin1(kLastExitCleanKey), true).toBool();
  settings.setValue(QString::fromLatin1(kLastExitCleanKey), false);
  i18n::setCurrentLanguage(
      i18n::languageFromCode(settings.value(QString::fromLatin1(kUiLanguageKey), QStringLiteral("en"))
                                 .toString()));
  appearance_mode_ = appearanceModeFromCode(
      settings.value(QString::fromLatin1(kUiAppearanceKey), QStringLiteral("system")).toString());
  tab_width_spaces_ = normalizeTabWidthValue(
      settings.value(QString::fromLatin1(kUiTabWidthKey), 4).toInt());
  restore_session_on_startup_ =
      settings.value(QString::fromLatin1(kStartupRestoreSessionKey), false).toBool();

  setWindowTitle(trKey("app.name"));
  resize(1280, 820);

  QWidget* container = new QWidget(this);
  auto* root_layout = new QVBoxLayout(container);
  root_layout->setContentsMargins(0, 0, 0, 0);
  root_layout->setSpacing(0);

  search_panel_ = new QWidget(container);
  root_layout->addWidget(search_panel_);

  tab_bar_ = new QTabBar(container);
  tab_bar_->setExpanding(false);
  tab_bar_->setMovable(true);
  tab_bar_->setTabsClosable(true);
  root_layout->addWidget(tab_bar_);

  view_ = new LargeFileView(container);
  view_->setSession(&session_);
  view_->setAppearanceMode(appearance_mode_);
  view_->setTabWidth(tab_width_spaces_);
  root_layout->addWidget(view_, 1);

  setCentralWidget(container);

  buildSearchPanel();
  buildPerformancePanel();
  buildFindResultsPanel();
  buildMenus();

  file_watcher_ = new QFileSystemWatcher(this);
  connect(file_watcher_, &QFileSystemWatcher::fileChanged, this, &MainWindow::handleWatchedFileChanged);

  autosave_timer_ = new QTimer(this);
  autosave_timer_->setInterval(15000);
  connect(autosave_timer_, &QTimer::timeout, this, &MainWindow::autosaveCurrentTab);
  autosave_timer_->start();

  perf_timer_ = new QTimer(this);
  perf_timer_->setInterval(1000);
  connect(perf_timer_, &QTimer::timeout, this, &MainWindow::updatePerformancePanel);
  perf_timer_->start();

  refresh_timer_ = new QTimer(this);
  refresh_timer_->setSingleShot(true);
  refresh_timer_->setInterval(33);
  connect(refresh_timer_, &QTimer::timeout, this, &MainWindow::refreshWindowState);

  scroll_idle_timer_ = new QTimer(this);
  scroll_idle_timer_->setSingleShot(true);
  scroll_idle_timer_->setInterval(140);
  connect(scroll_idle_timer_, &QTimer::timeout, this, [this]() {
    view_scrolling_ = false;
    session_.setIndexPriority(core::DocumentSession::IndexPriority::kBackground);
    if (scroll_status_timer_ != nullptr) {
      scroll_status_timer_->stop();
    }
    if (pending_refresh_while_scrolling_) {
      pending_refresh_while_scrolling_ = false;
      refreshWindowState();
      return;
    }
    updateStatusBar();
  });

  scroll_status_timer_ = new QTimer(this);
  scroll_status_timer_->setInterval(180);
  connect(scroll_status_timer_, &QTimer::timeout, this, [this]() {
    if (view_scrolling_) {
      updateStatusBar();
    }
  });

  connect(tab_bar_, &QTabBar::currentChanged, this, &MainWindow::handleTabChanged);
  connect(tab_bar_, &QTabBar::tabCloseRequested, this, &MainWindow::handleTabCloseRequested);

  connect(&session_, &core::DocumentSession::changed, this, [this]() {
    if (view_scrolling_) {
      pending_refresh_while_scrolling_ = true;
      return;
    }
    if (refresh_timer_ == nullptr) {
      refreshWindowState();
      return;
    }
    if (!refresh_timer_->isActive()) {
      refresh_timer_->start();
    }
  });
  connect(&session_,
          &core::DocumentSession::undoRedoStateChanged,
          this,
          [this](bool can_undo, bool can_redo) {
            if (undo_action_ != nullptr) {
              undo_action_->setEnabled(can_undo);
            }
            if (redo_action_ != nullptr) {
              redo_action_->setEnabled(can_redo);
            }
          });
  connect(&session_, &core::DocumentSession::searchCompleted, this, &MainWindow::handleSearchCompleted);
  connect(view_, &LargeFileView::cursorMoved, this, [this](std::size_t line, std::size_t column) {
    cursor_line_ = line;
    cursor_column_ = column;
    if (hasCurrentTab()) {
      tabs_[currentTabIndex()].cursor_line = line;
      tabs_[currentTabIndex()].cursor_column = column;
    }
    updateStatusBar();
  });
  connect(view_, &LargeFileView::scrollActivity, this, [this](std::size_t /*top_line*/) {
    view_scrolling_ = true;
    session_.setIndexPriority(core::DocumentSession::IndexPriority::kInteractive);
    if (scroll_idle_timer_ != nullptr) {
      scroll_idle_timer_->start();
    }
    if (scroll_status_timer_ != nullptr && !scroll_status_timer_->isActive()) {
      scroll_status_timer_->start();
    }
  });

  loadRecentFiles();
  rebuildRecentFilesMenu();
  const bool restored_session = restore_session_on_startup_ && restoreSessionState();
  if (!restored_session && !last_exit_clean) {
    restoreRecoverySnapshotsIfAny();
  } else if (last_exit_clean) {
    clearAllRecoverySnapshots();
  }
  if (tabs_.empty()) {
    newTab();
  }

  syncFormatActions();
  updateFileWatcher();
  session_.setIndexPriority(core::DocumentSession::IndexPriority::kBackground);
  refreshWindowState();
  statusBar()->showMessage(trKey("status.ready"));
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (event == nullptr) {
    return;
  }

  if (!ensureCanCloseAllTabs()) {
    event->ignore();
    return;
  }

  (void)saveCurrentTabState();
  saveSessionState();
  cleanupOrphanSnapshots();
  {
    QSettings settings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
    settings.setValue(QString::fromLatin1(kLastExitCleanKey), true);
  }
  event->accept();
}

void MainWindow::newTab() {
  if (!saveCurrentTabState()) {
    return;
  }

  TabState tab;
  tab.untitled_id = next_untitled_index_++;
  tab.untitled_label = untitledNameForIndex(tab.untitled_id);
  tab.snapshot_path = snapshotPathForTab(static_cast<int>(tabs_.size()));
  tab.operation_log_path = operationLogPathForTab(static_cast<int>(tabs_.size()));
  tabs_.push_back(tab);

  const int new_index = static_cast<int>(tabs_.size() - 1);
  tab_bar_->addTab(tabDisplayName(tabs_[static_cast<std::size_t>(new_index)]));
  if (!switchToTab(new_index)) {
    tabs_.pop_back();
    tab_bar_->removeTab(new_index);
  }
}

void MainWindow::openFile() {
  const QString path = QFileDialog::getOpenFileName(this, trKey("title.open_file"));
  if (path.isEmpty()) {
    return;
  }
  (void)openFileInNewTab(path);
}

bool MainWindow::openFileInNewTab(const QString& path) {
  const QString absolute_path = QFileInfo(path).absoluteFilePath();
  if (absolute_path.isEmpty()) {
    return false;
  }

  for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
    if (tabs_[i].file_path == absolute_path && !tabs_[i].snapshot_dirty) {
      return switchToTab(i);
    }
  }

  if (!saveCurrentTabState()) {
    return false;
  }

  TabState tab;
  tab.file_path = absolute_path;
  tab.snapshot_path = snapshotPathForTab(static_cast<int>(tabs_.size()));
  tab.operation_log_path = operationLogPathForTab(static_cast<int>(tabs_.size()));
  tabs_.push_back(tab);

  const int index = static_cast<int>(tabs_.size() - 1);
  tab_bar_->addTab(QFileInfo(absolute_path).fileName());
  if (!switchToTab(index)) {
    tabs_.pop_back();
    tab_bar_->removeTab(index);
    return false;
  }

  addRecentFile(absolute_path);
  return true;
}

void MainWindow::handleTabChanged(int index) {
  if (index < 0 || index >= static_cast<int>(tabs_.size())) {
    return;
  }
  if (index == active_tab_index_) {
    return;
  }

  if (!switchToTab(index)) {
    QSignalBlocker blocker(tab_bar_);
    if (active_tab_index_ >= 0 && active_tab_index_ < tab_bar_->count()) {
      tab_bar_->setCurrentIndex(active_tab_index_);
    }
  }
}

void MainWindow::handleTabCloseRequested(int index) {
  if (index < 0 || index >= static_cast<int>(tabs_.size())) {
    return;
  }

  if (index != active_tab_index_ && !switchToTab(index)) {
    return;
  }

  if (!ensureCanAbandonCurrentDocument()) {
    return;
  }

  const int closing_index = active_tab_index_;
  if (closing_index < 0 || closing_index >= static_cast<int>(tabs_.size())) {
    return;
  }

  (void)removeSnapshotFile(tabs_[closing_index].snapshot_path);
  if (!tabs_[closing_index].operation_log_path.isEmpty()) {
    (void)QFile::remove(tabs_[closing_index].operation_log_path);
  }
  tabs_.erase(tabs_.begin() + closing_index);
  tab_bar_->removeTab(closing_index);

  if (tabs_.empty()) {
    active_tab_index_ = -1;
    newTab();
    return;
  }

  const int next_index = std::min(closing_index, static_cast<int>(tabs_.size() - 1));
  active_tab_index_ = -1;
  (void)switchToTab(next_index);
}

void MainWindow::closeCurrentTab() {
  if (!hasCurrentTab()) {
    return;
  }
  handleTabCloseRequested(active_tab_index_);
}

bool MainWindow::switchToTab(int index) {
  if (index < 0 || index >= static_cast<int>(tabs_.size())) {
    return false;
  }

  if (active_tab_index_ == index && hasCurrentTab()) {
    return true;
  }

  if (!saveCurrentTabState()) {
    return false;
  }

  const int previous_index = active_tab_index_;
  active_tab_index_ = index;
  if (!loadTabIntoSession(index)) {
    active_tab_index_ = previous_index;
    return false;
  }

  {
    QSignalBlocker blocker(tab_bar_);
    tab_bar_->setCurrentIndex(index);
  }

  clearSearchState();
  refreshWindowState();
  updateFileWatcher();
  updatePerformancePanel();
  return true;
}

bool MainWindow::loadTabIntoSession(int index) {
  if (index < 0 || index >= static_cast<int>(tabs_.size())) {
    return false;
  }

  TabState& tab = tabs_[index];
  bool loaded = false;
  bool loaded_from_snapshot = false;

  if (!tab.snapshot_path.isEmpty() && QFileInfo::exists(tab.snapshot_path)) {
    SnapshotPayload payload;
    if (readSnapshotFile(tab.snapshot_path, &payload)) {
      if (!session_.openFromBytes(payload.content, payload.file_path, payload.dirty)) {
        return false;
      }
      session_.setTextEncoding(payload.encoding);
      session_.setLineEnding(payload.line_ending);
      tab.file_path = payload.file_path;
      tab.snapshot_dirty = payload.dirty;
      tab.encoding = payload.encoding;
      tab.line_ending = payload.line_ending;
      tab.cursor_line = payload.cursor_line;
      tab.cursor_column = payload.cursor_column;
      tab.bookmarks = payload.bookmarks;
      loaded = true;
      loaded_from_snapshot = true;
    } else {
      (void)removeSnapshotFile(tab.snapshot_path);
      tab.snapshot_dirty = false;
    }
  }

  if (!loaded) {
    if (!tab.operation_log_path.isEmpty() && QFileInfo::exists(tab.operation_log_path)) {
      QString error;
      suppress_file_change_prompt_ = true;
      const bool ok = session_.restoreFromOperationLog(tab.operation_log_path, &error);
      suppress_file_change_prompt_ = false;
      if (ok) {
        tab.file_path = session_.filePath();
        tab.snapshot_dirty = session_.isDirty();
        tab.encoding = session_.textEncoding();
        tab.line_ending = session_.lineEnding();
        loaded = true;
      } else if (!tab.file_path.isEmpty()) {
        // Fall back to opening disk file if operation-log restore fails.
      } else {
        QMessageBox::critical(this, trKey("msg.open_failed"), error);
        return false;
      }
    }
  }

  if (!loaded) {
    if (!tab.file_path.isEmpty()) {
      QString error;
      suppress_file_change_prompt_ = true;
      const bool ok = session_.openFile(tab.file_path, &error);
      suppress_file_change_prompt_ = false;
      if (!ok) {
        QMessageBox::critical(this, trKey("msg.open_failed"), error);
        return false;
      }
    } else {
      if (!session_.openFromBytes({}, QString(), false)) {
        return false;
      }
    }
    session_.setTextEncoding(tab.encoding);
    session_.setLineEnding(tab.line_ending);
  }

  if (!loaded_from_snapshot && !tab.snapshot_dirty && !tab.operation_log_path.isEmpty() &&
      QFileInfo::exists(tab.operation_log_path)) {
    // Keep operation logs only for unsaved states.
    (void)QFile::remove(tab.operation_log_path);
  }
  session_.setReadOnly(tab.read_only);

  view_->setSession(&session_);
  if (!view_->goToLineColumn(tab.cursor_line, tab.cursor_column, false)) {
    cursor_line_ = 0;
    cursor_column_ = 0;
  }

  return true;
}

bool MainWindow::saveCurrentTabState() {
  if (!hasCurrentTab()) {
    return true;
  }

  TabState& tab = tabs_[currentTabIndex()];
  if (tab.operation_log_path.isEmpty()) {
    tab.operation_log_path = operationLogPathForTab(currentTabIndex());
  }
  tab.cursor_line = cursor_line_;
  tab.cursor_column = cursor_column_;
  tab.encoding = session_.textEncoding();
  tab.line_ending = session_.lineEnding();
  tab.read_only = session_.isReadOnly();

  const bool needs_snapshot = session_.isDirty() || session_.filePath().isEmpty();
  if (!needs_snapshot) {
    tab.snapshot_dirty = false;
    tab.file_path = session_.filePath();
    (void)removeSnapshotFile(tab.snapshot_path);
    if (!tab.operation_log_path.isEmpty()) {
      (void)QFile::remove(tab.operation_log_path);
    }
    updateTabTitle(currentTabIndex());
    return true;
  }

  bool operation_log_written = false;
  if (session_.isDirty() && !session_.filePath().isEmpty() && !tab.operation_log_path.isEmpty()) {
    QString op_error;
    operation_log_written = session_.saveOperationLog(tab.operation_log_path, &op_error);
    if (!operation_log_written && !op_error.isEmpty()) {
      statusBar()->showMessage(op_error, 4000);
    }
  }

  const std::uint64_t size = session_.byteSize();
  if (size > kMaxSwitchSnapshotBytes) {
    if (operation_log_written) {
      tab.file_path = session_.filePath();
      tab.snapshot_dirty = true;
      (void)removeSnapshotFile(tab.snapshot_path);
      updateTabTitle(currentTabIndex());
      return true;
    }

    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        trKey("title.large_unsaved"),
        trKey("msg.large_unsaved"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Save) {
      if (!saveCurrentFile()) {
        return false;
      }
      tab.snapshot_dirty = false;
      tab.file_path = session_.filePath();
      (void)removeSnapshotFile(tab.snapshot_path);
      if (!tab.operation_log_path.isEmpty()) {
        (void)QFile::remove(tab.operation_log_path);
      }
      updateTabTitle(currentTabIndex());
      return true;
    }
    if (choice == QMessageBox::Cancel) {
      return false;
    }
    tab.snapshot_dirty = false;
    (void)removeSnapshotFile(tab.snapshot_path);
    if (!tab.operation_log_path.isEmpty()) {
      (void)QFile::remove(tab.operation_log_path);
    }
    return true;
  }

  QByteArray content;
  if (size > 0) {
    const std::string bytes = session_.bytesAt(0, static_cast<std::size_t>(size));
    content = QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size()));
  }

  SnapshotPayload payload;
  payload.file_path = session_.filePath();
  payload.dirty = session_.isDirty();
  payload.encoding = session_.textEncoding();
  payload.line_ending = session_.lineEnding();
  payload.cursor_line = cursor_line_;
  payload.cursor_column = cursor_column_;
  payload.bookmarks = tab.bookmarks;
  payload.content = std::move(content);

  if (!writeSnapshotFile(tab.snapshot_path, payload)) {
    QMessageBox::warning(this, trKey("msg.snapshot_failed"), trKey("msg.snapshot_failed_detail"));
    return false;
  }

  tab.file_path = payload.file_path;
  tab.snapshot_dirty = payload.dirty;
  if (!tab.snapshot_dirty && !tab.operation_log_path.isEmpty()) {
    (void)QFile::remove(tab.operation_log_path);
  }
  updateTabTitle(currentTabIndex());
  return true;
}

bool MainWindow::ensureCanCloseAllTabs() {
  if (tabs_.empty()) {
    return true;
  }

  const int original_active = active_tab_index_;
  for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
    const bool maybe_dirty = (i == active_tab_index_) ? session_.isDirty() : tabs_[i].snapshot_dirty;
    if (!maybe_dirty) {
      continue;
    }

    if (i != active_tab_index_ && !switchToTab(i)) {
      if (original_active >= 0 && original_active < static_cast<int>(tabs_.size())) {
        (void)switchToTab(original_active);
      }
      return false;
    }

    if (!session_.isDirty()) {
      continue;
    }
    if (!ensureCanAbandonCurrentDocument()) {
      if (original_active >= 0 && original_active < static_cast<int>(tabs_.size())) {
        (void)switchToTab(original_active);
      }
      return false;
    }
  }

  if (original_active >= 0 && original_active < static_cast<int>(tabs_.size())) {
    (void)switchToTab(original_active);
  }
  return true;
}

bool MainWindow::ensureCanAbandonCurrentDocument() {
  if (!session_.isDirty()) {
    return true;
  }

  const QMessageBox::StandardButton choice = QMessageBox::warning(
      this,
      trKey("msg.unsaved_changes"),
      trKey("msg.unsaved_continue"),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save);

  if (choice == QMessageBox::Save) {
    return saveCurrentFile();
  }
  if (choice == QMessageBox::Discard) {
    if (hasCurrentTab()) {
      TabState& tab = tabs_[currentTabIndex()];
      QString error;
      bool reset_ok = true;
      suppress_file_change_prompt_ = true;
      if (!tab.file_path.isEmpty()) {
        reset_ok = session_.openFile(tab.file_path, &error);
      } else {
        reset_ok = session_.openFromBytes({}, QString(), false);
      }
      suppress_file_change_prompt_ = false;
      if (!reset_ok) {
        QMessageBox::critical(this, trKey("msg.discard_failed"), error);
        return false;
      }
      tab.snapshot_dirty = false;
      (void)removeSnapshotFile(tab.snapshot_path);
      if (!tab.operation_log_path.isEmpty()) {
        (void)QFile::remove(tab.operation_log_path);
      }
    }
    return true;
  }
  return false;
}

void MainWindow::saveFile() {
  (void)saveCurrentFile();
}

bool MainWindow::saveCurrentFile() {
  if (!hasCurrentTab()) {
    return false;
  }

  QString path = session_.filePath();
  if (path.isEmpty()) {
    return saveAsInteractive();
  }

  QString error;
  suppress_file_change_prompt_ = true;
  const bool ok = session_.saveAs(path, &error);
  suppress_file_change_prompt_ = false;
  if (!ok) {
    QMessageBox::critical(this, trKey("msg.save_failed"), error);
    return false;
  }

  TabState& tab = tabs_[currentTabIndex()];
  tab.file_path = session_.filePath();
  tab.snapshot_dirty = false;
  tab.encoding = session_.textEncoding();
  tab.line_ending = session_.lineEnding();
  (void)removeSnapshotFile(tab.snapshot_path);
  if (!tab.operation_log_path.isEmpty()) {
    (void)QFile::remove(tab.operation_log_path);
  }

  addRecentFile(path);
  refreshWindowState();
  updateFileWatcher();
  statusBar()->showMessage(trKey("msg.saved"), 3000);
  return true;
}

void MainWindow::saveAs() {
  (void)saveAsInteractive();
}

bool MainWindow::saveAsInteractive() {
  if (!hasCurrentTab()) {
    return false;
  }

  const QString path = QFileDialog::getSaveFileName(this, trKey("title.save_as"));
  if (path.isEmpty()) {
    return false;
  }

  QString error;
  suppress_file_change_prompt_ = true;
  const bool ok = session_.saveAs(path, &error);
  suppress_file_change_prompt_ = false;
  if (!ok) {
    QMessageBox::critical(this, trKey("msg.save_failed"), error);
    return false;
  }

  TabState& tab = tabs_[currentTabIndex()];
  tab.file_path = session_.filePath();
  tab.snapshot_dirty = false;
  tab.encoding = session_.textEncoding();
  tab.line_ending = session_.lineEnding();
  (void)removeSnapshotFile(tab.snapshot_path);
  if (!tab.operation_log_path.isEmpty()) {
    (void)QFile::remove(tab.operation_log_path);
  }

  addRecentFile(path);
  refreshWindowState();
  updateFileWatcher();
  statusBar()->showMessage(trKey("msg.saved"), 3000);
  return true;
}

void MainWindow::goToLocation() {
  if (!hasCurrentTab() || view_ == nullptr) {
    return;
  }

  const QString initial = QStringLiteral("%1:%2")
                              .arg(static_cast<qulonglong>(cursor_line_ + 1))
                              .arg(static_cast<qulonglong>(cursor_column_ + 1));
  bool ok = false;
  const QString raw_input = QInputDialog::getText(this,
                                                   trKey("title.goto"),
                                                   trKey("prompt.goto"),
                                                   QLineEdit::Normal,
                                                   initial,
                                                   &ok);
  if (!ok) {
    return;
  }

  const QString input = raw_input.trimmed();
  if (input.isEmpty()) {
    return;
  }

  if (input.startsWith(QLatin1Char('@'))) {
    bool parse_ok = false;
    const qulonglong value = input.mid(1).trimmed().toULongLong(&parse_ok, 10);
    if (!parse_ok) {
      QMessageBox::warning(this, trKey("title.goto"), trKey("msg.goto_invalid_offset"));
      return;
    }
    if (!view_->goToOffset(static_cast<std::uint64_t>(value), true)) {
      QMessageBox::warning(this, trKey("title.goto"), trKey("msg.goto_offset_out_of_range"));
    }
    return;
  }

  QString line_part = input;
  QString column_part = QStringLiteral("1");
  const int colon_index = input.indexOf(QLatin1Char(':'));
  const int comma_index = input.indexOf(QLatin1Char(','));
  int sep_index = -1;
  if (colon_index >= 0 && comma_index >= 0) {
    sep_index = std::min(colon_index, comma_index);
  } else if (colon_index >= 0) {
    sep_index = colon_index;
  } else if (comma_index >= 0) {
    sep_index = comma_index;
  }
  if (sep_index >= 0) {
    line_part = input.left(sep_index).trimmed();
    column_part = input.mid(sep_index + 1).trimmed();
  }

  bool line_ok = false;
  bool col_ok = false;
  const qulonglong line_value = line_part.toULongLong(&line_ok, 10);
  const qulonglong col_value = column_part.toULongLong(&col_ok, 10);
  if (!line_ok || !col_ok || line_value == 0 || col_value == 0) {
    QMessageBox::warning(this, trKey("title.goto"), trKey("msg.goto_invalid_line_column"));
    return;
  }

  const std::size_t target_line = static_cast<std::size_t>(line_value - 1);
  const std::size_t target_col = static_cast<std::size_t>(col_value - 1);
  if (!view_->goToLineColumn(target_line, target_col, true)) {
    QMessageBox::warning(this, trKey("title.goto"), trKey("msg.goto_line_out_of_range"));
  }
}

void MainWindow::toggleBookmark() {
  if (!hasCurrentTab()) {
    return;
  }

  auto& bookmarks = tabs_[currentTabIndex()].bookmarks;
  auto it = bookmarks.find(cursor_line_);
  if (it == bookmarks.end()) {
    bookmarks.insert(cursor_line_);
    statusBar()->showMessage(
        trKey("msg.bookmark_added").arg(static_cast<qulonglong>(cursor_line_ + 1)),
        2000);
  } else {
    bookmarks.erase(it);
    statusBar()->showMessage(
        trKey("msg.bookmark_removed").arg(static_cast<qulonglong>(cursor_line_ + 1)),
        2000);
  }
}

void MainWindow::nextBookmark() {
  if (!hasCurrentTab() || view_ == nullptr) {
    return;
  }

  const auto& bookmarks = tabs_[currentTabIndex()].bookmarks;
  if (bookmarks.empty()) {
    statusBar()->showMessage(trKey("msg.no_bookmarks"), 2000);
    return;
  }

  auto it = bookmarks.upper_bound(cursor_line_);
  if (it == bookmarks.end()) {
    it = bookmarks.begin();
  }
  (void)view_->goToLineColumn(*it, 0, true);
}

void MainWindow::previousBookmark() {
  if (!hasCurrentTab() || view_ == nullptr) {
    return;
  }

  const auto& bookmarks = tabs_[currentTabIndex()].bookmarks;
  if (bookmarks.empty()) {
    statusBar()->showMessage(trKey("msg.no_bookmarks"), 2000);
    return;
  }

  auto it = bookmarks.lower_bound(cursor_line_);
  if (it == bookmarks.begin()) {
    it = bookmarks.end();
  }
  --it;
  (void)view_->goToLineColumn(*it, 0, true);
}

void MainWindow::undoEdit() {
  if (session_.isReadOnly()) {
    return;
  }
  (void)session_.undo();
}

void MainWindow::redoEdit() {
  if (session_.isReadOnly()) {
    return;
  }
  (void)session_.redo();
}

void MainWindow::cutSelection() {
  if (session_.isReadOnly() || view_ == nullptr || !view_->hasSelection()) {
    return;
  }

  QClipboard* clipboard = QApplication::clipboard();
  if (clipboard == nullptr) {
    return;
  }

  const QString text = view_->selectedText();
  if (text.isEmpty()) {
    return;
  }
  clipboard->setText(text);
  (void)view_->deleteSelection();
}

void MainWindow::copySelection() {
  if (view_ == nullptr || !view_->hasSelection()) {
    return;
  }

  QClipboard* clipboard = QApplication::clipboard();
  if (clipboard == nullptr) {
    return;
  }

  const QString text = view_->selectedText();
  if (text.isEmpty()) {
    return;
  }
  clipboard->setText(text);
}

void MainWindow::pasteClipboard() {
  if (session_.isReadOnly() || view_ == nullptr) {
    return;
  }

  QClipboard* clipboard = QApplication::clipboard();
  if (clipboard == nullptr) {
    return;
  }

  const QString text = clipboard->text();
  if (text.isEmpty()) {
    return;
  }

  const std::uint64_t paste_bytes =
      static_cast<std::uint64_t>(session_.encodeTextForStorage(text).size());
  if (paste_bytes >= kLargePasteConfirmBytes) {
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        trKey("title.large_edit"),
        trKey("msg.large_paste_confirm").arg(static_cast<qulonglong>(paste_bytes)),
        QMessageBox::Ok | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (choice != QMessageBox::Ok) {
      return;
    }
  }
  view_->pasteText(text);
}

void MainWindow::findText() {
  showSearchPanel(false);
  if (find_input_ == nullptr) {
    return;
  }

  const QString query = find_input_->text().trimmed();
  if (query.isEmpty()) {
    updateSearchPanelSummary();
    return;
  }

  const core::SearchOptions options = searchOptionsFromPanel();
  last_find_query_ = query;
  last_search_options_ = options;
  startSearchRequest(query, options);
}

void MainWindow::findNext() {
  showSearchPanel(false);
  if (find_input_ == nullptr) {
    return;
  }

  const QString query = find_input_->text().trimmed();
  if (query.isEmpty()) {
    statusBar()->showMessage(trKey("msg.search_enter_text"), 2500);
    updateSearchPanelSummary();
    return;
  }

  const core::SearchOptions options = searchOptionsFromPanel();
  const bool query_changed = query != last_find_query_ ||
                             options.case_sensitive != last_search_options_.case_sensitive ||
                             options.regex != last_search_options_.regex;
  if (query_changed || search_matches_.empty()) {
    last_find_query_ = query;
    last_search_options_ = options;
    startSearchRequest(query, options);
    return;
  }

  const std::size_t next = (active_match_index_ + 1) % search_matches_.size();
  navigateToMatch(next, true);
  updateSearchPanelSummary();
}

void MainWindow::findPrevious() {
  showSearchPanel(false);
  if (find_input_ == nullptr) {
    return;
  }

  const QString query = find_input_->text().trimmed();
  if (query.isEmpty()) {
    statusBar()->showMessage(trKey("msg.search_enter_text"), 2500);
    updateSearchPanelSummary();
    return;
  }

  const core::SearchOptions options = searchOptionsFromPanel();
  const bool query_changed = query != last_find_query_ ||
                             options.case_sensitive != last_search_options_.case_sensitive ||
                             options.regex != last_search_options_.regex;
  if (query_changed || search_matches_.empty()) {
    last_find_query_ = query;
    last_search_options_ = options;
    startSearchRequest(query, options);
    return;
  }

  const std::size_t previous =
      (active_match_index_ == 0) ? (search_matches_.size() - 1) : (active_match_index_ - 1);
  navigateToMatch(previous, true);
  updateSearchPanelSummary();
}

void MainWindow::replaceText() {
  if (session_.isReadOnly()) {
    statusBar()->showMessage(trKey("msg.read_only"), 3000);
    return;
  }
  showSearchPanel(true);
  if (find_input_ == nullptr) {
    return;
  }

  const QString query = find_input_->text().trimmed();
  if (query.isEmpty()) {
    updateSearchPanelSummary();
    return;
  }

  const core::SearchOptions options = searchOptionsFromPanel();
  last_find_query_ = query;
  last_search_options_ = options;
  startSearchRequest(query, options);
}

void MainWindow::replaceNext() {
  if (session_.isReadOnly()) {
    statusBar()->showMessage(trKey("msg.read_only"), 3000);
    return;
  }
  showSearchPanel(true);
  if (find_input_ == nullptr || replace_input_ == nullptr) {
    return;
  }

  const QString query = find_input_->text().trimmed();
  if (query.isEmpty()) {
    statusBar()->showMessage(trKey("msg.replace_enter_text"), 3000);
    return;
  }

  last_find_query_ = query;
  last_replace_text_ = replace_input_->text();
  last_search_options_ = searchOptionsFromPanel();

  session_.cancelAllSearches();
  pending_search_request_id_ = 0;

  if (search_matches_.empty()) {
    search_matches_ = session_.findAllMatches(last_find_query_, last_search_options_, 100000);
    if (search_matches_.empty()) {
      clearSearchState();
      statusBar()->showMessage(trKey("msg.no_matches_replace"), 4000);
      return;
    }
    active_match_index_ = 0;
  }

  const std::size_t current_index =
      std::min<std::size_t>(active_match_index_, search_matches_.size() - 1);
  const core::SearchMatch current_match = search_matches_[current_index];
  if (current_match.length == 0) {
    statusBar()->showMessage(trKey("msg.replace_zero_length"), 4000);
    return;
  }

  if (!session_.replaceRange(current_match.offset, current_match.length, last_replace_text_)) {
    statusBar()->showMessage(trKey("msg.replace_failed"), 4000);
    return;
  }

  const std::uint64_t replacement_bytes =
      static_cast<std::uint64_t>(session_.encodeTextForStorage(last_replace_text_).size());
  std::uint64_t anchor_offset = current_match.offset;
  if (std::numeric_limits<std::uint64_t>::max() - anchor_offset < replacement_bytes) {
    anchor_offset = std::numeric_limits<std::uint64_t>::max();
  } else {
    anchor_offset += replacement_bytes;
  }

  search_matches_ = session_.findAllMatches(last_find_query_, last_search_options_, 100000);
  if (search_matches_.empty()) {
    clearSearchState();
    statusBar()->showMessage(trKey("msg.replace_done_no_more"), 5000);
    return;
  }

  if (find_next_action_ != nullptr) {
    find_next_action_->setEnabled(true);
  }
  if (find_previous_action_ != nullptr) {
    find_previous_action_->setEnabled(true);
  }
  if (replace_next_action_ != nullptr) {
    replace_next_action_->setEnabled(!session_.isReadOnly());
  }
  if (replace_all_action_ != nullptr) {
    replace_all_action_->setEnabled(!session_.isReadOnly());
  }

  const auto it = std::lower_bound(search_matches_.begin(),
                                   search_matches_.end(),
                                   anchor_offset,
                                   [](const core::SearchMatch& match, std::uint64_t offset) {
                                     return match.offset < offset;
                                   });
  const std::size_t next_index =
      (it == search_matches_.end()) ? 0 : static_cast<std::size_t>(std::distance(search_matches_.begin(), it));
  navigateToMatch(next_index, true);
  statusBar()->showMessage(trKey("msg.replace_done_one"), 3000);
  updateSearchPanelSummary();
}

void MainWindow::replaceAll() {
  if (session_.isReadOnly()) {
    statusBar()->showMessage(trKey("msg.read_only"), 3000);
    return;
  }
  showSearchPanel(true);
  if (find_input_ == nullptr || replace_input_ == nullptr) {
    return;
  }

  const QString query = find_input_->text().trimmed();
  if (query.isEmpty()) {
    statusBar()->showMessage(trKey("msg.replace_enter_text"), 3000);
    return;
  }

  last_find_query_ = query;
  last_replace_text_ = replace_input_->text();
  last_search_options_ = searchOptionsFromPanel();

  ReplaceScopeChoice scope = ReplaceScopeChoice::kCurrentFile;
  if (replace_scope_combo_ != nullptr) {
    scope = static_cast<ReplaceScopeChoice>(replace_scope_combo_->currentData().toInt());
  }

  const auto collectScopedMatches = [this](std::uint64_t range_start, std::uint64_t range_end) {
    const std::vector<core::SearchMatch> all =
        session_.findAllMatches(last_find_query_, last_search_options_, 100000);
    return nonOverlappingMatchesInRange(all, range_start, range_end, 100000);
  };

  session_.cancelAllSearches();
  pending_search_request_id_ = 0;

  if (scope == ReplaceScopeChoice::kAllOpenTabs) {
    if (!saveCurrentTabState()) {
      return;
    }
    const int original_tab = currentTabIndex();
    std::size_t total_replaced = 0;
    std::size_t touched_tabs = 0;
    bool canceled = false;

    QProgressDialog progress(
        trKey("msg.replace_progress_tabs")
            .arg(static_cast<qulonglong>(1))
            .arg(static_cast<qulonglong>(tabs_.size())),
        trKey("action.cancel"),
        0,
        static_cast<int>(tabs_.size()),
        this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);

    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
      progress.setLabelText(
          trKey("msg.replace_progress_tabs")
              .arg(static_cast<qulonglong>(i + 1))
              .arg(static_cast<qulonglong>(tabs_.size())));
      progress.setValue(i);
      QApplication::processEvents();
      if (progress.wasCanceled()) {
        canceled = true;
        break;
      }
      if (!switchToTab(i)) {
        continue;
      }
      if (session_.isReadOnly()) {
        continue;
      }

      const std::vector<core::SearchMatch> matches =
          collectScopedMatches(0, std::numeric_limits<std::uint64_t>::max());
      if (matches.empty()) {
        continue;
      }

      std::size_t replaced_here = 0;
      std::size_t processed = 0;
      session_.beginTransaction(QStringLiteral("Replace All"));
      for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
        if (progress.wasCanceled()) {
          canceled = true;
          break;
        }
        if (session_.replaceRange(it->offset,
                                  static_cast<std::uint64_t>(it->length),
                                  last_replace_text_)) {
          ++replaced_here;
        }
        ++processed;
        if (processed % 64 == 0) {
          QApplication::processEvents();
        }
      }
      session_.endTransaction();
      total_replaced += replaced_here;
      if (replaced_here > 0) {
        ++touched_tabs;
      }
      if (canceled) {
        break;
      }
    }

    progress.setValue(static_cast<int>(tabs_.size()));
    if (original_tab >= 0 && original_tab < static_cast<int>(tabs_.size())) {
      (void)switchToTab(original_tab);
    }
    clearSearchState();
    if (total_replaced == 0) {
      statusBar()->showMessage(trKey("msg.no_matches_replace"), 4000);
      return;
    }
    if (canceled) {
      statusBar()->showMessage(
          trKey("msg.replace_tabs_canceled")
              .arg(static_cast<qulonglong>(total_replaced))
              .arg(static_cast<qulonglong>(touched_tabs)),
          6000);
      return;
    }
    statusBar()->showMessage(
        trKey("msg.replace_tabs_done")
            .arg(static_cast<qulonglong>(total_replaced))
            .arg(static_cast<qulonglong>(touched_tabs)),
        6000);
    return;
  }

  std::uint64_t range_start = 0;
  std::uint64_t range_end = std::numeric_limits<std::uint64_t>::max();
  if (scope == ReplaceScopeChoice::kSelection) {
    if (view_ == nullptr || !view_->selectedByteRange(&range_start, &range_end) || range_end <= range_start) {
      statusBar()->showMessage(trKey("msg.replace_scope_selection_required"), 4000);
      return;
    }
  }

  const std::vector<core::SearchMatch> preview_matches = collectScopedMatches(range_start, range_end);
  if (preview_matches.empty()) {
    clearSearchState();
    statusBar()->showMessage(trKey("msg.no_matches_replace"), 3000);
    return;
  }

  const std::uint64_t replacement_bytes =
      static_cast<std::uint64_t>(session_.encodeTextForStorage(last_replace_text_).size());
  std::uint64_t estimated_touch_bytes = 0;
  for (const core::SearchMatch& match : preview_matches) {
    const std::uint64_t chunk = static_cast<std::uint64_t>(match.length) + replacement_bytes;
    if (estimated_touch_bytes > std::numeric_limits<std::uint64_t>::max() - chunk) {
      estimated_touch_bytes = std::numeric_limits<std::uint64_t>::max();
      break;
    }
    estimated_touch_bytes += chunk;
    if (estimated_touch_bytes >= kLargeReplaceConfirmBytes) {
      break;
    }
  }
  if (estimated_touch_bytes >= kLargeReplaceConfirmBytes) {
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        trKey("title.large_edit"),
        trKey("msg.large_replace_confirm").arg(static_cast<qulonglong>(estimated_touch_bytes)),
        QMessageBox::Ok | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (choice != QMessageBox::Ok) {
      return;
    }
  }

  QStringList preview_lines;
  const std::size_t preview_count = std::min<std::size_t>(preview_matches.size(), 20);
  for (std::size_t i = 0; i < preview_count; ++i) {
    const core::SearchMatch& match = preview_matches[i];
    std::size_t line = 0;
    std::size_t col = 0;
    (void)session_.lineColumnForOffset(match.offset, &line, &col);

    const std::uint64_t start = (match.offset > 20) ? (match.offset - 20) : 0;
    const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(
        session_.byteSize() - start,
        static_cast<std::uint64_t>(std::max<std::size_t>(match.length + 40, 40))));
    const std::string snippet_bytes = session_.bytesAt(start, take);
    QString snippet = session_.decodeBytesFromStorage(snippet_bytes);
    snippet.replace(QLatin1Char('\r'), QLatin1Char(' '));
    snippet.replace(QLatin1Char('\n'), QLatin1Char(' '));

    preview_lines.push_back(
        QStringLiteral("%1:%2  %3")
            .arg(static_cast<qulonglong>(line + 1))
            .arg(static_cast<qulonglong>(col + 1))
            .arg(snippet));
  }

  QMessageBox confirm(this);
  confirm.setWindowTitle(trKey("title.replace_preview"));
  confirm.setText(trKey("msg.replace_preview_confirm").arg(static_cast<qulonglong>(preview_matches.size())));
  confirm.setInformativeText(trKey("msg.replace_preview_hint").arg(static_cast<qulonglong>(preview_count)));
  confirm.setDetailedText(preview_lines.join(QLatin1Char('\n')));
  confirm.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
  confirm.setDefaultButton(QMessageBox::Ok);
  if (confirm.exec() != QMessageBox::Ok) {
    statusBar()->showMessage(trKey("msg.replace_all_canceled"), 2000);
    return;
  }

  QProgressDialog progress(
      trKey("msg.replace_progress_current")
          .arg(static_cast<qulonglong>(0))
          .arg(static_cast<qulonglong>(preview_matches.size())),
      trKey("action.cancel"),
      0,
      static_cast<int>(preview_matches.size()),
      this);
  progress.setWindowModality(Qt::ApplicationModal);
  progress.setMinimumDuration(0);

  std::size_t replaced = 0;
  std::size_t processed = 0;
  bool canceled = false;
  session_.beginTransaction(QStringLiteral("Replace All"));
  for (auto it = preview_matches.rbegin(); it != preview_matches.rend(); ++it) {
    if (progress.wasCanceled()) {
      canceled = true;
      break;
    }
    if (session_.replaceRange(it->offset,
                              static_cast<std::uint64_t>(it->length),
                              last_replace_text_)) {
      ++replaced;
    }
    ++processed;
    if (processed % 32 == 0 || processed == preview_matches.size()) {
      progress.setValue(static_cast<int>(processed));
      progress.setLabelText(
          trKey("msg.replace_progress_current")
              .arg(static_cast<qulonglong>(processed))
              .arg(static_cast<qulonglong>(preview_matches.size())));
      QApplication::processEvents();
    }
  }
  session_.endTransaction();
  progress.setValue(static_cast<int>(preview_matches.size()));

  if (canceled) {
    statusBar()->showMessage(
        trKey("msg.replace_all_canceled_with_count").arg(static_cast<qulonglong>(replaced)),
        5000);
  }

  std::vector<core::SearchMatch> remaining_matches =
      session_.findAllMatches(last_find_query_, last_search_options_, 100000);
  remaining_matches = nonOverlappingMatchesInRange(remaining_matches, range_start, range_end, 100000);
  search_matches_ = std::move(remaining_matches);
  if (search_matches_.empty()) {
    clearSearchState();
    if (!canceled) {
      statusBar()->showMessage(
          trKey("msg.replace_all_done").arg(static_cast<qulonglong>(replaced)),
          5000);
    }
    return;
  }

  if (find_next_action_ != nullptr) {
    find_next_action_->setEnabled(true);
  }
  if (find_previous_action_ != nullptr) {
    find_previous_action_->setEnabled(true);
  }
  if (replace_next_action_ != nullptr) {
    replace_next_action_->setEnabled(!session_.isReadOnly());
  }
  if (replace_all_action_ != nullptr) {
    replace_all_action_->setEnabled(!session_.isReadOnly());
  }
  navigateToMatch(0, true);
  if (!canceled) {
    statusBar()->showMessage(
        trKey("msg.replace_all_done_remain")
            .arg(static_cast<qulonglong>(replaced))
            .arg(static_cast<qulonglong>(search_matches_.size())),
        6000);
  }
  updateSearchPanelSummary();
}

void MainWindow::handleSearchCompleted(qulonglong request_id, qulonglong match_count) {
  std::vector<core::SearchMatch> matches = session_.takeSearchResults(request_id);
  Q_UNUSED(match_count);
  if (request_id != pending_search_request_id_) {
    return;
  }
  pending_search_request_id_ = 0;

  std::sort(matches.begin(), matches.end(), [](const core::SearchMatch& lhs, const core::SearchMatch& rhs) {
    if (lhs.offset != rhs.offset) {
      return lhs.offset < rhs.offset;
    }
    return lhs.length < rhs.length;
  });

  if (matches.empty()) {
    clearSearchState();
    statusBar()->showMessage(trKey("msg.search_no_match").arg(request_id), 5000);
    updateSearchPanelSummary();
    return;
  }

  search_matches_ = matches;
  active_match_index_ = 0;
  if (find_next_action_ != nullptr) {
    find_next_action_->setEnabled(true);
  }
  if (find_previous_action_ != nullptr) {
    find_previous_action_->setEnabled(true);
  }
  if (replace_next_action_ != nullptr) {
    replace_next_action_->setEnabled(!session_.isReadOnly());
  }
  if (replace_all_action_ != nullptr) {
    replace_all_action_->setEnabled(!session_.isReadOnly());
  }

  navigateToMatch(0, true);
  updateSearchPanelSummary();
}

void MainWindow::clearSearchState() {
  search_matches_.clear();
  active_match_index_ = 0;
  pending_search_request_id_ = 0;
  if (find_next_action_ != nullptr) {
    find_next_action_->setEnabled(false);
  }
  if (find_previous_action_ != nullptr) {
    find_previous_action_->setEnabled(false);
  }
  if (replace_next_action_ != nullptr) {
    replace_next_action_->setEnabled(false);
  }
  if (replace_all_action_ != nullptr) {
    replace_all_action_->setEnabled(false);
  }
  if (view_ != nullptr) {
    view_->clearActiveMatch();
  }
  updateSearchPanelSummary();
}

void MainWindow::startSearchRequest(const QString& query, const core::SearchOptions& options) {
  session_.cancelAllSearches();
  clearSearchState();

  const std::uint64_t request_id = session_.startSearch(query, options, 100000);
  if (request_id == 0) {
    statusBar()->showMessage(trKey("msg.search_not_started"), 3000);
    return;
  }
  pending_search_request_id_ = request_id;
  updateStatusBar();
}

void MainWindow::navigateToMatch(std::size_t match_index, bool center_view) {
  if (match_index >= search_matches_.size() || view_ == nullptr) {
    return;
  }

  const core::SearchMatch& match = search_matches_[match_index];
  std::size_t start_line = 0;
  std::size_t start_col = 0;
  if (!session_.lineColumnForOffset(match.offset, &start_line, &start_col)) {
    return;
  }

  const std::uint64_t size = session_.byteSize();
  const std::uint64_t match_length = static_cast<std::uint64_t>(match.length);
  const std::uint64_t end_offset =
      (match.offset >= size || match_length >= size - match.offset) ? size : (match.offset + match_length);
  std::size_t end_line = 0;
  std::size_t end_col = 0;
  if (!session_.lineColumnForOffset(end_offset, &end_line, &end_col)) {
    end_line = start_line;
    end_col = start_col + 1;
  }

  std::size_t highlight_columns = 1;
  if (end_line == start_line && end_col > start_col) {
    highlight_columns = end_col - start_col;
  }

  active_match_index_ = match_index;
  view_->scrollToLine(start_line, center_view);
  view_->setActiveMatch(start_line, start_col, highlight_columns);
  updateStatusBar();
}

void MainWindow::refreshWindowState() {
  if (hasCurrentTab()) {
    TabState& tab = tabs_[currentTabIndex()];
    tab.file_path = session_.filePath();
    tab.snapshot_dirty = session_.isDirty();
    tab.encoding = session_.textEncoding();
    tab.line_ending = session_.lineEnding();
    tab.read_only = session_.isReadOnly();
  }

  const bool read_only = session_.isReadOnly();
  if (read_only_action_ != nullptr) {
    QSignalBlocker blocker(*read_only_action_);
    read_only_action_->setChecked(read_only);
  }
  if (cut_action_ != nullptr) {
    cut_action_->setEnabled(!read_only && view_ != nullptr && view_->hasSelection());
  }
  if (paste_action_ != nullptr) {
    QClipboard* clipboard = QApplication::clipboard();
    const bool has_text = clipboard != nullptr && !clipboard->text().isEmpty();
    paste_action_->setEnabled(!read_only && has_text);
  }
  if (replace_action_ != nullptr) {
    replace_action_->setEnabled(!read_only);
  }
  if (replace_next_action_ != nullptr) {
    replace_next_action_->setEnabled(!read_only && !search_matches_.empty());
  }
  if (replace_all_action_ != nullptr) {
    replace_all_action_->setEnabled(!read_only && !search_matches_.empty());
  }
  if (replace_input_ != nullptr) {
    replace_input_->setEnabled(!read_only);
  }
  if (replace_scope_combo_ != nullptr) {
    replace_scope_combo_->setEnabled(!read_only);
  }
  if (replace_next_button_ != nullptr) {
    replace_next_button_->setEnabled(!read_only);
  }
  if (replace_all_button_ != nullptr) {
    replace_all_button_->setEnabled(!read_only);
  }

  updateStatusBar();
  syncFormatActions();
  updateSearchPanelSummary();
  updateAllTabTitles();
  updatePerformancePanel();

  const QString title = currentTabTitle();
  setWindowTitle(trKey("app.window_title").arg(title));
}

void MainWindow::updateStatusBar() {
  const QString index_flag = session_.isLineIndexComplete() ? trKey("status.indexed")
                                                             : trKey("status.indexing");
  QString text = trKey("status.tab_line")
                     .arg(static_cast<qulonglong>(hasCurrentTab() ? currentTabIndex() + 1 : 0))
                     .arg(static_cast<qulonglong>(tabs_.size()))
                     .arg(static_cast<qulonglong>(cursor_line_ + 1))
                     .arg(static_cast<qulonglong>(cursor_column_ + 1))
                     .arg(session_.lineCount())
                     .arg(static_cast<qulonglong>(session_.indexedLineCount()))
                     .arg(static_cast<qulonglong>(session_.byteSize()))
                     .arg(index_flag)
                     .arg(session_.textEncodingName())
                     .arg(session_.lineEndingName());

  if (pending_search_request_id_ != 0) {
    text += trKey("status.searching").arg(pending_search_request_id_);
  } else if (!search_matches_.empty()) {
    const std::size_t clamped = std::min<std::size_t>(active_match_index_, search_matches_.size() - 1);
    text += trKey("status.match")
                .arg(static_cast<qulonglong>(clamped + 1))
                .arg(static_cast<qulonglong>(search_matches_.size()));
  }

  if (hasCurrentTab()) {
    text += trKey("status.bookmarks")
                .arg(static_cast<qulonglong>(tabs_[currentTabIndex()].bookmarks.size()));
  }
  if (session_.isReadOnly()) {
    text += trKey("status.read_only");
  }

  statusBar()->showMessage(text);
}

void MainWindow::buildMenus() {
  menuBar()->clear();

  file_menu_ = menuBar()->addMenu(trKey("menu.file"));

  new_tab_action_ = file_menu_->addAction(trKey("action.new_tab"));
  new_tab_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
  connect(new_tab_action_, &QAction::triggered, this, &MainWindow::newTab);

  open_action_ = file_menu_->addAction(trKey("action.open"));
  open_action_->setShortcut(QKeySequence::Open);
  connect(open_action_, &QAction::triggered, this, &MainWindow::openFile);

  open_recent_menu_ = file_menu_->addMenu(trKey("menu.open_recent"));

  close_tab_action_ = file_menu_->addAction(trKey("action.close_tab"));
  close_tab_action_->setShortcut(QKeySequence::Close);
  connect(close_tab_action_, &QAction::triggered, this, &MainWindow::closeCurrentTab);

  save_action_ = file_menu_->addAction(trKey("action.save"));
  save_action_->setShortcut(QKeySequence::Save);
  connect(save_action_, &QAction::triggered, this, &MainWindow::saveFile);

  save_as_action_ = file_menu_->addAction(trKey("action.save_as"));
  save_as_action_->setShortcut(QKeySequence::SaveAs);
  connect(save_as_action_, &QAction::triggered, this, &MainWindow::saveAs);

  file_menu_->addSeparator();
  quit_action_ = file_menu_->addAction(trKey("action.quit"));
  quit_action_->setShortcut(QKeySequence::Quit);
  connect(quit_action_, &QAction::triggered, this, &QWidget::close);

  edit_menu_ = menuBar()->addMenu(trKey("menu.edit"));
  undo_action_ = edit_menu_->addAction(trKey("action.undo"));
  undo_action_->setShortcut(QKeySequence::Undo);
  undo_action_->setEnabled(false);
  connect(undo_action_, &QAction::triggered, this, &MainWindow::undoEdit);

  redo_action_ = edit_menu_->addAction(trKey("action.redo"));
  redo_action_->setShortcut(QKeySequence::Redo);
  redo_action_->setEnabled(false);
  connect(redo_action_, &QAction::triggered, this, &MainWindow::redoEdit);

  edit_menu_->addSeparator();

  cut_action_ = edit_menu_->addAction(trKey("action.cut"));
  cut_action_->setShortcut(QKeySequence::Cut);
  connect(cut_action_, &QAction::triggered, this, &MainWindow::cutSelection);

  copy_action_ = edit_menu_->addAction(trKey("action.copy"));
  copy_action_->setShortcut(QKeySequence::Copy);
  connect(copy_action_, &QAction::triggered, this, &MainWindow::copySelection);

  paste_action_ = edit_menu_->addAction(trKey("action.paste"));
  paste_action_->setShortcut(QKeySequence::Paste);
  connect(paste_action_, &QAction::triggered, this, &MainWindow::pasteClipboard);

  edit_menu_->addSeparator();
  read_only_action_ = edit_menu_->addAction(trKey("action.read_only_mode"));
  read_only_action_->setCheckable(true);
  read_only_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+R")));
  connect(read_only_action_, &QAction::triggered, this, &MainWindow::toggleReadOnlyMode);

  format_menu_ = menuBar()->addMenu(trKey("menu.format"));
  encoding_menu_ = format_menu_->addMenu(trKey("menu.encoding"));
  auto* encoding_group = new QActionGroup(this);
  encoding_group->setExclusive(true);

  encoding_utf8_action_ = encoding_menu_->addAction(QStringLiteral("UTF-8"));
  encoding_utf8_action_->setCheckable(true);
  encoding_group->addAction(encoding_utf8_action_);
  connect(encoding_utf8_action_, &QAction::triggered, this, [this]() {
    session_.setTextEncoding(core::DocumentSession::TextEncoding::kUtf8);
  });

  encoding_gbk_action_ = encoding_menu_->addAction(QStringLiteral("GBK"));
  encoding_gbk_action_->setCheckable(true);
  encoding_group->addAction(encoding_gbk_action_);
  connect(encoding_gbk_action_, &QAction::triggered, this, [this]() {
    session_.setTextEncoding(core::DocumentSession::TextEncoding::kGbk);
  });

  line_ending_menu_ = format_menu_->addMenu(trKey("menu.line_endings"));
  auto* line_ending_group = new QActionGroup(this);
  line_ending_group->setExclusive(true);

  line_ending_lf_action_ = line_ending_menu_->addAction(QStringLiteral("LF"));
  line_ending_lf_action_->setCheckable(true);
  line_ending_group->addAction(line_ending_lf_action_);
  connect(line_ending_lf_action_, &QAction::triggered, this, [this]() {
    session_.setLineEnding(core::DocumentSession::LineEnding::kLf);
  });

  line_ending_crlf_action_ = line_ending_menu_->addAction(QStringLiteral("CRLF"));
  line_ending_crlf_action_->setCheckable(true);
  line_ending_group->addAction(line_ending_crlf_action_);
  connect(line_ending_crlf_action_, &QAction::triggered, this, [this]() {
    session_.setLineEnding(core::DocumentSession::LineEnding::kCrlf);
  });

  tab_width_menu_ = format_menu_->addMenu(trKey("menu.tab_width"));
  auto* tab_width_group = new QActionGroup(this);
  tab_width_group->setExclusive(true);
  auto applyTabWidth = [this](int spaces) {
    tab_width_spaces_ = normalizeTabWidthValue(spaces);
    if (view_ != nullptr) {
      view_->setTabWidth(tab_width_spaces_);
    }
    QSettings settings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
    settings.setValue(QString::fromLatin1(kUiTabWidthKey), tab_width_spaces_);
    syncFormatActions();
  };

  tab_width_2_action_ = tab_width_menu_->addAction(trKey("label.tab_width_2"));
  tab_width_2_action_->setCheckable(true);
  tab_width_group->addAction(tab_width_2_action_);
  connect(tab_width_2_action_, &QAction::triggered, this, [applyTabWidth]() { applyTabWidth(2); });

  tab_width_4_action_ = tab_width_menu_->addAction(trKey("label.tab_width_4"));
  tab_width_4_action_->setCheckable(true);
  tab_width_group->addAction(tab_width_4_action_);
  connect(tab_width_4_action_, &QAction::triggered, this, [applyTabWidth]() { applyTabWidth(4); });

  tab_width_8_action_ = tab_width_menu_->addAction(trKey("label.tab_width_8"));
  tab_width_8_action_->setCheckable(true);
  tab_width_group->addAction(tab_width_8_action_);
  connect(tab_width_8_action_, &QAction::triggered, this, [applyTabWidth]() { applyTabWidth(8); });

  search_menu_ = menuBar()->addMenu(trKey("menu.search"));
  go_to_action_ = search_menu_->addAction(trKey("action.go_to"));
  go_to_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
  connect(go_to_action_, &QAction::triggered, this, &MainWindow::goToLocation);

  search_menu_->addSeparator();

  find_action_ = search_menu_->addAction(trKey("action.find"));
  find_action_->setShortcut(QKeySequence::Find);
  connect(find_action_, &QAction::triggered, this, &MainWindow::findText);

  find_in_files_action_ = search_menu_->addAction(trKey("action.find_in_files"));
  find_in_files_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
  connect(find_in_files_action_, &QAction::triggered, this, &MainWindow::findInFiles);

  find_next_action_ = search_menu_->addAction(trKey("action.find_next"));
  find_next_action_->setShortcut(QKeySequence(Qt::Key_F3));
  find_next_action_->setEnabled(false);
  connect(find_next_action_, &QAction::triggered, this, &MainWindow::findNext);

  find_previous_action_ = search_menu_->addAction(trKey("action.find_previous"));
  find_previous_action_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
  find_previous_action_->setEnabled(false);
  connect(find_previous_action_, &QAction::triggered, this, &MainWindow::findPrevious);

  search_menu_->addSeparator();

  replace_action_ = search_menu_->addAction(trKey("action.replace"));
  replace_action_->setShortcut(QKeySequence::Replace);
  connect(replace_action_, &QAction::triggered, this, &MainWindow::replaceText);

  replace_next_action_ = search_menu_->addAction(trKey("action.replace_next"));
  replace_next_action_->setEnabled(false);
  connect(replace_next_action_, &QAction::triggered, this, &MainWindow::replaceNext);

  replace_all_action_ = search_menu_->addAction(trKey("action.replace_all"));
  replace_all_action_->setEnabled(false);
  connect(replace_all_action_, &QAction::triggered, this, &MainWindow::replaceAll);

  search_menu_->addSeparator();
  toggle_bookmark_action_ = search_menu_->addAction(trKey("action.toggle_bookmark"));
  toggle_bookmark_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+F2")));
  connect(toggle_bookmark_action_, &QAction::triggered, this, &MainWindow::toggleBookmark);

  next_bookmark_action_ = search_menu_->addAction(trKey("action.next_bookmark"));
  next_bookmark_action_->setShortcut(QKeySequence(Qt::Key_F2));
  connect(next_bookmark_action_, &QAction::triggered, this, &MainWindow::nextBookmark);

  previous_bookmark_action_ = search_menu_->addAction(trKey("action.previous_bookmark"));
  previous_bookmark_action_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F2));
  connect(previous_bookmark_action_, &QAction::triggered, this, &MainWindow::previousBookmark);

  view_menu_ = menuBar()->addMenu(trKey("menu.view"));
  if (perf_dock_ != nullptr) {
    perf_panel_action_ = perf_dock_->toggleViewAction();
    perf_panel_action_->setText(trKey("action.performance_panel"));
    view_menu_->addAction(perf_panel_action_);
  } else {
    perf_panel_action_ = nullptr;
  }

  appearance_menu_ = view_menu_->addMenu(trKey("menu.appearance"));
  auto* appearance_group = new QActionGroup(this);
  appearance_group->setExclusive(true);

  appearance_follow_system_action_ = appearance_menu_->addAction(trKey("action.appearance_follow_system"));
  appearance_follow_system_action_->setCheckable(true);
  appearance_follow_system_action_->setChecked(appearance_mode_ == LargeFileView::AppearanceMode::kFollowSystem);
  appearance_group->addAction(appearance_follow_system_action_);
  connect(appearance_follow_system_action_, &QAction::triggered, this, &MainWindow::setAppearanceFollowSystem);

  appearance_light_action_ = appearance_menu_->addAction(trKey("action.appearance_light"));
  appearance_light_action_->setCheckable(true);
  appearance_light_action_->setChecked(appearance_mode_ == LargeFileView::AppearanceMode::kLight);
  appearance_group->addAction(appearance_light_action_);
  connect(appearance_light_action_, &QAction::triggered, this, &MainWindow::setAppearanceLight);

  appearance_dark_action_ = appearance_menu_->addAction(trKey("action.appearance_dark"));
  appearance_dark_action_->setCheckable(true);
  appearance_dark_action_->setChecked(appearance_mode_ == LargeFileView::AppearanceMode::kDark);
  appearance_group->addAction(appearance_dark_action_);
  connect(appearance_dark_action_, &QAction::triggered, this, &MainWindow::setAppearanceDark);

  restore_session_on_startup_action_ = view_menu_->addAction(trKey("action.restore_session_on_startup"));
  restore_session_on_startup_action_->setCheckable(true);
  restore_session_on_startup_action_->setChecked(restore_session_on_startup_);
  connect(restore_session_on_startup_action_, &QAction::triggered, this, [this](bool checked) {
    restore_session_on_startup_ = checked;
    QSettings settings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
    settings.setValue(QString::fromLatin1(kStartupRestoreSessionKey), restore_session_on_startup_);
  });

  view_menu_->addSeparator();
  command_palette_action_ = view_menu_->addAction(trKey("action.command_palette"));
  command_palette_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
  connect(command_palette_action_, &QAction::triggered, this, &MainWindow::showCommandPalette);

  shortcuts_action_ = view_menu_->addAction(trKey("action.customize_shortcuts"));
  connect(shortcuts_action_, &QAction::triggered, this, &MainWindow::customizeShortcuts);

  help_menu_ = menuBar()->addMenu(trKey("menu.help"));
  about_action_ = help_menu_->addAction(trKey("action.about"));
  about_action_->setMenuRole(QAction::AboutRole);
  connect(about_action_, &QAction::triggered, this, &MainWindow::showAboutDialog);

  language_menu_ = menuBar()->addMenu(trKey("menu.language"));
  auto* language_group = new QActionGroup(this);
  language_group->setExclusive(true);
  for (const auto& item : i18n::supportedLanguages()) {
    QAction* action = language_menu_->addAction(item.display_name);
    action->setCheckable(true);
    action->setChecked(item.language == i18n::currentLanguage());
    language_group->addAction(action);
    connect(action, &QAction::triggered, this, [this, item]() {
      applyLanguage(item.language, true);
    });
  }

  rebuildRecentFilesMenu();
  applyShortcutOverrides();
}

void MainWindow::buildSearchPanel() {
  if (search_panel_ == nullptr) {
    return;
  }

  auto* layout = new QHBoxLayout(search_panel_);
  layout->setContentsMargins(8, 6, 8, 6);
  layout->setSpacing(8);

  find_label_ = new QLabel(trKey("label.find"), search_panel_);
  layout->addWidget(find_label_);

  find_input_ = new QLineEdit(search_panel_);
  find_input_->setPlaceholderText(trKey("placeholder.search_text"));
  layout->addWidget(find_input_, 2);

  replace_label_ = new QLabel(trKey("label.replace"), search_panel_);
  layout->addWidget(replace_label_);

  replace_input_ = new QLineEdit(search_panel_);
  replace_input_->setPlaceholderText(trKey("placeholder.replacement"));
  layout->addWidget(replace_input_, 2);

  replace_scope_label_ = new QLabel(trKey("label.scope"), search_panel_);
  layout->addWidget(replace_scope_label_);

  replace_scope_combo_ = new QComboBox(search_panel_);
  replace_scope_combo_->addItem(trKey("label.scope_selection"),
                                static_cast<int>(ReplaceScopeChoice::kSelection));
  replace_scope_combo_->addItem(trKey("label.scope_current_file"),
                                static_cast<int>(ReplaceScopeChoice::kCurrentFile));
  replace_scope_combo_->addItem(trKey("label.scope_all_tabs"),
                                static_cast<int>(ReplaceScopeChoice::kAllOpenTabs));
  replace_scope_combo_->setCurrentIndex(1);
  layout->addWidget(replace_scope_combo_);

  case_sensitive_checkbox_ = new QCheckBox(trKey("label.case"), search_panel_);
  layout->addWidget(case_sensitive_checkbox_);

  regex_checkbox_ = new QCheckBox(trKey("label.regex"), search_panel_);
  layout->addWidget(regex_checkbox_);

  find_prev_button_ = new QPushButton(trKey("label.prev"), search_panel_);
  connect(find_prev_button_, &QPushButton::clicked, this, &MainWindow::findPrevious);
  layout->addWidget(find_prev_button_);

  find_next_button_ = new QPushButton(trKey("label.next"), search_panel_);
  connect(find_next_button_, &QPushButton::clicked, this, &MainWindow::findNext);
  layout->addWidget(find_next_button_);

  replace_next_button_ = new QPushButton(trKey("label.replace_btn"), search_panel_);
  connect(replace_next_button_, &QPushButton::clicked, this, &MainWindow::replaceNext);
  layout->addWidget(replace_next_button_);

  replace_all_button_ = new QPushButton(trKey("label.replace_all_btn"), search_panel_);
  connect(replace_all_button_, &QPushButton::clicked, this, &MainWindow::replaceAll);
  layout->addWidget(replace_all_button_);

  match_count_label_ = new QLabel(trKey("label.matches_zero"), search_panel_);
  layout->addWidget(match_count_label_);
  layout->addStretch(1);

  connect(find_input_, &QLineEdit::returnPressed, this, &MainWindow::findNext);
  connect(replace_input_, &QLineEdit::returnPressed, this, &MainWindow::replaceNext);

  auto invalidate_search = [this]() {
    session_.cancelAllSearches();
    clearSearchState();
    updateSearchPanelSummary();
  };
  connect(find_input_, &QLineEdit::textChanged, this, invalidate_search);
  connect(case_sensitive_checkbox_, &QCheckBox::toggled, this, invalidate_search);
  connect(regex_checkbox_, &QCheckBox::toggled, this, invalidate_search);

  updateSearchPanelSummary();
}

void MainWindow::retranslateSearchPanel() {
  if (find_label_ != nullptr) {
    find_label_->setText(trKey("label.find"));
  }
  if (replace_label_ != nullptr) {
    replace_label_->setText(trKey("label.replace"));
  }
  if (replace_scope_label_ != nullptr) {
    replace_scope_label_->setText(trKey("label.scope"));
  }
  if (find_input_ != nullptr) {
    find_input_->setPlaceholderText(trKey("placeholder.search_text"));
  }
  if (replace_input_ != nullptr) {
    replace_input_->setPlaceholderText(trKey("placeholder.replacement"));
  }
  if (replace_scope_combo_ != nullptr) {
    const int previous = replace_scope_combo_->currentIndex();
    replace_scope_combo_->clear();
    replace_scope_combo_->addItem(trKey("label.scope_selection"),
                                  static_cast<int>(ReplaceScopeChoice::kSelection));
    replace_scope_combo_->addItem(trKey("label.scope_current_file"),
                                  static_cast<int>(ReplaceScopeChoice::kCurrentFile));
    replace_scope_combo_->addItem(trKey("label.scope_all_tabs"),
                                  static_cast<int>(ReplaceScopeChoice::kAllOpenTabs));
    replace_scope_combo_->setCurrentIndex(std::clamp(previous, 0, 2));
  }
  if (case_sensitive_checkbox_ != nullptr) {
    case_sensitive_checkbox_->setText(trKey("label.case"));
  }
  if (regex_checkbox_ != nullptr) {
    regex_checkbox_->setText(trKey("label.regex"));
  }
  if (find_prev_button_ != nullptr) {
    find_prev_button_->setText(trKey("label.prev"));
  }
  if (find_next_button_ != nullptr) {
    find_next_button_->setText(trKey("label.next"));
  }
  if (replace_next_button_ != nullptr) {
    replace_next_button_->setText(trKey("label.replace_btn"));
  }
  if (replace_all_button_ != nullptr) {
    replace_all_button_->setText(trKey("label.replace_all_btn"));
  }
  updateSearchPanelSummary();
}

void MainWindow::applyLanguage(i18n::Language language, bool persist) {
  i18n::setCurrentLanguage(language);
  if (persist) {
    QSettings settings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
    settings.setValue(QString::fromLatin1(kUiLanguageKey), i18n::languageCode(language));
  }

  if (perf_dock_ != nullptr) {
    perf_dock_->setWindowTitle(trKey("title.performance"));
  }
  if (find_results_dock_ != nullptr) {
    find_results_dock_->setWindowTitle(trKey("title.find_results"));
  }
  if (find_results_tree_ != nullptr) {
    find_results_tree_->setHeaderLabels(
        QStringList{trKey("label.find_results_location"), trKey("label.find_results_preview")});
  }

  buildMenus();
  retranslateSearchPanel();
  refreshWindowState();
  if (view_ != nullptr) {
    view_->viewport()->update();
  }
}

void MainWindow::applyAppearance(LargeFileView::AppearanceMode mode, bool persist) {
  appearance_mode_ = mode;
  if (view_ != nullptr) {
    view_->setAppearanceMode(mode);
  }
  if (persist) {
    QSettings settings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
    settings.setValue(QString::fromLatin1(kUiAppearanceKey), appearanceCode(mode));
  }

  if (appearance_follow_system_action_ != nullptr) {
    QSignalBlocker blocker(*appearance_follow_system_action_);
    appearance_follow_system_action_->setChecked(mode == LargeFileView::AppearanceMode::kFollowSystem);
  }
  if (appearance_light_action_ != nullptr) {
    QSignalBlocker blocker(*appearance_light_action_);
    appearance_light_action_->setChecked(mode == LargeFileView::AppearanceMode::kLight);
  }
  if (appearance_dark_action_ != nullptr) {
    QSignalBlocker blocker(*appearance_dark_action_);
    appearance_dark_action_->setChecked(mode == LargeFileView::AppearanceMode::kDark);
  }
}

void MainWindow::setAppearanceFollowSystem() {
  applyAppearance(LargeFileView::AppearanceMode::kFollowSystem, true);
}

void MainWindow::setAppearanceLight() {
  applyAppearance(LargeFileView::AppearanceMode::kLight, true);
}

void MainWindow::setAppearanceDark() {
  applyAppearance(LargeFileView::AppearanceMode::kDark, true);
}

std::vector<std::pair<QString, QAction*>> MainWindow::commandActions() const {
  return {
      {trKey("action.new_tab"), new_tab_action_},
      {trKey("action.open"), open_action_},
      {trKey("action.save"), save_action_},
      {trKey("action.save_as"), save_as_action_},
      {trKey("action.close_tab"), close_tab_action_},
      {trKey("action.find"), find_action_},
      {trKey("action.find_in_files"), find_in_files_action_},
      {trKey("action.replace"), replace_action_},
      {trKey("action.go_to"), go_to_action_},
      {trKey("action.toggle_bookmark"), toggle_bookmark_action_},
      {trKey("action.next_bookmark"), next_bookmark_action_},
      {trKey("action.previous_bookmark"), previous_bookmark_action_},
      {trKey("action.performance_panel"), perf_panel_action_},
      {trKey("action.appearance_follow_system"), appearance_follow_system_action_},
      {trKey("action.appearance_light"), appearance_light_action_},
      {trKey("action.appearance_dark"), appearance_dark_action_},
      {trKey("action.restore_session_on_startup"), restore_session_on_startup_action_},
      {trKey("action.read_only_mode"), read_only_action_},
      {trKey("action.customize_shortcuts"), shortcuts_action_},
      {trKey("action.about"), about_action_},
  };
}

std::vector<std::tuple<QString, QString, QAction*>> MainWindow::shortcutBindings() const {
  return {
      {QStringLiteral("new_tab"), trKey("action.new_tab"), new_tab_action_},
      {QStringLiteral("open"), trKey("action.open"), open_action_},
      {QStringLiteral("close_tab"), trKey("action.close_tab"), close_tab_action_},
      {QStringLiteral("save"), trKey("action.save"), save_action_},
      {QStringLiteral("save_as"), trKey("action.save_as"), save_as_action_},
      {QStringLiteral("quit"), trKey("action.quit"), quit_action_},
      {QStringLiteral("undo"), trKey("action.undo"), undo_action_},
      {QStringLiteral("redo"), trKey("action.redo"), redo_action_},
      {QStringLiteral("cut"), trKey("action.cut"), cut_action_},
      {QStringLiteral("copy"), trKey("action.copy"), copy_action_},
      {QStringLiteral("paste"), trKey("action.paste"), paste_action_},
      {QStringLiteral("read_only_mode"), trKey("action.read_only_mode"), read_only_action_},
      {QStringLiteral("go_to"), trKey("action.go_to"), go_to_action_},
      {QStringLiteral("find"), trKey("action.find"), find_action_},
      {QStringLiteral("find_in_files"), trKey("action.find_in_files"), find_in_files_action_},
      {QStringLiteral("find_next"), trKey("action.find_next"), find_next_action_},
      {QStringLiteral("find_previous"), trKey("action.find_previous"), find_previous_action_},
      {QStringLiteral("replace"), trKey("action.replace"), replace_action_},
      {QStringLiteral("replace_next"), trKey("action.replace_next"), replace_next_action_},
      {QStringLiteral("replace_all"), trKey("action.replace_all"), replace_all_action_},
      {QStringLiteral("toggle_bookmark"), trKey("action.toggle_bookmark"), toggle_bookmark_action_},
      {QStringLiteral("next_bookmark"), trKey("action.next_bookmark"), next_bookmark_action_},
      {QStringLiteral("previous_bookmark"), trKey("action.previous_bookmark"), previous_bookmark_action_},
      {QStringLiteral("command_palette"), trKey("action.command_palette"), command_palette_action_},
  };
}

void MainWindow::applyShortcutOverrides() {
  QSettings settings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
  const QVariantMap overrides = settings.value(QString::fromLatin1(kShortcutOverridesKey)).toMap();
  for (const auto& binding : shortcutBindings()) {
    QAction* action = std::get<2>(binding);
    if (action == nullptr) {
      continue;
    }
    const QString id = std::get<0>(binding);
    if (!overrides.contains(id)) {
      continue;
    }
    const QKeySequence seq =
        QKeySequence::fromString(overrides.value(id).toString(), QKeySequence::PortableText);
    if (!seq.isEmpty()) {
      action->setShortcut(seq);
    }
  }
}

void MainWindow::showCommandPalette() {
  QDialog dialog(this);
  dialog.setWindowTitle(trKey("title.command_palette"));
  dialog.resize(520, 420);

  auto* layout = new QVBoxLayout(&dialog);
  auto* input = new QLineEdit(&dialog);
  input->setPlaceholderText(trKey("placeholder.command_palette"));
  layout->addWidget(input);

  auto* list = new QListWidget(&dialog);
  layout->addWidget(list, 1);

  for (const auto& entry : commandActions()) {
    QAction* action = entry.second;
    if (action == nullptr) {
      continue;
    }
    QString label = entry.first;
    const QKeySequence shortcut = action->shortcut();
    if (!shortcut.isEmpty()) {
      label += QStringLiteral("    %1").arg(shortcut.toString(QKeySequence::NativeText));
    }
    auto* item = new QListWidgetItem(label, list);
    item->setData(Qt::UserRole,
                  QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(action)));
    item->setHidden(!action->isEnabled());
  }

  auto updateFilter = [input, list]() {
    const QString needle = input->text().trimmed();
    for (int i = 0; i < list->count(); ++i) {
      QListWidgetItem* item = list->item(i);
      if (item == nullptr) {
        continue;
      }
      const bool match = needle.isEmpty() || item->text().contains(needle, Qt::CaseInsensitive);
      item->setHidden(!match);
    }
    for (int i = 0; i < list->count(); ++i) {
      QListWidgetItem* item = list->item(i);
      if (item != nullptr && !item->isHidden()) {
        list->setCurrentRow(i);
        break;
      }
    }
  };

  auto triggerCurrent = [&dialog, list]() {
    QListWidgetItem* item = list->currentItem();
    if (item == nullptr) {
      return;
    }
    const qulonglong raw = item->data(Qt::UserRole).toULongLong();
    QAction* action = reinterpret_cast<QAction*>(raw);
    if (action == nullptr || !action->isEnabled()) {
      return;
    }
    action->trigger();
    dialog.accept();
  };

  connect(input, &QLineEdit::textChanged, &dialog, updateFilter);
  connect(input, &QLineEdit::returnPressed, &dialog, triggerCurrent);
  connect(list, &QListWidget::itemActivated, &dialog, [&](QListWidgetItem*) {
    triggerCurrent();
  });

  updateFilter();
  input->setFocus();
  (void)dialog.exec();
}

void MainWindow::customizeShortcuts() {
  const auto bindings = shortcutBindings();
  if (bindings.empty()) {
    return;
  }

  QDialog dialog(this);
  dialog.setWindowTitle(trKey("title.customize_shortcuts"));
  dialog.resize(680, 460);

  auto* layout = new QVBoxLayout(&dialog);
  auto* table = new QTableWidget(static_cast<int>(bindings.size()), 2, &dialog);
  table->setHorizontalHeaderLabels(
      QStringList{trKey("label.shortcut_command"), trKey("label.shortcut_key")});
  table->horizontalHeader()->setStretchLastSection(true);
  table->verticalHeader()->setVisible(false);
  table->setSelectionMode(QAbstractItemView::NoSelection);

  for (int row = 0; row < static_cast<int>(bindings.size()); ++row) {
    const auto& binding = bindings[static_cast<std::size_t>(row)];
    auto* label_item = new QTableWidgetItem(std::get<1>(binding));
    label_item->setFlags(Qt::ItemIsEnabled);
    table->setItem(row, 0, label_item);

    auto* editor = new QKeySequenceEdit(&dialog);
    QAction* action = std::get<2>(binding);
    if (action != nullptr) {
      editor->setKeySequence(action->shortcut());
    }
    table->setCellWidget(row, 1, editor);
  }
  layout->addWidget(table, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  QVariantMap overrides;
  for (int row = 0; row < static_cast<int>(bindings.size()); ++row) {
    const auto& binding = bindings[static_cast<std::size_t>(row)];
    auto* editor = qobject_cast<QKeySequenceEdit*>(table->cellWidget(row, 1));
    if (editor == nullptr) {
      continue;
    }
    const QKeySequence seq = editor->keySequence();
    const QString id = std::get<0>(binding);
    if (!seq.isEmpty()) {
      overrides.insert(id, seq.toString(QKeySequence::PortableText));
    }
  }

  QSettings settings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
  settings.setValue(QString::fromLatin1(kShortcutOverridesKey), overrides);
  applyShortcutOverrides();
}

void MainWindow::toggleReadOnlyMode() {
  if (!hasCurrentTab()) {
    return;
  }
  TabState& tab = tabs_[currentTabIndex()];
  tab.read_only = !session_.isReadOnly();
  session_.setReadOnly(tab.read_only);
  refreshWindowState();
}

void MainWindow::showAboutDialog() {
  QMessageBox dialog(this);
  dialog.setIcon(QMessageBox::Information);
  dialog.setWindowTitle(trKey("title.about"));
  dialog.setText(trKey("about.product").arg(QApplication::applicationName().toHtmlEscaped()));
  dialog.setTextFormat(Qt::RichText);
  dialog.setTextInteractionFlags(Qt::TextBrowserInteraction);

  QString summary;
  summary += trKey("about.tagline").toHtmlEscaped();
  summary += QStringLiteral("<br/>");
  summary += trKey("about.version").arg(QString::fromLatin1(MASSIVEEDIT_VERSION)).toHtmlEscaped();
  summary += QStringLiteral("<br/>");
  summary += trKey("about.build_config").arg(QString::fromLatin1(MASSIVEEDIT_BUILD_CONFIG)).toHtmlEscaped();
  summary += QStringLiteral("<br/>");
  summary += trKey("about.build_time").arg(QStringLiteral(__DATE__), QStringLiteral(__TIME__)).toHtmlEscaped();
  summary += QStringLiteral("<br/>");
  summary += trKey("about.tech_stack").arg(QString::fromLatin1(QT_VERSION_STR)).toHtmlEscaped();
  summary += QStringLiteral("<br/>");
  summary += trKey("about.github_user").arg(QString::fromLatin1(kAboutGithubUser).toHtmlEscaped()).toHtmlEscaped();
  summary += QStringLiteral("<br/>");
  const QString contact_email = QString::fromLatin1(kAboutContactEmail).toHtmlEscaped();
  summary += trKey("about.contact_email")
                 .arg(QStringLiteral("<a href=\"mailto:%1\">%2</a>").arg(contact_email, contact_email));
  summary += QStringLiteral("<br/>");
  const QString repository_url = QString::fromLatin1(kAboutProjectUrl).toHtmlEscaped();
  summary += trKey("about.repository")
                 .arg(QStringLiteral("<a href=\"%1\">%2</a>").arg(repository_url, repository_url));
  summary += QStringLiteral("<br/>");
  const QString issues_url = QString::fromLatin1(kAboutIssuesUrl).toHtmlEscaped();
  summary += trKey("about.issues")
                 .arg(QStringLiteral("<a href=\"%1\">%2</a>").arg(issues_url, issues_url));
  summary += QStringLiteral("<br/>");
  const QString license_url = QString::fromLatin1(kAboutLicenseUrl).toHtmlEscaped();
  summary += trKey("about.license")
                 .arg(QStringLiteral("<a href=\"%1\">%2</a>").arg(license_url, QStringLiteral("MIT License")));
  summary += QStringLiteral("<br/>");
  summary += trKey("about.platform")
                 .arg(QSysInfo::prettyProductName().toHtmlEscaped(),
                      QSysInfo::kernelType().toHtmlEscaped(),
                      QSysInfo::kernelVersion().toHtmlEscaped(),
                      QSysInfo::currentCpuArchitecture().toHtmlEscaped())
                 .toHtmlEscaped();
  summary += QStringLiteral("<br/>");
  summary += trKey("about.copyright")
                 .arg(QString::number(QDate::currentDate().year()),
                      QString::fromLatin1(kAboutGithubUser))
                 .toHtmlEscaped();
  dialog.setInformativeText(summary);
  dialog.exec();
}

void MainWindow::buildFindResultsPanel() {
  find_results_dock_ = new QDockWidget(trKey("title.find_results"), this);
  find_results_dock_->setObjectName(QStringLiteral("findResultsDock"));

  find_results_tree_ = new QTreeWidget(find_results_dock_);
  find_results_tree_->setColumnCount(2);
  find_results_tree_->setHeaderLabels(
      QStringList{trKey("label.find_results_location"), trKey("label.find_results_preview")});
  find_results_tree_->setAlternatingRowColors(true);
  find_results_tree_->setRootIsDecorated(true);
  find_results_tree_->setUniformRowHeights(true);
  find_results_tree_->header()->setStretchLastSection(true);
  find_results_tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  connect(find_results_tree_,
          &QTreeWidget::itemActivated,
          this,
          &MainWindow::handleFindResultActivated);

  find_results_dock_->setWidget(find_results_tree_);
  addDockWidget(Qt::BottomDockWidgetArea, find_results_dock_);
  find_results_dock_->hide();
}

void MainWindow::clearFindResults() {
  if (find_results_tree_ != nullptr) {
    find_results_tree_->clear();
  }
}

void MainWindow::findInFiles() {
  QDialog dialog(this);
  dialog.setWindowTitle(trKey("title.find_in_files"));
  dialog.resize(560, 180);

  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();

  auto* query_input = new QLineEdit(&dialog);
  query_input->setPlaceholderText(trKey("placeholder.search_text"));
  form->addRow(trKey("label.find"), query_input);

  auto* dir_row = new QWidget(&dialog);
  auto* dir_layout = new QHBoxLayout(dir_row);
  dir_layout->setContentsMargins(0, 0, 0, 0);
  auto* dir_input = new QLineEdit(dir_row);
  QString initial_dir = last_find_in_files_dir_;
  if (initial_dir.isEmpty()) {
    const QString current_path = session_.filePath();
    initial_dir = current_path.isEmpty() ? QDir::homePath() : QFileInfo(current_path).absolutePath();
  }
  dir_input->setText(initial_dir);
  auto* browse_button = new QPushButton(trKey("action.browse"), dir_row);
  dir_layout->addWidget(dir_input, 1);
  dir_layout->addWidget(browse_button);
  form->addRow(trKey("label.directory"), dir_row);

  auto* case_box = new QCheckBox(trKey("label.case"), &dialog);
  auto* regex_box = new QCheckBox(trKey("label.regex"), &dialog);
  auto* options_row = new QWidget(&dialog);
  auto* options_layout = new QHBoxLayout(options_row);
  options_layout->setContentsMargins(0, 0, 0, 0);
  options_layout->addWidget(case_box);
  options_layout->addWidget(regex_box);
  options_layout->addStretch(1);
  form->addRow(trKey("label.options"), options_row);

  layout->addLayout(form);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(browse_button, &QPushButton::clicked, &dialog, [this, dir_input]() {
    const QString selected = QFileDialog::getExistingDirectory(
        this, trKey("title.select_directory"), dir_input->text().trimmed());
    if (!selected.isEmpty()) {
      dir_input->setText(selected);
    }
  });

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const QString query = query_input->text().trimmed();
  const QString root = dir_input->text().trimmed();
  if (query.isEmpty()) {
    statusBar()->showMessage(trKey("msg.search_enter_text"), 3000);
    return;
  }
  if (root.isEmpty() || !QDir(root).exists()) {
    QMessageBox::warning(this, trKey("title.find_in_files"), trKey("msg.invalid_directory"));
    return;
  }

  last_find_in_files_dir_ = root;
  core::SearchOptions options;
  options.case_sensitive = case_box->isChecked();
  options.regex = regex_box->isChecked();
  runFindInFiles(query, root, options);
}

void MainWindow::runFindInFiles(const QString& query,
                                const QString& root_dir,
                                const core::SearchOptions& options) {
  if (find_results_tree_ == nullptr || find_results_dock_ == nullptr) {
    return;
  }

  clearFindResults();
  find_results_dock_->show();
  find_results_dock_->raise();
  statusBar()->showMessage(trKey("msg.find_in_files_running"), 2000);

  const Qt::CaseSensitivity cs = options.case_sensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
  QRegularExpression regex;
  if (options.regex) {
    QRegularExpression::PatternOptions pattern_opts = QRegularExpression::NoPatternOption;
    if (!options.case_sensitive) {
      pattern_opts |= QRegularExpression::CaseInsensitiveOption;
    }
    regex = QRegularExpression(query, pattern_opts);
    if (!regex.isValid()) {
      QMessageBox::warning(this,
                           trKey("title.find_in_files"),
                           trKey("msg.invalid_regex").arg(regex.errorString()));
      return;
    }
  }

  std::map<QString, QTreeWidgetItem*> file_nodes;
  int file_count = 0;
  int hit_count = 0;

  QDirIterator it(root_dir,
                  QDir::Files | QDir::Readable | QDir::NoSymLinks | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext() && hit_count < kMaxFindInFilesResults) {
    const QString path = it.next();
    ++file_count;
    if (file_count % 200 == 0) {
      QApplication::processEvents();
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      continue;
    }
    if (looksBinary(file.peek(4096))) {
      continue;
    }

    int line_number = 0;
    while (!file.atEnd() && hit_count < kMaxFindInFilesResults) {
      const QByteArray raw = file.readLine();
      ++line_number;
      const QString line = QString::fromUtf8(raw);
      if (line.isEmpty()) {
        continue;
      }

      auto ensureNode = [&]() -> QTreeWidgetItem* {
        auto node_it = file_nodes.find(path);
        if (node_it != file_nodes.end()) {
          return node_it->second;
        }
        auto* file_item = new QTreeWidgetItem(find_results_tree_);
        file_item->setText(0, QDir::toNativeSeparators(path));
        file_item->setText(1, QString());
        file_nodes.emplace(path, file_item);
        return file_item;
      };

      if (options.regex) {
        QRegularExpressionMatchIterator match_it = regex.globalMatch(line);
        while (match_it.hasNext() && hit_count < kMaxFindInFilesResults) {
          const QRegularExpressionMatch match = match_it.next();
          if (!match.hasMatch() || match.capturedLength() <= 0) {
            continue;
          }
          QTreeWidgetItem* file_item = ensureNode();
          auto* child = new QTreeWidgetItem(file_item);
          child->setText(0,
                         trKey("label.find_result_line_col")
                             .arg(static_cast<qulonglong>(line_number))
                             .arg(static_cast<qulonglong>(match.capturedStart() + 1)));
          child->setText(1, normalizedSnippet(line));
          child->setData(0, Qt::UserRole, path);
          child->setData(0, Qt::UserRole + 1, line_number);
          child->setData(0, Qt::UserRole + 2, match.capturedStart() + 1);
          ++hit_count;
        }
      } else {
        int from = 0;
        while (hit_count < kMaxFindInFilesResults) {
          const int found = line.indexOf(query, from, cs);
          if (found < 0) {
            break;
          }
          QTreeWidgetItem* file_item = ensureNode();
          auto* child = new QTreeWidgetItem(file_item);
          child->setText(0,
                         trKey("label.find_result_line_col")
                             .arg(static_cast<qulonglong>(line_number))
                             .arg(static_cast<qulonglong>(found + 1)));
          child->setText(1, normalizedSnippet(line));
          child->setData(0, Qt::UserRole, path);
          child->setData(0, Qt::UserRole + 1, line_number);
          child->setData(0, Qt::UserRole + 2, found + 1);
          ++hit_count;
          from = found + std::max(1, static_cast<int>(query.size()));
        }
      }
    }
  }

  for (auto& entry : file_nodes) {
    if (entry.second != nullptr) {
      entry.second->setExpanded(true);
    }
  }

  if (hit_count == 0) {
    statusBar()->showMessage(trKey("msg.find_in_files_none"), 4000);
    return;
  }
  if (hit_count >= kMaxFindInFilesResults) {
    statusBar()->showMessage(
        trKey("msg.find_in_files_done_truncated")
            .arg(static_cast<qulonglong>(hit_count))
            .arg(static_cast<qulonglong>(file_count))
            .arg(static_cast<qulonglong>(kMaxFindInFilesResults)),
        6000);
    return;
  }
  statusBar()->showMessage(
      trKey("msg.find_in_files_done")
          .arg(static_cast<qulonglong>(hit_count))
          .arg(static_cast<qulonglong>(file_count)),
      5000);
}

void MainWindow::handleFindResultActivated(QTreeWidgetItem* item, int column) {
  Q_UNUSED(column);
  if (item == nullptr) {
    return;
  }
  const QString path = item->data(0, Qt::UserRole).toString();
  if (path.isEmpty()) {
    return;
  }
  const int line = item->data(0, Qt::UserRole + 1).toInt();
  const int col = item->data(0, Qt::UserRole + 2).toInt();
  if (!openFileInNewTab(path)) {
    return;
  }
  if (view_ != nullptr) {
    const std::size_t line_index = (line > 0) ? static_cast<std::size_t>(line - 1) : 0;
    const std::size_t col_index = (col > 0) ? static_cast<std::size_t>(col - 1) : 0;
    (void)view_->goToLineColumn(line_index, col_index, true);
  }
}

void MainWindow::showSearchPanel(bool focus_replace) {
  if (search_panel_ == nullptr) {
    return;
  }
  search_panel_->setVisible(true);
  if (focus_replace && replace_input_ != nullptr) {
    replace_input_->setFocus();
    replace_input_->selectAll();
    return;
  }
  if (find_input_ != nullptr) {
    find_input_->setFocus();
    find_input_->selectAll();
  }
}

core::SearchOptions MainWindow::searchOptionsFromPanel() const {
  core::SearchOptions options;
  options.case_sensitive = (case_sensitive_checkbox_ != nullptr) ? case_sensitive_checkbox_->isChecked() : false;
  options.regex = (regex_checkbox_ != nullptr) ? regex_checkbox_->isChecked() : false;
  return options;
}

void MainWindow::updateSearchPanelSummary() {
  if (match_count_label_ == nullptr) {
    return;
  }

  if (pending_search_request_id_ != 0) {
    match_count_label_->setText(trKey("label.searching"));
    return;
  }

  if (find_input_ == nullptr || find_input_->text().isEmpty()) {
    match_count_label_->setText(trKey("label.matches_zero"));
    return;
  }

  if (search_matches_.empty()) {
    match_count_label_->setText(trKey("label.matches_zero"));
    return;
  }

  const std::size_t current = std::min<std::size_t>(active_match_index_, search_matches_.size() - 1);
  match_count_label_->setText(
      QStringLiteral("%1/%2")
          .arg(static_cast<qulonglong>(current + 1))
          .arg(static_cast<qulonglong>(search_matches_.size())));
}

void MainWindow::buildPerformancePanel() {
  perf_dock_ = new QDockWidget(trKey("title.performance"), this);
  perf_dock_->setObjectName(QStringLiteral("perfDock"));

  QWidget* body = new QWidget(perf_dock_);
  auto* layout = new QVBoxLayout(body);
  layout->setContentsMargins(8, 8, 8, 8);

  perf_summary_label_ = new QLabel(body);
  perf_summary_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  perf_summary_label_->setWordWrap(true);
  layout->addWidget(perf_summary_label_);

  perf_dock_->setWidget(body);
  addDockWidget(Qt::RightDockWidgetArea, perf_dock_);
  perf_dock_->hide();
}

void MainWindow::updatePerformancePanel() {
  if (perf_summary_label_ == nullptr || view_scrolling_) {
    return;
  }

  const auto stats = session_.chunkCacheStats();
  const std::uint64_t reads = std::max<std::uint64_t>(stats.reads, 1);
  const double hit_ratio = static_cast<double>(stats.cache_hits) / static_cast<double>(reads);

  QString text;
  text += trKey("perf.file")
              .arg(session_.filePath().isEmpty() ? trKey("perf.untitled") : session_.filePath());
  text += trKey("perf.tabs").arg(static_cast<qulonglong>(tabs_.size()));
  text += trKey("perf.bytes").arg(static_cast<qulonglong>(session_.byteSize()));
  text += trKey("perf.lines").arg(static_cast<qulonglong>(session_.lineCount()));
  text += trKey("perf.indexed_lines").arg(static_cast<qulonglong>(session_.indexedLineCount()));
  text += trKey("perf.index_complete")
              .arg(session_.isLineIndexComplete() ? trKey("status.yes") : trKey("status.no"));
  text += trKey("perf.cache_reads").arg(static_cast<qulonglong>(stats.reads));
  text += trKey("perf.cache_hit_ratio").arg(hit_ratio * 100.0, 0, 'f', 1);
  text += trKey("perf.cache_bytes").arg(static_cast<qulonglong>(stats.cached_bytes));
  text += trKey("perf.cache_chunks").arg(static_cast<qulonglong>(stats.cached_chunks));
  text += trKey("perf.bytes_served").arg(static_cast<qulonglong>(stats.bytes_served));

  perf_summary_label_->setText(text);
}

void MainWindow::syncFormatActions() {
  tab_width_spaces_ = normalizeTabWidthValue(tab_width_spaces_);
  if (view_ != nullptr && view_->tabWidth() != tab_width_spaces_) {
    view_->setTabWidth(tab_width_spaces_);
  }
  if (encoding_utf8_action_ != nullptr) {
    QSignalBlocker blocker(*encoding_utf8_action_);
    encoding_utf8_action_->setChecked(session_.textEncoding() == core::DocumentSession::TextEncoding::kUtf8);
  }
  if (encoding_gbk_action_ != nullptr) {
    QSignalBlocker blocker(*encoding_gbk_action_);
    encoding_gbk_action_->setChecked(session_.textEncoding() == core::DocumentSession::TextEncoding::kGbk);
  }
  if (line_ending_lf_action_ != nullptr) {
    QSignalBlocker blocker(*line_ending_lf_action_);
    line_ending_lf_action_->setChecked(session_.lineEnding() == core::DocumentSession::LineEnding::kLf);
  }
  if (line_ending_crlf_action_ != nullptr) {
    QSignalBlocker blocker(*line_ending_crlf_action_);
    line_ending_crlf_action_->setChecked(session_.lineEnding() == core::DocumentSession::LineEnding::kCrlf);
  }
  if (tab_width_2_action_ != nullptr) {
    QSignalBlocker blocker(*tab_width_2_action_);
    tab_width_2_action_->setChecked(tab_width_spaces_ == 2);
  }
  if (tab_width_4_action_ != nullptr) {
    QSignalBlocker blocker(*tab_width_4_action_);
    tab_width_4_action_->setChecked(tab_width_spaces_ == 4);
  }
  if (tab_width_8_action_ != nullptr) {
    QSignalBlocker blocker(*tab_width_8_action_);
    tab_width_8_action_->setChecked(tab_width_spaces_ == 8);
  }
}

void MainWindow::updateFileWatcher() {
  if (file_watcher_ == nullptr) {
    return;
  }

  const QStringList watched = file_watcher_->files();
  if (!watched.isEmpty()) {
    file_watcher_->removePaths(watched);
  }

  const QString path = session_.filePath();
  if (path.isEmpty()) {
    return;
  }

  if (QFileInfo::exists(path)) {
    file_watcher_->addPath(path);
  }
}

void MainWindow::handleWatchedFileChanged(const QString& path) {
  updateFileWatcher();
  if (suppress_file_change_prompt_) {
    return;
  }

  if (path.isEmpty() || path != session_.filePath() || !QFileInfo::exists(path)) {
    return;
  }

  while (true) {
    QMessageBox msg(this);
    msg.setWindowTitle(trKey("title.file_changed"));
    msg.setText(trKey("msg.file_changed"));
    QPushButton* reload_button = nullptr;
    QPushButton* keep_button = nullptr;
    QPushButton* overwrite_button = nullptr;
    QPushButton* save_as_button = nullptr;
    QPushButton* compare_button = nullptr;

    if (session_.isDirty()) {
      msg.setInformativeText(trKey("msg.file_changed_reload_dirty"));
      overwrite_button = msg.addButton(trKey("action.overwrite"), QMessageBox::AcceptRole);
      save_as_button = msg.addButton(trKey("action.save_as"), QMessageBox::ActionRole);
      compare_button = msg.addButton(trKey("action.compare"), QMessageBox::ActionRole);
      reload_button = msg.addButton(trKey("action.reload"), QMessageBox::DestructiveRole);
      keep_button = msg.addButton(trKey("action.keep_current"), QMessageBox::RejectRole);
      msg.setDefaultButton(keep_button);
    } else {
      msg.setInformativeText(trKey("msg.file_changed_reload"));
      reload_button = msg.addButton(trKey("action.reload"), QMessageBox::AcceptRole);
      keep_button = msg.addButton(trKey("action.keep_current"), QMessageBox::RejectRole);
      msg.setDefaultButton(reload_button);
    }

    msg.exec();
    QPushButton* clicked = qobject_cast<QPushButton*>(msg.clickedButton());
    if (clicked == nullptr || clicked == keep_button) {
      return;
    }
    if (clicked == compare_button) {
      QMessageBox diff(this);
      diff.setWindowTitle(trKey("title.compare_with_disk"));
      diff.setText(trKey("msg.compare_summary"));
      diff.setDetailedText(buildExternalCompareSummary(path));
      diff.setStandardButtons(QMessageBox::Ok);
      diff.exec();
      continue;
    }
    if (clicked == overwrite_button) {
      if (saveCurrentFile()) {
        refreshWindowState();
      }
      return;
    }
    if (clicked == save_as_button) {
      (void)saveAsInteractive();
      return;
    }
    if (clicked != reload_button) {
      return;
    }

    QString error;
    suppress_file_change_prompt_ = true;
    const bool ok = session_.openFile(path, &error);
    suppress_file_change_prompt_ = false;
    if (!ok) {
      QMessageBox::critical(this, trKey("msg.reload_failed"), error);
      return;
    }

    if (hasCurrentTab()) {
      TabState& tab = tabs_[currentTabIndex()];
      tab.file_path = path;
      tab.snapshot_dirty = false;
      (void)removeSnapshotFile(tab.snapshot_path);
      if (!tab.operation_log_path.isEmpty()) {
        (void)QFile::remove(tab.operation_log_path);
      }
    }

    clearSearchState();
    refreshWindowState();
    return;
  }
}

void MainWindow::autosaveCurrentTab() {
  if (!hasCurrentTab()) {
    return;
  }
  (void)saveCurrentTabState();
}

bool MainWindow::hasCurrentTab() const {
  return active_tab_index_ >= 0 && active_tab_index_ < static_cast<int>(tabs_.size());
}

int MainWindow::currentTabIndex() const {
  return hasCurrentTab() ? active_tab_index_ : -1;
}

QString MainWindow::untitledNameForIndex(int idx) const {
  return trKey("label.untitled_n").arg(idx);
}

QString MainWindow::tabDisplayName(const TabState& tab) const {
  if (!tab.file_path.isEmpty()) {
    return QFileInfo(tab.file_path).fileName();
  }
  if (tab.recovered_id > 0) {
    return trKey("label.recovered_n").arg(tab.recovered_id);
  }
  if (tab.untitled_id > 0) {
    return trKey("label.untitled_n").arg(tab.untitled_id);
  }
  if (!tab.untitled_label.isEmpty()) {
    return tab.untitled_label;
  }
  return trKey("label.untitled");
}

QString MainWindow::currentTabTitle() const {
  if (!hasCurrentTab()) {
    return trKey("label.untitled");
  }

  const TabState& tab = tabs_[currentTabIndex()];
  QString title = tab.file_path.isEmpty() ? tabDisplayName(tab) : tab.file_path;
  if (title.isEmpty()) {
    title = trKey("label.untitled");
  }
  if (session_.isDirty()) {
    title += QStringLiteral(" *");
  }
  return title;
}

void MainWindow::updateTabTitle(int index) {
  if (tab_bar_ == nullptr || index < 0 || index >= static_cast<int>(tabs_.size()) || index >= tab_bar_->count()) {
    return;
  }

  const TabState& tab = tabs_[index];
  QString base = tabDisplayName(tab);
  if (base.isEmpty()) {
    base = tab.file_path;
  }
  if (base.isEmpty()) {
    base = trKey("label.untitled");
  }

  const bool dirty = (index == currentTabIndex()) ? session_.isDirty() : tab.snapshot_dirty;
  if (dirty) {
    base += QStringLiteral(" *");
  }
  tab_bar_->setTabText(index, base);
}

void MainWindow::updateAllTabTitles() {
  for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
    updateTabTitle(i);
  }
}

QString MainWindow::recoveryDirPath() const {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir dir(base);
  if (!dir.exists()) {
    (void)dir.mkpath(QStringLiteral("."));
  }
  const QString recovery = dir.filePath(QStringLiteral("recovery"));
  QDir recovery_dir(recovery);
  if (!recovery_dir.exists()) {
    (void)recovery_dir.mkpath(QStringLiteral("."));
  }
  return recovery;
}

QString MainWindow::snapshotPathForTab(int index) const {
  const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
  return QDir(recoveryDirPath()).filePath(QStringLiteral("tab_%1_%2.snapshot").arg(index).arg(token));
}

QString MainWindow::operationLogPathForTab(int index) const {
  const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
  return QDir(recoveryDirPath()).filePath(QStringLiteral("tab_%1_%2.oplog").arg(index).arg(token));
}

bool MainWindow::writeSnapshotFile(const QString& path, const SnapshotPayload& payload) const {
  if (path.isEmpty()) {
    return false;
  }

  QSaveFile out(path);
  if (!out.open(QIODevice::WriteOnly)) {
    return false;
  }

  QDataStream stream(&out);
  stream.setVersion(QDataStream::Qt_6_0);
  stream << static_cast<quint32>(kSnapshotMagic);
  stream << static_cast<quint32>(kSnapshotVersion);
  stream << payload.file_path;
  stream << payload.dirty;
  stream << static_cast<qint32>(payload.encoding);
  stream << static_cast<qint32>(payload.line_ending);
  stream << static_cast<quint64>(payload.cursor_line);
  stream << static_cast<quint64>(payload.cursor_column);
  stream << static_cast<quint32>(payload.bookmarks.size());
  for (const std::size_t line : payload.bookmarks) {
    stream << static_cast<quint64>(line);
  }
  stream << payload.content;

  if (stream.status() != QDataStream::Ok) {
    out.cancelWriting();
    return false;
  }
  return out.commit();
}

bool MainWindow::readSnapshotFile(const QString& path, SnapshotPayload* payload) const {
  if (payload == nullptr || path.isEmpty()) {
    return false;
  }

  QFile in(path);
  if (!in.open(QIODevice::ReadOnly)) {
    return false;
  }

  QDataStream stream(&in);
  stream.setVersion(QDataStream::Qt_6_0);

  quint32 magic = 0;
  quint32 version = 0;
  stream >> magic;
  stream >> version;
  if (magic != kSnapshotMagic || version != kSnapshotVersion) {
    return false;
  }

  SnapshotPayload out;
  qint32 encoding = 0;
  qint32 line_ending = 0;
  quint64 cursor_line = 0;
  quint64 cursor_column = 0;
  quint32 bookmark_count = 0;

  stream >> out.file_path;
  stream >> out.dirty;
  stream >> encoding;
  stream >> line_ending;
  stream >> cursor_line;
  stream >> cursor_column;
  stream >> bookmark_count;
  for (quint32 i = 0; i < bookmark_count; ++i) {
    quint64 value = 0;
    stream >> value;
    out.bookmarks.insert(static_cast<std::size_t>(value));
  }
  stream >> out.content;

  if (stream.status() != QDataStream::Ok) {
    return false;
  }

  out.encoding = static_cast<core::DocumentSession::TextEncoding>(encoding);
  out.line_ending = static_cast<core::DocumentSession::LineEnding>(line_ending);
  out.cursor_line = static_cast<std::size_t>(cursor_line);
  out.cursor_column = static_cast<std::size_t>(cursor_column);

  *payload = std::move(out);
  return true;
}

bool MainWindow::removeSnapshotFile(const QString& path) const {
  if (path.isEmpty() || !QFileInfo::exists(path)) {
    return true;
  }
  return QFile::remove(path);
}

void MainWindow::saveSessionState() const {
  QSettings settings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));

  QVariantList tab_list;
  for (const TabState& tab : tabs_) {
    QVariantMap item;
    item.insert(QStringLiteral("file_path"), tab.file_path);
    item.insert(QStringLiteral("snapshot_path"), tab.snapshot_path);
    item.insert(QStringLiteral("operation_log_path"), tab.operation_log_path);
    item.insert(QStringLiteral("snapshot_dirty"), tab.snapshot_dirty);
    item.insert(QStringLiteral("encoding"), static_cast<int>(tab.encoding));
    item.insert(QStringLiteral("line_ending"), static_cast<int>(tab.line_ending));
    item.insert(QStringLiteral("cursor_line"), static_cast<qulonglong>(tab.cursor_line));
    item.insert(QStringLiteral("cursor_column"), static_cast<qulonglong>(tab.cursor_column));
    item.insert(QStringLiteral("untitled_id"), tab.untitled_id);
    item.insert(QStringLiteral("recovered_id"), tab.recovered_id);
    item.insert(QStringLiteral("read_only"), tab.read_only);

    QVariantList bookmarks;
    for (const std::size_t line : tab.bookmarks) {
      bookmarks.push_back(static_cast<qulonglong>(line));
    }
    item.insert(QStringLiteral("bookmarks"), bookmarks);
    tab_list.push_back(item);
  }

  settings.setValue(QString::fromLatin1(kSessionTabsKey), tab_list);
  settings.setValue(QString::fromLatin1(kSessionActiveTabKey), active_tab_index_);
  settings.setValue(QString::fromLatin1(kSessionNextUntitledKey), next_untitled_index_);
}

bool MainWindow::restoreSessionState() {
  QSettings settings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
  const QVariantList tab_list = settings.value(QString::fromLatin1(kSessionTabsKey)).toList();
  if (tab_list.isEmpty()) {
    return false;
  }

  tabs_.clear();
  while (tab_bar_->count() > 0) {
    tab_bar_->removeTab(0);
  }
  active_tab_index_ = -1;

  int max_untitled_id = 0;
  for (const QVariant& value : tab_list) {
    const QVariantMap item = value.toMap();
    if (item.isEmpty()) {
      continue;
    }

    TabState tab;
    tab.file_path = item.value(QStringLiteral("file_path")).toString();
    tab.snapshot_path = item.value(QStringLiteral("snapshot_path")).toString();
    tab.operation_log_path = item.value(QStringLiteral("operation_log_path")).toString();
    tab.snapshot_dirty = item.value(QStringLiteral("snapshot_dirty")).toBool();
    tab.encoding = static_cast<core::DocumentSession::TextEncoding>(item.value(QStringLiteral("encoding")).toInt());
    tab.line_ending =
        static_cast<core::DocumentSession::LineEnding>(item.value(QStringLiteral("line_ending")).toInt());
    tab.cursor_line = static_cast<std::size_t>(item.value(QStringLiteral("cursor_line")).toULongLong());
    tab.cursor_column = static_cast<std::size_t>(item.value(QStringLiteral("cursor_column")).toULongLong());
    tab.untitled_id = item.value(QStringLiteral("untitled_id")).toInt();
    tab.recovered_id = item.value(QStringLiteral("recovered_id")).toInt();
    tab.read_only = item.value(QStringLiteral("read_only")).toBool();
    if (tab.recovered_id > 0) {
      tab.untitled_label = trKey("label.recovered_n").arg(tab.recovered_id);
    } else if (tab.untitled_id > 0) {
      tab.untitled_label = untitledNameForIndex(tab.untitled_id);
    }
    if (tab.operation_log_path.isEmpty()) {
      tab.operation_log_path = operationLogPathForTab(static_cast<int>(tabs_.size()));
    }
    const QVariantList bookmark_list = item.value(QStringLiteral("bookmarks")).toList();
    for (const QVariant& b : bookmark_list) {
      tab.bookmarks.insert(static_cast<std::size_t>(b.toULongLong()));
    }

    if (!tab.file_path.isEmpty() && !QFileInfo::exists(tab.file_path) &&
        (tab.snapshot_path.isEmpty() || !QFileInfo::exists(tab.snapshot_path)) &&
        (tab.operation_log_path.isEmpty() || !QFileInfo::exists(tab.operation_log_path))) {
      continue;
    }
    if (tab.file_path.isEmpty() && (tab.snapshot_path.isEmpty() || !QFileInfo::exists(tab.snapshot_path)) &&
        (tab.operation_log_path.isEmpty() || !QFileInfo::exists(tab.operation_log_path))) {
      continue;
    }

    max_untitled_id = std::max(max_untitled_id, std::max(tab.untitled_id, tab.recovered_id));
    tabs_.push_back(tab);
    tab_bar_->addTab(tabDisplayName(tab));
  }

  if (tabs_.empty()) {
    return false;
  }

  next_untitled_index_ = std::max(settings.value(QString::fromLatin1(kSessionNextUntitledKey)).toInt(),
                                  max_untitled_id + 1);
  const int requested_active = settings.value(QString::fromLatin1(kSessionActiveTabKey), 0).toInt();
  const int active = std::clamp(requested_active, 0, static_cast<int>(tabs_.size()) - 1);
  if (!switchToTab(active) && !switchToTab(0)) {
    tabs_.clear();
    while (tab_bar_->count() > 0) {
      tab_bar_->removeTab(0);
    }
    active_tab_index_ = -1;
    return false;
  }
  return true;
}

void MainWindow::cleanupOrphanSnapshots() const {
  QDir dir(recoveryDirPath());
  const QStringList files = dir.entryList(
      QStringList{QStringLiteral("*.snapshot"), QStringLiteral("*.oplog")},
      QDir::Files);
  std::set<QString> keep_paths;
  for (const TabState& tab : tabs_) {
    if (!tab.snapshot_path.isEmpty() && QFileInfo::exists(tab.snapshot_path)) {
      keep_paths.insert(QFileInfo(tab.snapshot_path).absoluteFilePath());
    }
    if (!tab.operation_log_path.isEmpty() && QFileInfo::exists(tab.operation_log_path)) {
      keep_paths.insert(QFileInfo(tab.operation_log_path).absoluteFilePath());
    }
  }

  for (const QString& name : files) {
    const QString full = QFileInfo(dir.filePath(name)).absoluteFilePath();
    if (keep_paths.find(full) == keep_paths.end()) {
      (void)QFile::remove(full);
    }
  }
}

QString MainWindow::buildExternalCompareSummary(const QString& path) const {
  if (path.isEmpty()) {
    return trKey("msg.compare_no_data");
  }

  QFile disk(path);
  if (!disk.open(QIODevice::ReadOnly)) {
    return trKey("msg.compare_read_failed");
  }

  const QByteArray disk_bytes = disk.readAll();
  const std::string current_bytes = session_.bytesAt(0, static_cast<std::size_t>(session_.byteSize()));
  const QString disk_text =
      session_.decodeBytesFromStorage(std::string_view(disk_bytes.constData(), static_cast<std::size_t>(disk_bytes.size())));
  const QString current_text = session_.decodeBytesFromStorage(current_bytes);

  const QStringList disk_lines = disk_text.split(QLatin1Char('\n'));
  const QStringList current_lines = current_text.split(QLatin1Char('\n'));
  const int limit = 120;
  int shown = 0;

  QStringList out;
  const int max_lines = std::max(disk_lines.size(), current_lines.size());
  for (int i = 0; i < max_lines && shown < limit; ++i) {
    const QString lhs = (i < disk_lines.size()) ? disk_lines.at(i) : QString();
    const QString rhs = (i < current_lines.size()) ? current_lines.at(i) : QString();
    if (lhs == rhs) {
      continue;
    }
    out << trKey("label.compare_line").arg(static_cast<qulonglong>(i + 1));
    out << trKey("label.compare_disk").arg(normalizedSnippet(lhs));
    out << trKey("label.compare_current").arg(normalizedSnippet(rhs));
    out << QString();
    ++shown;
  }

  if (out.isEmpty()) {
    return trKey("msg.compare_no_diff");
  }
  if (shown >= limit) {
    out << trKey("msg.compare_truncated").arg(static_cast<qulonglong>(limit));
  }
  return out.join(QLatin1Char('\n'));
}

void MainWindow::restoreRecoverySnapshotsIfAny() {
  QDir dir(recoveryDirPath());
  const QStringList files = dir.entryList(QStringList{QStringLiteral("*.snapshot")}, QDir::Files, QDir::Name);
  if (files.isEmpty()) {
    return;
  }

  const QMessageBox::StandardButton choice = QMessageBox::question(
      this,
      trKey("title.recover_tabs"),
      trKey("msg.recover_tabs"),
      QMessageBox::Yes | QMessageBox::No,
      QMessageBox::Yes);

  if (choice != QMessageBox::Yes) {
    clearAllRecoverySnapshots();
    return;
  }

  tabs_.clear();
  while (tab_bar_->count() > 0) {
    tab_bar_->removeTab(0);
  }
  active_tab_index_ = -1;

  for (const QString& name : files) {
    const QString path = dir.filePath(name);
    SnapshotPayload payload;
    if (!readSnapshotFile(path, &payload)) {
      (void)QFile::remove(path);
      continue;
    }

    TabState tab;
    tab.file_path = payload.file_path;
    tab.recovered_id = next_untitled_index_++;
    tab.untitled_label = trKey("label.recovered_n").arg(tab.recovered_id);
    tab.snapshot_path = path;
    tab.operation_log_path = operationLogPathForTab(static_cast<int>(tabs_.size()));
    tab.snapshot_dirty = payload.dirty;
    tab.encoding = payload.encoding;
    tab.line_ending = payload.line_ending;
    tab.cursor_line = payload.cursor_line;
    tab.cursor_column = payload.cursor_column;
    tab.bookmarks = payload.bookmarks;

    tabs_.push_back(tab);

    QString label = tabDisplayName(tab);
    if (label.isEmpty()) {
      label = tab.untitled_label;
    }
    tab_bar_->addTab(label);
  }

  if (!tabs_.empty()) {
    (void)switchToTab(0);
    statusBar()->showMessage(trKey("msg.recovered_tabs").arg(static_cast<qulonglong>(tabs_.size())),
                             5000);
  }
}

void MainWindow::clearAllRecoverySnapshots() {
  QDir dir(recoveryDirPath());
  const QStringList files = dir.entryList(
      QStringList{QStringLiteral("*.snapshot"), QStringLiteral("*.oplog")},
      QDir::Files);
  for (const QString& name : files) {
    (void)QFile::remove(dir.filePath(name));
  }
}

void MainWindow::addRecentFile(const QString& path) {
  const QString absolute_path = QFileInfo(path).absoluteFilePath();
  if (absolute_path.isEmpty()) {
    return;
  }
  recent_files_.removeAll(absolute_path);
  recent_files_.prepend(absolute_path);
  while (recent_files_.size() > kMaxRecentFiles) {
    recent_files_.removeLast();
  }
  saveRecentFiles();
  rebuildRecentFilesMenu();
}

void MainWindow::clearRecentFiles() {
  if (recent_files_.isEmpty()) {
    return;
  }
  recent_files_.clear();
  saveRecentFiles();
  rebuildRecentFilesMenu();
}

void MainWindow::loadRecentFiles() {
  QSettings settings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
  const QStringList stored = settings.value(QString::fromLatin1(kRecentFilesKey)).toStringList();
  recent_files_.clear();
  for (const QString& path : stored) {
    const QString absolute_path = QFileInfo(path).absoluteFilePath();
    if (absolute_path.isEmpty() || recent_files_.contains(absolute_path)) {
      continue;
    }
    recent_files_.push_back(absolute_path);
    if (recent_files_.size() >= kMaxRecentFiles) {
      break;
    }
  }
}

void MainWindow::saveRecentFiles() const {
  QSettings settings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
  settings.setValue(QString::fromLatin1(kRecentFilesKey), recent_files_);
}

void MainWindow::rebuildRecentFilesMenu() {
  if (open_recent_menu_ == nullptr) {
    return;
  }

  open_recent_menu_->clear();
  if (recent_files_.isEmpty()) {
    QAction* empty = open_recent_menu_->addAction(trKey("label.no_recent"));
    empty->setEnabled(false);
  } else {
    for (int i = 0; i < recent_files_.size(); ++i) {
      const QString path = recent_files_.at(i);
      const QString label = QStringLiteral("&%1 %2").arg(i + 1).arg(QDir::toNativeSeparators(path));
      QAction* action = open_recent_menu_->addAction(label);
      connect(action, &QAction::triggered, this, [this, path]() {
        (void)openFileInNewTab(path);
      });
    }
  }

  open_recent_menu_->addSeparator();
  QAction* clear_action = open_recent_menu_->addAction(trKey("action.clear_recent"));
  clear_action->setEnabled(!recent_files_.isEmpty());
  connect(clear_action, &QAction::triggered, this, &MainWindow::clearRecentFiles);
}

}  // namespace massiveedit::ui
