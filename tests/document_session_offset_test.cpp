#include <cassert>
#include <cstddef>
#include <cstdint>

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include "massiveedit/core/document_session.h"

using massiveedit::core::DocumentSession;

int main() {
  DocumentSession session;
  const QByteArray content("abc\n", 4);
  assert(session.openFromBytes(content, QStringLiteral("offset-test")));

  std::size_t line = 0;
  std::size_t column = 0;

  assert(session.lineColumnForOffset(0, &line, &column));
  assert(line == 0);
  assert(column == 0);

  assert(session.lineColumnForOffset(3, &line, &column));
  assert(line == 0);
  assert(column == 3);

  assert(session.lineColumnForOffset(4, &line, &column));
  assert(line == 1);
  assert(column == 0);

  assert(session.lineColumnForOffset(1024, &line, &column));
  assert(line == 1);
  assert(column == 0);

  std::uint64_t eof_offset = 0;
  assert(session.offsetForLineColumn(1, 0, &eof_offset));
  assert(eof_offset == 4);

  // Empty lines are valid lines and must not be reported as null.
  assert(session.openFromBytes(QByteArray("a\n\nb\n", 5), QStringLiteral("empty-lines")));
  const QString empty_middle = session.lineAt(1);
  assert(!empty_middle.isNull());
  assert(empty_middle.isEmpty());

  const QString empty_last = session.lineAt(3);
  assert(!empty_last.isNull());
  assert(empty_last.isEmpty());

  const QString invalid = session.lineAt(4);
  assert(invalid.isNull());

  // Unicode file paths should open correctly on all platforms (including Windows).
  QTemporaryDir temp_dir;
  assert(temp_dir.isValid());
  const QString unicode_path = temp_dir.filePath(QStringLiteral("五道题网页.sketch"));
  {
    QFile out(unicode_path);
    assert(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    assert(out.write("hello\n", 6) == 6);
    out.close();
  }
  QString open_error;
  assert(session.openFile(unicode_path, &open_error));
  assert(session.lineAt(0) == QStringLiteral("hello"));

  return 0;
}
