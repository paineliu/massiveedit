#pragma once

#include <QAction>
#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "massiveedit/core/document_session.h"
#include "massiveedit/core/search_engine.h"
#include "massiveedit/ui/i18n.h"
#include "massiveedit/ui/large_file_view.h"

class QCheckBox;
class QComboBox;
class QCloseEvent;
class QDragEnterEvent;
class QDockWidget;
class QDropEvent;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QTabBar;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace massiveedit::ui {

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

 protected:
  void closeEvent(QCloseEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private slots:
  void newTab();
  void openFile();
  void closeCurrentTab();
  void saveFile();
  void saveAs();
  void goToLocation();
  void toggleBookmark();
  void nextBookmark();
  void previousBookmark();
  void undoEdit();
  void redoEdit();
  void cutSelection();
  void copySelection();
  void pasteClipboard();
  void findText();
  void findNext();
  void findPrevious();
  void replaceText();
  void replaceNext();
  void replaceAll();
  void handleSearchCompleted(qulonglong request_id, qulonglong match_count);
  void refreshWindowState();
  void handleTabChanged(int index);
  void handleTabCloseRequested(int index);
  void handleWatchedFileChanged(const QString& path);
  void autosaveCurrentTab();
  void updatePerformancePanel();
  void findInFiles();
  void showCommandPalette();
  void customizeShortcuts();
  void toggleReadOnlyMode();
  void setAppearanceFollowSystem();
  void setAppearanceLight();
  void setAppearanceDark();
  void showAboutDialog();
  void handleFindResultActivated(QTreeWidgetItem* item, int column);

 private:
  struct TabState {
    QString file_path;
    QString untitled_label;
    int untitled_id = 0;
    int recovered_id = 0;
    QString snapshot_path;
    QString operation_log_path;
    bool snapshot_dirty = false;
    core::DocumentSession::TextEncoding encoding = core::DocumentSession::TextEncoding::kUtf8;
    core::DocumentSession::LineEnding line_ending = core::DocumentSession::LineEnding::kLf;
    std::size_t cursor_line = 0;
    std::size_t cursor_column = 0;
    std::set<std::size_t> bookmarks;
    bool read_only = false;
  };

  struct SnapshotPayload {
    QString file_path;
    bool dirty = false;
    core::DocumentSession::TextEncoding encoding = core::DocumentSession::TextEncoding::kUtf8;
    core::DocumentSession::LineEnding line_ending = core::DocumentSession::LineEnding::kLf;
    std::size_t cursor_line = 0;
    std::size_t cursor_column = 0;
    std::set<std::size_t> bookmarks;
    QByteArray content;
  };

  [[nodiscard]] bool openFileInNewTab(const QString& path);
  [[nodiscard]] bool switchToTab(int index);
  [[nodiscard]] bool loadTabIntoSession(int index);
  [[nodiscard]] bool saveCurrentTabState();
  [[nodiscard]] bool ensureCanCloseAllTabs();
  [[nodiscard]] bool ensureCanAbandonCurrentDocument();
  [[nodiscard]] bool saveCurrentFile();
  [[nodiscard]] bool saveAsInteractive();
  [[nodiscard]] bool hasCurrentTab() const;
  [[nodiscard]] int currentTabIndex() const;
  [[nodiscard]] QString currentTabTitle() const;
  [[nodiscard]] QString untitledNameForIndex(int idx) const;
  [[nodiscard]] QString tabDisplayName(const TabState& tab) const;
  [[nodiscard]] QString recoveryDirPath() const;
  [[nodiscard]] QString snapshotPathForTab(int index) const;
  [[nodiscard]] QString operationLogPathForTab(int index) const;
  [[nodiscard]] bool writeSnapshotFile(const QString& path, const SnapshotPayload& payload) const;
  [[nodiscard]] bool readSnapshotFile(const QString& path, SnapshotPayload* payload) const;
  [[nodiscard]] bool removeSnapshotFile(const QString& path) const;
  void restoreRecoverySnapshotsIfAny();
  void clearAllRecoverySnapshots();
  void updateTabTitle(int index);
  void updateAllTabTitles();
  void updateFileWatcher();
  void addRecentFile(const QString& path);
  void clearRecentFiles();
  void loadRecentFiles();
  void saveRecentFiles() const;
  void rebuildRecentFilesMenu();
  void buildSearchPanel();
  void buildFindResultsPanel();
  void showSearchPanel(bool focus_replace);
  void runFindInFiles(const QString& query, const QString& root_dir, const core::SearchOptions& options);
  void clearFindResults();
  void applyShortcutOverrides();
  void saveSessionState() const;
  [[nodiscard]] bool restoreSessionState();
  void cleanupOrphanSnapshots() const;
  [[nodiscard]] QString buildExternalCompareSummary(const QString& path) const;
  [[nodiscard]] core::SearchOptions searchOptionsFromPanel() const;
  [[nodiscard]] std::vector<std::pair<QString, QAction*>> commandActions() const;
  [[nodiscard]] std::vector<std::tuple<QString, QString, QAction*>> shortcutBindings() const;
  void updateSearchPanelSummary();
  void syncFormatActions();
  void buildMenus();
  void buildPerformancePanel();
  void applyLanguage(i18n::Language language, bool persist);
  void applyAppearance(LargeFileView::AppearanceMode mode, bool persist);
  void retranslateSearchPanel();
  void updateStatusBar();
  void clearSearchState();
  void startSearchRequest(const QString& query, const core::SearchOptions& options);
  void navigateToMatch(std::size_t match_index, bool center_view);

  core::DocumentSession session_;
  std::vector<TabState> tabs_;
  int active_tab_index_ = -1;
  int next_untitled_index_ = 1;

  QWidget* search_panel_ = nullptr;
  QTabBar* tab_bar_ = nullptr;
  LargeFileView* view_ = nullptr;

  QLineEdit* find_input_ = nullptr;
  QLineEdit* replace_input_ = nullptr;
  QComboBox* replace_scope_combo_ = nullptr;
  QCheckBox* case_sensitive_checkbox_ = nullptr;
  QCheckBox* regex_checkbox_ = nullptr;
  QLabel* find_label_ = nullptr;
  QLabel* replace_label_ = nullptr;
  QLabel* replace_scope_label_ = nullptr;
  QPushButton* find_prev_button_ = nullptr;
  QPushButton* find_next_button_ = nullptr;
  QPushButton* replace_next_button_ = nullptr;
  QPushButton* replace_all_button_ = nullptr;
  QLabel* match_count_label_ = nullptr;

  QFileSystemWatcher* file_watcher_ = nullptr;
  QTimer* autosave_timer_ = nullptr;
  QTimer* perf_timer_ = nullptr;
  QTimer* refresh_timer_ = nullptr;
  QTimer* scroll_idle_timer_ = nullptr;
  QTimer* scroll_status_timer_ = nullptr;

  QDockWidget* perf_dock_ = nullptr;
  QLabel* perf_summary_label_ = nullptr;
  QDockWidget* find_results_dock_ = nullptr;
  QTreeWidget* find_results_tree_ = nullptr;

  QMenu* open_recent_menu_ = nullptr;
  QMenu* file_menu_ = nullptr;
  QMenu* edit_menu_ = nullptr;
  QMenu* format_menu_ = nullptr;
  QMenu* encoding_menu_ = nullptr;
  QMenu* line_ending_menu_ = nullptr;
  QMenu* tab_width_menu_ = nullptr;
  QMenu* search_menu_ = nullptr;
  QMenu* view_menu_ = nullptr;
  QMenu* appearance_menu_ = nullptr;
  QMenu* help_menu_ = nullptr;
  QMenu* language_menu_ = nullptr;

  QAction* new_tab_action_ = nullptr;
  QAction* open_action_ = nullptr;
  QAction* close_tab_action_ = nullptr;
  QAction* save_action_ = nullptr;
  QAction* save_as_action_ = nullptr;
  QAction* quit_action_ = nullptr;
  QAction* go_to_action_ = nullptr;
  QAction* toggle_bookmark_action_ = nullptr;
  QAction* next_bookmark_action_ = nullptr;
  QAction* previous_bookmark_action_ = nullptr;
  QAction* undo_action_ = nullptr;
  QAction* redo_action_ = nullptr;
  QAction* cut_action_ = nullptr;
  QAction* copy_action_ = nullptr;
  QAction* paste_action_ = nullptr;
  QAction* find_in_files_action_ = nullptr;
  QAction* command_palette_action_ = nullptr;
  QAction* shortcuts_action_ = nullptr;
  QAction* read_only_action_ = nullptr;
  QAction* appearance_follow_system_action_ = nullptr;
  QAction* appearance_light_action_ = nullptr;
  QAction* appearance_dark_action_ = nullptr;
  QAction* restore_session_on_startup_action_ = nullptr;
  QAction* about_action_ = nullptr;
  QAction* find_action_ = nullptr;
  QAction* find_next_action_ = nullptr;
  QAction* find_previous_action_ = nullptr;
  QAction* replace_action_ = nullptr;
  QAction* replace_next_action_ = nullptr;
  QAction* replace_all_action_ = nullptr;
  QAction* perf_panel_action_ = nullptr;
  QAction* encoding_utf8_action_ = nullptr;
  QAction* encoding_gbk_action_ = nullptr;
  QAction* line_ending_lf_action_ = nullptr;
  QAction* line_ending_crlf_action_ = nullptr;
  QAction* tab_width_2_action_ = nullptr;
  QAction* tab_width_4_action_ = nullptr;
  QAction* tab_width_8_action_ = nullptr;

  std::vector<core::SearchMatch> search_matches_;
  std::size_t active_match_index_ = 0;
  std::uint64_t pending_search_request_id_ = 0;
  std::size_t cursor_line_ = 0;
  std::size_t cursor_column_ = 0;
  QString last_find_query_;
  QString last_replace_text_;
  QStringList recent_files_;
  core::SearchOptions last_search_options_;
  bool suppress_file_change_prompt_ = false;
  bool view_scrolling_ = false;
  bool pending_refresh_while_scrolling_ = false;
  QString last_find_in_files_dir_;
  LargeFileView::AppearanceMode appearance_mode_ = LargeFileView::AppearanceMode::kFollowSystem;
  int tab_width_spaces_ = 4;
  bool restore_session_on_startup_ = false;
};

}  // namespace massiveedit::ui
