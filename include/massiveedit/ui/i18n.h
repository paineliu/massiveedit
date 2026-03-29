#pragma once

#include <QString>

#include <vector>

namespace massiveedit::ui::i18n {

enum class Language {
  kEnglish,
  kSimplifiedChinese,
  kTraditionalChinese,
  kJapanese,
  kKorean,
  kFrench,
};

struct LanguageItem {
  Language language;
  QString code;
  QString display_name;
};

Language currentLanguage();
void setCurrentLanguage(Language language);
Language languageFromCode(const QString& code);
QString languageCode(Language language);
QString languageDisplayName(Language language);
const std::vector<LanguageItem>& supportedLanguages();

QString tr(const QString& key);

}  // namespace massiveedit::ui::i18n
