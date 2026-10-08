#include "cadimportprocess.h"
#include "equipmentcontroller.h"
#include "equipmentwindow.h"
#include "collaborationdialog.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QCryptographicHash>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

#define CHECK(x) do { if (!(x)) { QTextStream(stderr) << "FAIL " << __LINE__ << ": " << #x << Qt::endl; return false; } } while (false)

namespace {
bool until(const std::function<bool()> &condition, int timeout = 5000)
{
    QElapsedTimer clock; clock.start();
    while (!condition() && clock.elapsed() < timeout) QTest::qWait(10);
    return condition();
}
bool write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray protocol()
{
    return QJsonDocument(QJsonObject{{"protocol", "equipment-cad-mesh"}, {"version", 1}, {"unit", "mm"},
        {"sourceName", "motor.step"}, {"sourceSha256", QString(64, 'a')},
        {"parts", QJsonArray{QJsonObject{{"id", "solid1"}, {"name", "曲面"}, {"material", "未提供"},
            {"shape", "mesh"}, {"dimensions", QJsonArray{2,2,2}}, {"position", QJsonArray{4,5,6}},
            {"mesh", QJsonObject{{"vertices", QJsonArray{QJsonArray{-1,-1,-1}, QJsonArray{1,-1,1}, QJsonArray{0,1,0}}},
                                {"triangles", QJsonArray{QJsonArray{0,1,2}}}}}}}}}).toJson(QJsonDocument::Compact);
}
QByteArray oldEquipment()
{
    return R"({"equipmentId":"old","equipmentName":"old","parts":[{"id":"p","name":"old part","material":"steel","shape":"box","dimensions":[2,3,4],"position":[0,0,0]}]})";
}
struct Fixture {
    QTemporaryDir temp;
    QString input = temp.filePath("source.step");
    QString script = temp.filePath("converter.py");
    CadImportProcess::Config config;
    bool initialize(const QByteArray &body, int timeout = 3000) {
        config = CadImportProcess::Config::runtime();
        // 测试只注入明确绝对路径，永不从PATH或模型目录发现程序。
        config.modules = temp.path(); config.converter = script; config.timeoutMs = timeout;
        return temp.isValid() && write(input, "fake controlled STEP input") && write(script,
            "import argparse, pathlib, sys, time, subprocess, json\n"
            "p=argparse.ArgumentParser()\np.add_argument('--input');p.add_argument('--output');p.add_argument('--modules')\n"
            "a=p.parse_args()\n" + body);
    }
    QByteArray resultCode() const {
        return "pathlib.Path(a.output).write_bytes(" + QByteArray("bytes.fromhex('") + protocol().toHex() + "'))\n";
    }
};

bool successAndRepository()
{
    Fixture fixture;
    CHECK(fixture.initialize("time.sleep(0.15)\n" + fixture.resultCode()));
    EquipmentRepository repository;
    CHECK(repository.openDatabase(fixture.temp.filePath("db.sqlite")) && repository.initialize());
    EquipmentController controller(repository);
    CHECK(controller.importJson(oldEquipment()));
    CadImportProcess process(fixture.config);
    bool finished = false; QByteArray bytes; QString error;
    CHECK(process.start(fixture.input, [&](QByteArray result, QString message) {
        bytes = std::move(result); error = message; finished = true;
    }));
    CHECK(!process.start(fixture.input, {}));
    CHECK(controller.equipment()->id == "old");
    CHECK(until([&] { return finished; }));
    CHECK(error.isEmpty() && bytes == protocol() && !process.isBusy() && process.processId() == 0);
    CHECK(controller.importCadResult(bytes));
    CHECK(controller.parts().size() == 1 && controller.parts().first().mesh
          && controller.parts().first().shape == "mesh");
    CHECK(controller.loadEquipment("old"));
    CHECK(!controller.importCadResult(bytes)); // 重复CAD SHA不覆盖且仍显示旧设备。
    CHECK(controller.equipment()->id == "old");
    auto bad = QJsonDocument::fromJson(bytes).object(); bad["protocol"] = "wrong";
    CHECK(!controller.importCadResult(QJsonDocument(bad).toJson()));
    CHECK(controller.equipment()->id == "old");
    // 空库初始化已有EQ-001样例，加本测试old与CAD共三台。
    QList<Equipment> saved; CHECK(repository.loadEquipment(saved) && saved.size() == 3);
    CHECK(controller.loadEquipment("CAD-" + QString(64, 'a')) && controller.parts().first().mesh);
    return true;
}

bool failureCases()
{
    Fixture fixture;
    CHECK(fixture.initialize({}));
    for (const QByteArray &body : {
            fixture.resultCode() + "sys.exit(7)\n", QByteArray("sys.exit(0)\n"),
            QByteArray("f=open(a.output,'wb');f.seek(64*1024*1024);f.write(b'x');f.close()\n"),
            QByteArray("sys.stdout.buffer.write(b'x'*(256*1024));sys.stdout.flush();time.sleep(1)\n"),
            QByteArray("sys.stderr.buffer.write(b'x'*(256*1024));sys.stderr.flush();time.sleep(1)\n"),
            QByteArray("sys.stdout.buffer.write(b'x'*(70*1024));sys.stdout.flush();sys.stderr.buffer.write(b'x'*(70*1024));sys.stderr.flush();time.sleep(1)\n")}) {
        CHECK(fixture.initialize(body));
        CadImportProcess process(fixture.config);
        bool finished = false; QByteArray bytes; QString error;
        CHECK(process.start(fixture.input, [&](QByteArray result, QString message) {
            bytes = std::move(result); error = message; finished = true;
        }));
        CHECK(until([&] { return finished; }));
        CHECK(bytes.isEmpty() && !error.isEmpty() && !process.isBusy() && process.processId() == 0);
    }
    // 稀疏输入制造100MiB以上，程序不得启动。
    QFile input(fixture.input); CHECK(input.open(QIODevice::ReadWrite));
    CHECK(input.resize(100 * 1024 * 1024 + 1)); input.close();
    CadImportProcess process(fixture.config); CHECK(!process.start(fixture.input, {}));
    CHECK(!process.isBusy() && process.processId() == 0);
    CHECK(fixture.initialize({}));
    fixture.config.python = "python.exe";
    CadImportProcess relative(fixture.config); CHECK(!relative.start(fixture.input, {}));
    fixture.config.python = fixture.temp.filePath("not-a-program.exe");
    CHECK(write(fixture.config.python, "not an executable"));
    CadImportProcess invalid(fixture.config); bool failed = false;
    CHECK(invalid.start(fixture.input, [&](QByteArray, QString error) { failed = !error.isEmpty(); }));
    CHECK(until([&] { return failed; }));
    CHECK(!invalid.isBusy());
    return true;
}

bool cancelTimeoutAndOwner()
{
    Fixture fixture; CHECK(fixture.initialize("time.sleep(10)\n" + fixture.resultCode(), 100));
    CadImportProcess timeout(fixture.config);
    bool done = false; QString failure;
    CHECK(timeout.start(fixture.input, [&](QByteArray bytes, QString error) {
        done = true; failure = error; if (!bytes.isEmpty()) failure.clear();
    }));
    CHECK(until([&] { return done; }));
    CHECK(!failure.isEmpty() && !timeout.isBusy() && timeout.processId() == 0);
    CHECK(fixture.initialize("time.sleep(10)\n" + fixture.resultCode()));
    for (bool destroy : {false, true}) {
        auto process = std::make_unique<CadImportProcess>(fixture.config);
        bool callback = false; QByteArray bytes;
        CHECK(process->start(fixture.input, [&](QByteArray result, QString) { callback = true; bytes = result; }));
        CHECK(until([&] { return process->processId() > 0; }));
#ifdef Q_OS_WIN
        CHECK(until([&] { return process->isConfined(); }));
        HANDLE owned = OpenProcess(SYNCHRONIZE, FALSE, DWORD(process->processId()));
        CHECK(owned != nullptr);
#endif
        if (destroy) process.reset();
        else CHECK(process->cancel());
#ifdef Q_OS_WIN
        const DWORD result = WaitForSingleObject(owned, 3000);
        CloseHandle(owned); CHECK(result == WAIT_OBJECT_0);
#endif
        QTest::qWait(200);
        CHECK(bytes.isEmpty() && callback == !destroy);
    }
    // 完成通知已脱离QProcess信号栈，可直接销毁拥有者或开始下一次导入。
    CHECK(fixture.initialize(fixture.resultCode()));
    bool deleted = false;
    auto selfDeleting = std::make_unique<CadImportProcess>(fixture.config);
    CHECK(selfDeleting->start(fixture.input, [&](QByteArray bytes, QString error) {
        deleted = !bytes.isEmpty() && error.isEmpty(); selfDeleting.reset();
    }));
    CHECK(until([&] { return deleted; })); CHECK(!selfDeleting);
    CadImportProcess reusable(fixture.config); int completed = 0;
    CHECK(reusable.start(fixture.input, [&](QByteArray, QString) {
        ++completed;
        if (!reusable.start(fixture.input, [&](QByteArray bytes, QString error) {
            if (!bytes.isEmpty() && error.isEmpty()) ++completed;
        })) completed = -1;
    }));
    CHECK(until([&] { return completed >= 2 || completed < 0; })); CHECK(completed == 2);
#ifdef Q_OS_WIN
    // worker创建受控孙进程，取消必须终止整个Job而不操作其他Python。
    CHECK(fixture.initialize("child=subprocess.Popen([sys.executable,'-I','-c','import time;time.sleep(10)'])\n"
        "pathlib.Path(a.input+'.pid').write_text(str(child.pid))\ntime.sleep(10)\n"));
    CadImportProcess parent(fixture.config); CHECK(parent.start(fixture.input, {}));
    const QString pidFile = fixture.input + ".pid";
    CHECK(until([&] { return QFileInfo::exists(pidFile); }));
    QFile pid(pidFile); CHECK(pid.open(QIODevice::ReadOnly));
    HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, pid.readAll().toUInt());
    CHECK(child != nullptr);
    CHECK(parent.cancel());
    const DWORD result = WaitForSingleObject(child, 3000);
    CloseHandle(child); CHECK(result == WAIT_OBJECT_0);
#endif
    return true;
}

