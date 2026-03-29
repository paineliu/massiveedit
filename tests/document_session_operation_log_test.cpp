#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <QString>

#include "massiveedit/core/document_session.h"

using massiveedit::core::DocumentSession;

namespace {

std::filesystem::path writeTempFile(const std::string& prefix, const std::string& content) {
  const auto nonce =
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / (prefix + "_" + nonce + ".tmp");

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  assert(out.is_open());
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  assert(out.good());
  return path;
}

std::string snapshotBytes(const DocumentSession& session) {
  const std::uint64_t size = session.byteSize();
  if (size == 0) {
    return {};
  }
  return session.bytesAt(0, static_cast<std::size_t>(size));
}

}  // namespace

int main() {
  {
    const std::filesystem::path input_path = writeTempFile("massiveedit_oplog_file_base", "one\ntwo\n");
    const std::filesystem::path log_path = writeTempFile("massiveedit_oplog_file", "");

    DocumentSession writer;
    QString error;
    assert(writer.openFile(QString::fromStdString(input_path.string()), &error));
    writer.insertText(0, QStringLiteral("A"));
    writer.insertText(1, QStringLiteral("B"));
    writer.removeText(3, 2);
    assert(writer.undo());
    assert(writer.redo());
    const std::string expected = snapshotBytes(writer);

    assert(writer.saveOperationLog(QString::fromStdString(log_path.string()), &error));

    DocumentSession reader;
    assert(reader.restoreFromOperationLog(QString::fromStdString(log_path.string()), &error));
    assert(snapshotBytes(reader) == expected);
    assert(reader.isDirty());

    std::filesystem::remove(input_path);
    std::filesystem::remove(log_path);
  }

  {
    const std::filesystem::path log_path = writeTempFile("massiveedit_oplog_bytes", "");

    DocumentSession writer;
    QString error;
    assert(writer.openFromBytes(QByteArray("alpha\nbeta\n"), QStringLiteral("bytes-base"), false));
    writer.removeText(1, 3);
    writer.insertText(1, QStringLiteral("LPH"));
    writer.insertText(writer.byteSize(), QStringLiteral("tail"));
    const std::string expected = snapshotBytes(writer);

    assert(writer.saveOperationLog(QString::fromStdString(log_path.string()), &error));

    DocumentSession reader;
    assert(reader.restoreFromOperationLog(QString::fromStdString(log_path.string()), &error));
    assert(snapshotBytes(reader) == expected);
    assert(reader.isDirty());

    std::filesystem::remove(log_path);
  }

  return 0;
}
