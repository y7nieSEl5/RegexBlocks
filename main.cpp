#include <QApplication>
#include "ui/mainwindow.h"
#include "ui/theme.h"
#include "util/appicon.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("RegexBlocks");
    app.setApplicationDisplayName("RegexBlocks");
    app.setOrganizationName("CourseProject");
    app.setOrganizationDomain("course.local");
    app.setApplicationVersion("1.0.0");
    app.setWindowIcon(ui::makeAppIcon());

    // 在 MainWindow 构造前应用主题, 避免启动时先闪一下亮色再切暗色.
    Theme::instance().applySaved();

    MainWindow window;
    window.show();

    return app.exec();
}
