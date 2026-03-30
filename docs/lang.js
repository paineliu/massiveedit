(function () {
  var key = "massiveedit_lang";
  var supported = {
    "index.html": true,
    "download.html": true,
    "changelog.html": true,
    "privacy.html": true,
  };

  var path = window.location.pathname;
  var fileMatch = path.match(/\/([^\/?#]+)$/);
  if (!fileMatch) return;

  var file = fileMatch[1];
  if (!supported[file]) return;

  var isEn = /\/en\/[^\/?#]+$/.test(path);
  var currentLang = isEn ? "en" : "zh";

  function toEn(p) {
    if (/\/en\/[^\/?#]+$/.test(p)) return p;
    return p.replace(/\/([^\/?#]+)$/, "/en/$1");
  }

  function toZh(p) {
    if (!/\/en\/[^\/?#]+$/.test(p)) return p;
    return p.replace(/\/en\/([^\/?#]+)$/, "/$1");
  }

  function targetPath(lang) {
    return lang === "en" ? toEn(path) : toZh(path);
  }

  var params = new URLSearchParams(window.location.search);
  var queryLang = params.get("lang");
  if (queryLang === "zh" || queryLang === "en") {
    try {
      localStorage.setItem(key, queryLang);
    } catch (e) {}
  }

  var savedLang = null;
  try {
    savedLang = localStorage.getItem(key);
  } catch (e) {}

  var browserLang = (navigator.language || "").toLowerCase().indexOf("zh") === 0 ? "zh" : "en";
  var hasExplicit = queryLang === "zh" || queryLang === "en" || savedLang === "zh" || savedLang === "en";

  if (hasExplicit) {
    var desiredLang = queryLang === "zh" || queryLang === "en" ? queryLang : savedLang;
    if (desiredLang !== currentLang) {
      var explicitDest = targetPath(desiredLang);
      if (explicitDest && explicitDest !== path) {
        window.location.replace(explicitDest + window.location.hash);
        return;
      }
    }
  } else {
    // First visit: default to English only when browser language is non-Chinese.
    if (browserLang === "en" && currentLang === "zh") {
      var autoDest = targetPath("en");
      if (autoDest && autoDest !== path) {
        window.location.replace(autoDest + window.location.hash);
        return;
      }
    }
  }

  document.addEventListener("DOMContentLoaded", function () {
    var links = document.querySelectorAll("[data-lang-switch]");
    links.forEach(function (link) {
      link.addEventListener("click", function () {
        var lang = link.getAttribute("data-lang-switch");
        if (lang === "zh" || lang === "en") {
          try {
            localStorage.setItem(key, lang);
          } catch (e) {}
        }
      });
    });
  });
})();
