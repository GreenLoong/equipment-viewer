#include "mainwindow.h"
#include "recordstore.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPixmap>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qt_sqlite_demo"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt + SQLite 本地记录示例"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("database"), QStringLiteral("数据库文件路径"),
                      QStringLiteral("path")});
    parser.addOption({QStringLiteral("smoke-test"),
                      QStringLiteral("使用临时数据库显示窗口，保存截图后自动退出"),
                      QStringLiteral("png")});
    parser.process(app);

    const bool smokeTest = parser.isSet(QStringLiteral("smoke-test"));
    // 临时数据仅用于启动验证，不写入用户的记录数据库。
    QTemporaryDir temporaryDirectory;
    if (smokeTest && !temporaryDirectory.isValid())
        return 1;
    const QString databasePath = smokeTest
        ? temporaryDirectory.filePath(QStringLiteral("smoke.sqlite"))
        : parser.isSet(QStringLiteral("database"))
            ? parser.value(QStringLiteral("database"))
            : QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/records.sqlite"));

    RecordStore store;
    auto fail = [&](const QString &message) {
        QTextStream(stderr) << message << Qt::endl;
        if (!smokeTest)
            QMessageBox::critical(nullptr, QStringLiteral("启动失败"), message);
        return 1;
    };
    if (!store.open(databasePath))
        return fail(QStringLiteral("无法打开数据库：%1").arg(store.lastError()));
    if (smokeTest && !store.add(QStringLiteral("环境验证：Qt 窗口与 SQLite 读写已连接")))
        return fail(store.lastError());
    MainWindow window(store);
    if (!window.initialize())
        return fail(QStringLiteral("无法初始化记录列表：%1").arg(store.lastError()));
    window.show();

    if (smokeTest) {
        const QString screenshotPath = QFileInfo(parser.value(QStringLiteral("smoke-test"))).absoluteFilePath();
        QTimer::singleShot(1200, &window, [&, screenshotPath] {
            if (!window.isVisible() || !window.grab().save(screenshotPath)) {
                QTextStream(stderr) << "SMOKE FAIL: cannot save window image" << Qt::endl;
                app.exit(1);
                return;
            }
            QTextStream(stdout) << "SMOKE PASS: " << screenshotPath << Qt::endl;
            app.exit(0);
        });
    }
    return app.exec();
}