bool windowFlow()
{
    Fixture fixture; CHECK(fixture.initialize("time.sleep(0.3)\n" + fixture.resultCode()));
    qputenv("EQUIPMENT_CAD_CONVERTER", fixture.script.toUtf8());
    qputenv("EQUIPMENT_CAD_MODULES", fixture.temp.path().toUtf8());
    EquipmentRepository repository;
    CHECK(repository.openDatabase(fixture.temp.filePath("window.sqlite")) && repository.initialize());
    EquipmentController controller(repository); CHECK(controller.importJson(oldEquipment()));
    EquipmentWindow window(controller); CHECK(window.loadEquipment("old"));
    CHECK(window.importFile(fixture.input));
    CHECK(window.cadImportInProgress() && !window.importData(oldEquipment()));
    CHECK(!window.findChild<QPushButton *>("importButton")->isEnabled());
    CHECK(window.findChild<QPushButton *>("cadCancelButton")->isEnabled());
    CHECK(controller.selectPart("p") && controller.hideSelected());
    CHECK(controller.equipment()->id == "old"); // 转换等待期间业务交互仍可执行。
    CHECK(until([&] { return !window.cadImportInProgress(); }));
    CHECK(controller.equipment()->id == "CAD-" + QString(64, 'a'));
    auto *list = window.findChild<QListWidget *>("partsList"); list->setCurrentRow(0);
    CHECK(window.findChild<QLabel *>("shape")->text() == QStringLiteral("CAD曲面网格"));
    CHECK(window.findChild<QGroupBox *>("boxDimensions")->title().contains(QStringLiteral("外包络")));
    CHECK(window.findChild<QGroupBox *>("positionGroup")->title().contains(QStringLiteral("外包络中心")));
    CHECK(window.findChild<QLabel *>("cadSource")->text().contains("motor.step"));
    CollaborationDialog dialog(controller);
    dialog.findChild<QPushButton *>("sharedPublishButton")->click();
    CHECK(dialog.findChild<QLabel *>("sharedStatus")->text().contains(QStringLiteral("暂不支持CAD发布")));
    CHECK(window.loadEquipment("old"));
    auto wrong = QJsonDocument::fromJson(protocol()).object(); wrong["protocol"] = "wrong";
    CHECK(fixture.initialize("pathlib.Path(a.output).write_bytes(bytes.fromhex('"
        + QJsonDocument(wrong).toJson().toHex() + "'))\n"));
    CHECK(window.importFile(fixture.input));
    CHECK(until([&] { return !window.cadImportInProgress(); }));
    CHECK(controller.equipment()->id == "old");
    CHECK(fixture.initialize("time.sleep(10)\n" + fixture.resultCode()));
    CHECK(window.importFile(fixture.input)); CHECK(window.cancelCadImport());
    QTest::qWait(100); CHECK(controller.equipment()->id == "old");
    auto owner = std::make_unique<EquipmentWindow>(controller);
    CHECK(owner->importFile(fixture.input)); owner.reset();
    QTest::qWait(100); CHECK(controller.equipment()->id == "old");
    auto closing = std::make_unique<EquipmentWindow>(controller);
    CHECK(closing->importFile(fixture.input)); QTest::qWait(50);
    CHECK(closing->close() && !closing->cadImportInProgress());
    QTest::qWait(100); CHECK(controller.equipment()->id == "old");
    QList<Equipment> saved; CHECK(repository.loadEquipment(saved) && saved.size() == 3);
    return true;
}

