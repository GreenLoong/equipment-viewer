#include "mainwindow.h"
#include "recordstore.h"
#include "equipmentrepository.h"
#include "equipmentcontroller.h"
#include "equipmentwindow.h"

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
    QCoreApplication::setApplicationVersion(QStringLiteral("0.4.0"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt + SQLite 本地记录示例"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("database"), QStringLiteral("数据库文件路径"),
                      QStringLiteral("path")});
    parser.addOption({QStringLiteral("smoke-test"),
                      QStringLiteral("使用临时数据库显示窗口，保存截图后自动退出"),
                      QStringLiteral("png")});
    parser.addOption({QStringLiteral("equipment-check"),
                      QStringLiteral("初始化并输出设备及零件资料，随后退出，不打开窗口")});
    parser.addOption({QStringLiteral("equipment-view"), QStringLiteral("打开 EQ-001 设备查看窗口")});
    parser.process(app);

    const bool smokeTest = parser.isSet(QStringLiteral("smoke-test"));
    const bool equipmentCheck = parser.isSet(QStringLiteral("equipment-check"));
    const bool equipmentView = parser.isSet(QStringLiteral("equipment-view"));
    if (equipmentCheck && (smokeTest || equipmentView)) {
        QTextStream(stderr) << "--equipment-check 不能与 --smoke-test 或 --equipment-view 同时使用。" << Qt::endl;
        return 1;
    }
    // 临时数据仅用于启动验证，不写入用户的记录数据库。
    QTemporaryDir temporaryDirectory;
    if (smokeTest && !temporaryDirectory.isValid())
        return 1;
    const QString databasePath = smokeTest
        ? temporaryDirectory.filePath(QStringLiteral("smoke.sqlite"))
        : parser.isSet(QStringLiteral("database"))
            ? parser.value(QStringLiteral("database"))
            : QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/records.sqlite"));

    auto fail = [&](const QString &message) {
        QTextStream(stderr) << message << Qt::endl;
        if (!smokeTest && !equipmentCheck)
            QMessageBox::critical(nullptr, QStringLiteral("启动失败"), message);
        return 1;
    };
    EquipmentRepository repository;
    if (!repository.openDatabase(databasePath))
        return fail(repository.lastError());
    QTextStream(stdout) << "DATABASE: " << repository.databasePath() << Qt::endl;
    if (!repository.initialize())
        return fail(repository.lastError());

    auto runWindow = [&](QWidget &window) {
        window.show();
        if (smokeTest) {
            const QString screenshotPath = QFileInfo(parser.value(QStringLiteral("smoke-test"))).absoluteFilePath();
            QTimer::singleShot(1200, &window, [&app, &window, screenshotPath] {
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
    };
    if (equipmentView) {
        EquipmentController controller(repository);
        EquipmentWindow window(controller);
        if (!window.loadEquipment(QStringLiteral("EQ-001")))
            return fail(controller.lastError());
        return runWindow(window);
    }

    QList<Equipment> equipment;
    if (!repository.loadEquipment(equipment))
        return fail(repository.lastError());
    qsizetype partCount = 0;
    for (const Equipment &item : equipment) {
        QList<Part> parts;
        if (!repository.loadParts(item.id, parts))
            return fail(repository.lastError());
        QTextStream(stdout) << "EQUIPMENT: " << item.id << " " << item.name << Qt::endl;
        for (const Part &part : parts)
            QTextStream(stdout) << "PART: " << part.equipmentId << "/" << part.id
                                << " " << part.name << Qt::endl;
        partCount += parts.size();
    }
    QTextStream(stdout) << "COUNTS: equipment=" << equipment.size() << " parts=" << partCount << Qt::endl;
    if (equipmentCheck)
        return 0;

    RecordStore store;
    if (!store.open(databasePath))
        return fail(QStringLiteral("无法打开数据库：%1").arg(store.lastError()));
    if (smokeTest && !store.add(QStringLiteral("环境验证：Qt 窗口与 SQLite 读写已连接")))
        return fail(store.lastError());
    MainWindow window(store);
    if (!window.initialize())
        return fail(QStringLiteral("无法初始化记录列表：%1").arg(store.lastError()));
    return runWindow(window);
}
