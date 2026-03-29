#include <QApplication>
#include <QIcon>

#include "massiveedit/ui/main_window.h"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("MassiveEdit"));
  app.setOrganizationName(QStringLiteral("MassiveEdit"));

  QIcon app_icon;
  for (const int size : {16, 24, 32, 48, 64, 128, 256, 512, 1024}) {
    app_icon.addFile(QString(":/icons/app_icon_%1.png").arg(size));
  }
  if (!app_icon.isNull()) {
    app.setWindowIcon(app_icon);
  }

  massiveedit::ui::MainWindow main_window;
  if (!app_icon.isNull()) {
    main_window.setWindowIcon(app_icon);
  }
  main_window.show();
  return app.exec();
}
