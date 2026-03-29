#include <cassert>
#include <cstdint>
#include <string>

#include <QApplication>
#include <QKeyEvent>
#include <QString>

#include "massiveedit/core/document_session.h"
#include "massiveedit/ui/large_file_view.h"

using massiveedit::core::DocumentSession;
using massiveedit::ui::LargeFileView;

namespace {

std::string snapshotBytes(const DocumentSession& session) {
  const std::uint64_t size = session.byteSize();
  if (size == 0) {
    return {};
  }
  return session.bytesAt(0, static_cast<std::size_t>(size));
}

void sendKey(QWidget* target, int key) {
  assert(target != nullptr);
  QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
  QApplication::sendEvent(target, &event);
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  QApplication app(argc, argv);

  {
    DocumentSession session;
    assert(session.openFromBytes(QByteArray("abc\ndef"), QStringLiteral("lf-join")));
    LargeFileView view;
    view.setSession(&session);
    assert(view.goToLineColumn(1, 0, false));

    sendKey(&view, Qt::Key_Backspace);

    assert(snapshotBytes(session) == "abcdef");
    assert(view.cursorLine() == 0);
    assert(view.cursorColumn() == 3);
  }

  {
    DocumentSession session;
    assert(session.openFromBytes(QByteArray("ab\ncd"), QStringLiteral("lf-backspace")));
    LargeFileView view;
    view.setSession(&session);
    assert(view.goToLineColumn(1, 1, false));

    sendKey(&view, Qt::Key_Backspace);

    assert(snapshotBytes(session) == "ab\nd");
    assert(view.cursorLine() == 1);
    assert(view.cursorColumn() == 0);
  }

  {
    DocumentSession session;
    assert(session.openFromBytes(QByteArray("a\nbc"), QStringLiteral("lf-delete-head")));
    LargeFileView view;
    view.setSession(&session);
    assert(view.goToLineColumn(1, 0, false));

    sendKey(&view, Qt::Key_Delete);

    assert(snapshotBytes(session) == "a\nc");
    assert(view.cursorLine() == 1);
    assert(view.cursorColumn() == 0);
  }

  {
    DocumentSession session;
    assert(session.openFromBytes(QByteArray("abcd"), QStringLiteral("lf-delete-mid")));
    LargeFileView view;
    view.setSession(&session);
    assert(view.goToLineColumn(0, 2, false));

    sendKey(&view, Qt::Key_Delete);

    assert(snapshotBytes(session) == "abd");
    assert(view.cursorLine() == 0);
    assert(view.cursorColumn() == 2);
  }

  {
    DocumentSession session;
    assert(session.openFromBytes(QByteArray("ab\r\ncd"), QStringLiteral("crlf-join")));
    LargeFileView view;
    view.setSession(&session);
    assert(view.goToLineColumn(1, 0, false));

    sendKey(&view, Qt::Key_Backspace);

    assert(snapshotBytes(session) == "abcd");
    assert(view.cursorLine() == 0);
    assert(view.cursorColumn() == 2);
  }

  {
    DocumentSession session;
    assert(session.openFromBytes(QByteArray("xy"), QStringLiteral("tab-insert")));
    LargeFileView view;
    view.setSession(&session);
    assert(view.goToLineColumn(0, 1, false));

    sendKey(&view, Qt::Key_Tab);

    assert(snapshotBytes(session) == "x\ty");
    assert(view.cursorLine() == 0);
    assert(view.cursorColumn() == 2);
  }

  return 0;
}
