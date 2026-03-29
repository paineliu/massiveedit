#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <QString>

#include "massiveedit/core/document_session.h"

using massiveedit::core::DocumentSession;
using massiveedit::core::SearchOptions;

namespace {

std::filesystem::path writeTempFile(const std::string& prefix, const std::string& content) {
  const auto nonce =
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / (prefix + "_" + nonce + ".txt");

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  assert(out.is_open());
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  assert(out.good());
  return path;
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  assert(in.is_open());
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string snapshot(DocumentSession& session, const std::filesystem::path& output_path) {
  QString error;
  const bool saved = session.saveAs(QString::fromStdString(output_path.string()), &error);
  assert(saved);
  return readFile(output_path);
}

}  // namespace

int main() {
  {
    const std::string original = "aba aba\naba";
    const std::filesystem::path input_path = writeTempFile("massiveedit_replace_input", original);
    const std::filesystem::path output_path = writeTempFile("massiveedit_replace_output", "");

    DocumentSession session;
    QString error;
    assert(session.openFile(QString::fromStdString(input_path.string()), &error));

    SearchOptions options;
    options.case_sensitive = false;
    options.regex = false;

    std::vector<massiveedit::core::SearchMatch> matches =
        session.findAllMatches(QStringLiteral("aba"), options);
    assert(matches.size() == 3);
    assert(session.replaceRange(matches.front().offset, matches.front().length, QStringLiteral("X")));
    assert(snapshot(session, output_path) == "X aba\naba");

    assert(session.undo());
    assert(snapshot(session, output_path) == original);

    const std::size_t replaced = session.replaceAll(QStringLiteral("aba"), QStringLiteral("X"), options);
    assert(replaced == 3);
    assert(snapshot(session, output_path) == "X X\nX");

    assert(session.undo());
    assert(snapshot(session, output_path) == original);

    std::filesystem::remove(input_path);
    std::filesystem::remove(output_path);
  }

  {
    const std::filesystem::path input_path = writeTempFile("massiveedit_overlap_input", "aaaa");
    const std::filesystem::path output_path = writeTempFile("massiveedit_overlap_output", "");

    DocumentSession session;
    QString error;
    assert(session.openFile(QString::fromStdString(input_path.string()), &error));

    SearchOptions options;
    options.case_sensitive = true;
    options.regex = false;

    assert(session.replaceAll(QStringLiteral("aa"), QStringLiteral("b"), options) == 2);
    assert(snapshot(session, output_path) == "bb");
    assert(session.undo());
    assert(snapshot(session, output_path) == "aaaa");

    assert(session.replaceAll(QStringLiteral("aa"), QStringLiteral("b"), options, 1) == 1);
    assert(snapshot(session, output_path) == "baa");

    std::filesystem::remove(input_path);
    std::filesystem::remove(output_path);
  }

  {
    const std::filesystem::path input_path = writeTempFile("massiveedit_case_input", "Hello hELLo");
    const std::filesystem::path output_path = writeTempFile("massiveedit_case_output", "");

    DocumentSession session;
    QString error;
    assert(session.openFile(QString::fromStdString(input_path.string()), &error));

    SearchOptions options;
    options.case_sensitive = false;
    options.regex = false;

    assert(session.replaceAll(QStringLiteral("hello"), QStringLiteral("hi"), options) == 2);
    assert(snapshot(session, output_path) == "hi hi");

    std::filesystem::remove(input_path);
    std::filesystem::remove(output_path);
  }

  return 0;
}
