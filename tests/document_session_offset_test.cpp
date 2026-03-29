#include <cassert>
#include <cstddef>
#include <cstdint>

#include <QByteArray>
#include <QString>

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

  return 0;
}
