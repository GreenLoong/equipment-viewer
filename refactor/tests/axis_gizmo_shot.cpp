// 方向轴截图工具（仅重构树）：按视图模式保存画布图片，用于人工核对与文档配图。
// 用法：axis_gizmo_shot.exe -platform windows --snapshot-dir <目录> [--prefix p]
#include "sceneview.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QOpenGLWidget>
#include <QTest>
#include <QTextStream>
#include <QWidget>

#include <array>

namespace {
QImage frame(QWidget *canvas)
{
    if (auto *gl = qobject_cast<QOpenGLWidget *>(canvas))
        return gl->grabFramebuffer();
    return canvas ? canvas->grab().toImage() : QImage();
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    const int directoryIndex = arguments.indexOf(QStringLiteral("--snapshot-dir"));
    if (directoryIndex < 0 || directoryIndex + 1 >= arguments.size())
        return 1;
    const QString directory = QDir(arguments[directoryIndex + 1]).absolutePath();
    if (!QDir().mkpath(directory))
        return 1;
    const int prefixIndex = arguments.indexOf(QStringLiteral("--prefix"));
    const QString prefix = prefixIndex >= 0 && prefixIndex + 1 < arguments.size()
        ? arguments[prefixIndex + 1] : QStringLiteral("shot");

    SceneView scene;
    scene.resize(900, 560);
    scene.setAxisPlacement(SceneView::AxisPlacement::Corner);
    Part part;
    part.id = QStringLiteral("GIZMO-P1");
    part.name = QStringLiteral("方向轴验收件");
    part.shape = QStringLiteral("box");
    part.sizeX = 200;
    part.sizeY = 120;
    part.sizeZ = 80;
    scene.setParts({part});
    scene.show();
    QTest::qWait(300);
    auto *canvas = scene.findChild<QWidget *>(QStringLiteral("sceneCanvas"));
    if (!canvas)
        return 1;

    const std::array<SceneView::ViewMode, 4> modes = {SceneView::ViewMode::Isometric,
        SceneView::ViewMode::Engineering, SceneView::ViewMode::Front, SceneView::ViewMode::Top};
    const QStringList names{QStringLiteral("isometric"), QStringLiteral("engineering"),
                            QStringLiteral("front"), QStringLiteral("top")};
    for (int index = 0; index < int(modes.size()); ++index) {
        scene.setViewMode(modes[index]);
        QTest::qWait(80);
        const QImage image = frame(canvas);
        if (image.isNull() || !image.save(QDir(directory).filePath(
                QStringLiteral("%1-%2.png").arg(prefix, names[index]))))
            return 1;
        const SceneView::AxisGizmo gizmo = scene.axisGizmo(true);
        QString line = QStringLiteral("SHOT ") + names[index];
        for (int axis = 0; axis < 3; ++axis)
            line += QStringLiteral(" %1=%2").arg(axis).arg(QLineF(gizmo.origin, gizmo.tips[axis]).length(), 0, 'f', 1);
        QTextStream(stdout) << line << Qt::endl;
    }
    // 三视图：每栏方向轴必须与本栏视线一致（正/俯/右视各一栏）。
    scene.setViewMode(SceneView::ViewMode::ThreeViews);
    QTest::qWait(80);
    const QImage image = frame(canvas);
    if (image.isNull() || !image.save(QDir(directory).filePath(prefix + QStringLiteral("-threeViews.png"))))
        return 1;
    QTextStream(stdout) << "SHOT threeViews saved" << Qt::endl;
    return 0;
}