bool actualConversion(const QString &input, int count, const CadImportProcess::Config &config)
{
    QTemporaryDir temp; CHECK(temp.isValid());
    QByteArray bytes; QString error; bool done = false;
    CadImportProcess process(config);
    CHECK(process.start(input, [&](QByteArray result, QString message) {
        bytes = std::move(result); error = message; done = true;
    }));
    CHECK(until([&] { return done; }, 120000));
    if (!error.isEmpty()) QTextStream(stderr) << error << Qt::endl;
    CHECK(error.isEmpty() && !bytes.isEmpty());
    QFile source(input); CHECK(source.open(QIODevice::ReadOnly));
    QCryptographicHash digest(QCryptographicHash::Sha256); CHECK(digest.addData(&source)); source.close();
    const QString id = "CAD-" + QString::fromLatin1(digest.result().toHex());
    const QString database = temp.filePath("actual.sqlite");
    {
        EquipmentRepository repository;
        CHECK(repository.openDatabase(database) && repository.initialize());
        EquipmentController controller(repository); CHECK(controller.importJson(oldEquipment()));
        CHECK(controller.importCadResult(bytes));
        CHECK(controller.equipment()->id == id && controller.equipment()->name == QFileInfo(input).fileName());
        CHECK(controller.parts().size() == count);
        quint64 vertices = 0, triangles = 0;
        for (const Part &part : controller.parts()) {
            CHECK(part.shape == "mesh" && part.mesh && part.cadSourceName == QFileInfo(input).fileName());
            vertices += part.mesh->vertices.size(); triangles += part.mesh->triangles.size();
        }
        CHECK(vertices > 12 && triangles > 12);
        QTextStream(stdout) << "Actual OCP " << QFileInfo(input).fileName() << " solids=" << count
                            << " vertices=" << vertices << " triangles=" << triangles << Qt::endl;
    }
    EquipmentRepository reopened; CHECK(reopened.openDatabase(database) && reopened.initialize());
    EquipmentController restarted(reopened); CHECK(restarted.loadEquipment(id));
    CHECK(restarted.parts().size() == count && restarted.parts().first().mesh);
    return true;
}
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    const CadImportProcess::Config actualConfig = CadImportProcess::Config::runtime();
    if (!QFileInfo(actualConfig.python).isFile()) {
        QTextStream(stderr) << "Set EQUIPMENT_CAD_PYTHON to a trusted absolute Python runtime." << Qt::endl;
        return 2;
    }
    if (!successAndRepository() || !failureCases() || !cancelTimeoutAndOwner() || !windowFlow()) return 1;
    const QStringList arguments = application.arguments();
    for (const auto &option : {QStringLiteral("--actual-bearing"), QStringLiteral("--actual-motor")}) {
        const int index = arguments.indexOf(option);
        if (index >= 0 && (index + 1 >= arguments.size()
            || !actualConversion(arguments[index + 1], option == "--actual-bearing" ? 1 : 6, actualConfig))) return 1;
    }
    QTextStream(stdout) << "CAD process / atomic persistence / Windows owned Job / window workflow accepted=1" << Qt::endl;
    return 0;
}
