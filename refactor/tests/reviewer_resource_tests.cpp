#include "equipmentcontroller.h"
#include "equipmentwindow.h"
#include "sceneview.h"
#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

namespace {
#define REQUIRE(condition) do { if (!(condition)) { \
    QTextStream(stderr) << "RESOURCE_FAIL " << __LINE__ << ": " << #condition << Qt::endl; \
    return false; } } while (false)

void drain()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
QList<QPointer<QObject>> ownedObjects(QObject *owner)
{
    QList<QPointer<QObject>> result{QPointer<QObject>(owner)};
    for (auto *child : owner->findChildren<QObject *>()) result.append(QPointer<QObject>(child));
    return result;
}
bool allExpired(const QList<QPointer<QObject>> &objects)
{
    for (const auto &object : objects) if (object) return false;
    return true;
}

bool windows(const QString &database, int count, int loads, QString &renderer)
{
    for (int cycle = 0; cycle < count; ++cycle) {
        QList<QPointer<QObject>> objects;
        {
            EquipmentRepository repository;
            REQUIRE(repository.openDatabase(database) && repository.initialize());
            EquipmentController controller(repository);
            EquipmentWindow window(controller);
            for (int load = 0; load < loads; ++load) {
                REQUIRE(window.loadEquipment(QStringLiteral("EQ-001")));
                window.show();
                QCoreApplication::processEvents();
            }
            QTest::qWait(30);
            auto *scene = dynamic_cast<SceneView *>(window.findChild<QWidget *>("sceneView"));
            auto *parts = window.findChild<QListWidget *>("partsList");
            auto *hide = window.findChild<QPushButton *>("hideSelectedButton");
            auto *show = window.findChild<QPushButton *>("showAllButton");
            REQUIRE(scene && parts && parts->count() > 0 && hide && show);
            renderer = scene->rendererDescription();
            QTest::mouseClick(parts->viewport(), Qt::LeftButton, Qt::NoModifier,
                              parts->visualItemRect(parts->item(0)).center());
            REQUIRE(!controller.selectedPartId().isEmpty());
            QTest::mouseClick(hide, Qt::LeftButton);
            QTest::mouseClick(show, Qt::LeftButton);
            REQUIRE(controller.hiddenPartIds().isEmpty());
            // 捕获主窗与Scene的已知QObject子树，按父子所有权销毁后全部须失效。
            objects = ownedObjects(&window);
        }
        drain();
        REQUIRE(allExpired(objects));
    }
    return true;
}

QJsonObject counters()
{
    return {{"widgets", int(QApplication::allWidgets().size())},
            {"windows", int(QGuiApplication::allWindows().size())},
            {"applicationDescendants", int(QCoreApplication::instance()->findChildren<QObject *>().size())},
            {"sqlConnections", int(QSqlDatabase::connectionNames().size())}};
}
void report(const QString &phase, int batch, const QJsonObject &counts, const QString &renderer, int accepted = 0)
{
    auto row = counts;
    row.insert("phase", phase);
    row.insert("batch", batch);
    row.insert("pid", QCoreApplication::applicationPid());
    row.insert("utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    row.insert("renderer", renderer);
    row.insert("accepted", accepted);
    QTextStream(stdout) << QJsonDocument(row).toJson(QJsonDocument::Compact) << Qt::endl;
}
int option(const QStringList &args, const QString &name, int fallback, int maximum)
{
    const int position = args.indexOf(name);
    if (position < 0) return fallback;
    if (position + 1 >= args.size()) return -1;
    bool ok = false;
    const int value = args[position + 1].toInt(&ok);
    return ok && value >= 1 && value <= maximum ? value : -1;
}
bool run()
{
    const auto args = QCoreApplication::arguments();
    const int batches = option(args, "--batches", 5, 20);
    const int windowCount = option(args, "--windows", 20, 100);
    const int loadCount = option(args, "--loads", 5, 20);
    REQUIRE(batches > 0 && windowCount > 0 && loadCount > 0);
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString database = temporary.filePath(QStringLiteral("resources.sqlite"));
    const int originalSql = QSqlDatabase::connectionNames().size();
    QString renderer;
    REQUIRE(windows(database, 3, loadCount, renderer));
    // Scene有1200ms回退探针；采样前排空这类有界timer，避免把短暂排队当作持久增长。
    QTest::qWait(1400);
    drain();
    const auto baseline = counters();
    REQUIRE(baseline.value("sqlConnections").toInt() == originalSql);
    report("warm", 0, baseline, renderer);
    for (int batch = 1; batch <= batches; ++batch) {
        REQUIRE(windows(database, windowCount, loadCount, renderer));
        QTest::qWait(1400);
        drain();
        const auto current = counters();
        REQUIRE(current == baseline);
        report("batch", batch, current, renderer);
    }
    report("complete", batches, counters(), renderer, 1);
    return true;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    return run() ? 0 : 1;
}
