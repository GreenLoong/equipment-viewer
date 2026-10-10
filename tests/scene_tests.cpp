#include "sceneview.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QMouseEvent>
#include <QTest>
#include <QTextStream>
#include <QWheelEvent>
#include <limits>
#include <memory>

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            QTextStream(stderr) << "FAIL at line " << __LINE__ << ": " << #condition << Qt::endl; \
            return 1; \
        } \
    } while (false)

Part box(const QString &id)
{
    Part part;
    part.id = id;
    part.name = QStringLiteral("同名测试零件");
    part.shape = QStringLiteral("box");
    part.sizeX = 100;
    part.sizeY = 80;
    part.sizeZ = 60;
    return part;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    SceneView view;
    view.resize(640, 480);
    view.show();
    QTest::qWait(1500);
    auto *canvas = view.findChild<QWidget *>(QStringLiteral("sceneCanvas"));
    REQUIRE(canvas);
    int picks = 0;
    QString picked;
    view.onPartPicked = [&](const QString &id) { picked = id; ++picks; };
    view.setParts({box(QStringLiteral("P-001"))});
    app.processEvents();
    const QImage normal = view.grab().toImage();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    REQUIRE(picked == QStringLiteral("P-001"));
    view.setSelectedPart(picked);
    app.processEvents();
    REQUIRE(view.grab().toImage() != normal);
    view.setHiddenParts({QStringLiteral("P-001")});
    app.processEvents();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    REQUIRE(picked == QStringLiteral("P-001"));
    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    REQUIRE(picked.isEmpty());
    const QImage hidden = view.grab().toImage();
    REQUIRE(hidden != normal);
    view.setHiddenParts({});
    view.setSelectedPart({});
    app.processEvents();
    REQUIRE(view.grab().toImage() == normal);

    // 制造完整拖动后回到原点；仍不允许将释放当作短点击。
    const int beforeDrag = picks;
    const QPointF start = canvas->rect().center();
    QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, start + QPointF(70, 35), start + QPointF(70, 35),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent back(QEvent::MouseMove, start, start, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, start, start, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    app.sendEvent(canvas, &press);
    app.sendEvent(canvas, &move);
    app.processEvents();
    REQUIRE(view.grab().toImage() != normal);
    app.sendEvent(canvas, &back);
    app.sendEvent(canvas, &release);
    REQUIRE(picks == beforeDrag);
    view.fitAll();
    app.processEvents();
    REQUIRE(view.grab().toImage() == normal);

    QWheelEvent wheel(start, start, QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    app.sendEvent(canvas, &wheel);
    app.processEvents();
    REQUIRE(view.grab().toImage() != normal);
    view.fitAll();

    Part cylinder;
    cylinder.id = QStringLiteral("C-001");
    cylinder.shape = QStringLiteral("cylinder");
    cylinder.radius = 20;
    cylinder.length = 100;
    view.setParts({cylinder});
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    REQUIRE(picked == cylinder.id);

    // 极端有限值必须安全归一化，不允许加减边界时溢出成inf。
    Part extreme = box(QStringLiteral("EXTREME"));
    extreme.positionX = std::numeric_limits<double>::max();
    extreme.sizeX = std::numeric_limits<double>::max();
    extreme.sizeY = std::numeric_limits<double>::max();
    extreme.sizeZ = std::numeric_limits<double>::max();
    view.setParts({extreme});
    REQUIRE(!view.rendererDescription().contains(QStringLiteral("场景错误")));
    app.processEvents();
    REQUIRE(!view.grab().isNull());
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    REQUIRE(picked == extreme.id);

    Part invalid = box(QStringLiteral("INVALID"));
    invalid.sizeX = -1;
    view.setParts({invalid});
    REQUIRE(view.rendererDescription().contains(QStringLiteral("INVALID")));
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    REQUIRE(picked.isEmpty());
    view.setParts({});
    REQUIRE(!view.rendererDescription().contains(QStringLiteral("场景错误")));
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    REQUIRE(picked.isEmpty());

    view.setParts({box(QStringLiteral("SNAPSHOT")), cylinder});
    view.setSelectedPart(cylinder.id);
    app.processEvents();
    const QStringList args = app.arguments();
    const int snapshotIndex = args.indexOf(QStringLiteral("--snapshot-dir"));
    if (snapshotIndex >= 0) {
        REQUIRE(snapshotIndex + 1 < args.size());
        const QDir destination(args[snapshotIndex + 1]);
        REQUIRE(QDir().mkpath(destination.absolutePath()));
        REQUIRE(view.grab().save(destination.filePath(QStringLiteral("scene.png"))));
    }
    QTextStream(stdout) << view.rendererDescription() << Qt::endl;
    // 让场景成为顶层窗口的子控件并实际绘制，再按父子所有权销毁；
    // 覆盖集成窗口的关闭路径，而不仅是在栈上销毁单独顶层SceneView。
    for (int cycle = 0; cycle < 3; ++cycle) {
        auto host = std::make_unique<QWidget>();
        host->resize(480, 360);
        auto *child = new SceneView(host.get());
        child->setGeometry(host->rect());
        child->setParts({box(QStringLiteral("LIFECYCLE"))});
        host->show();
        QTest::qWait(100);
        REQUIRE(!host->grab().isNull());
        host.reset();
        app.processEvents();
    }
    QTextStream(stdout) << "Scene checks passed: picking, highlight, hide/restore, drag threshold, zoom, shapes, extreme range, invalid/empty." << Qt::endl;
    return 0;
}
