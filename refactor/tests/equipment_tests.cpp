#include "equipmentrepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUuid>
#include <QVariant>
#include <limits>

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            QTextStream(stderr) << "FAIL at line " << __LINE__ << ": " << #condition << Qt::endl; \
            return false; \
        } \
    } while (false)

// 只打开各用例的临时文件；这个连接用于检查约束和制造实际 SQL/提交错误。
struct Inspector {
    QString name = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    ~Inspector()
    {
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
    bool open(const QString &path)
    {
        database.setDatabaseName(path);
        if (!database.open())
            return false;
        return run(QStringLiteral("PRAGMA foreign_keys = ON"));
    }
    bool run(const QString &sql)
    {
        QSqlQuery query(database);
        return query.exec(sql);
    }
    int count(const QString &sql)
    {
        QSqlQuery query(database);
        if (!query.exec(sql) || !query.next())
            return -1;
        return query.value(0).toInt();
    }
};

bool persistenceAndFiltering()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("中文目录/equipment.sqlite"));
    {
        EquipmentRepository repository;
        REQUIRE(repository.openDatabase(path));
        REQUIRE(repository.databasePath() == QFileInfo(path).absoluteFilePath());
        REQUIRE(repository.initialize());
        QList<Equipment> equipment;
        QList<Part> parts;
        REQUIRE(repository.loadEquipment(equipment));
        REQUIRE(equipment.size() == 1);
        REQUIRE(equipment.first().id == QStringLiteral("EQ-001"));
        REQUIRE(equipment.first().name == QStringLiteral("输送机构"));
        REQUIRE(repository.loadParts(equipment.first().id, parts));
        REQUIRE(parts.size() == 3);
        REQUIRE(parts.at(0).name == QStringLiteral("底座"));
        REQUIRE(parts.at(1).name == QStringLiteral("驱动电机"));
        REQUIRE(parts.at(2).name == QStringLiteral("传动轴"));
        REQUIRE(parts.at(0).sizeX == 600 && !parts.at(0).radius);
        REQUIRE(parts.at(2).radius == 20 && !parts.at(2).sizeX);
        REQUIRE(repository.loadParts(QStringLiteral("EQ-001' OR 1=1 --"), parts));
        REQUIRE(parts.isEmpty());
        REQUIRE(!repository.loadParts(QStringLiteral(" \t　"), parts));
        REQUIRE(parts.isEmpty());
    }
    Inspector inspector;
    REQUIRE(inspector.open(path));
    // 人工改动已有名称后，重启不能覆盖它；demo_records 仍可共存。
    REQUIRE(inspector.run(QStringLiteral("UPDATE equipment SET name='已有资料 O''Hara' WHERE id='EQ-001'")));
    REQUIRE(inspector.run(QStringLiteral("CREATE TABLE demo_records(content TEXT)")));
    REQUIRE(inspector.run(QStringLiteral("INSERT INTO demo_records VALUES ('原有记录')")));
    for (int i = 0; i < 2; ++i) {
        EquipmentRepository repository;
        REQUIRE(repository.openDatabase(path));
        REQUIRE(repository.initialize());
        QList<Equipment> equipment;
        QList<Part> parts;
        REQUIRE(repository.loadEquipment(equipment));
        REQUIRE(equipment.size() == 1);
        REQUIRE(equipment.first().name == QStringLiteral("已有资料 O'Hara"));
        REQUIRE(repository.loadParts(QStringLiteral("EQ-001"), parts));
        REQUIRE(parts.size() == 3);
    }
    REQUIRE(inspector.count(QStringLiteral("SELECT count(*) FROM demo_records")) == 1);
    return true;
}

bool constraintsAndValidation()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("constraints.sqlite"));
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(path));
    REQUIRE(repository.initialize());
    Inspector inspector;
    REQUIRE(inspector.open(path));
    REQUIRE(!inspector.run(QStringLiteral("INSERT INTO equipment VALUES('EQ-001','重复主键')")));
    REQUIRE(!inspector.run(QStringLiteral("INSERT INTO equipment VALUES(char(9,12288),'空白 ID')")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET id='P-001' WHERE id='P-002'")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET equipment_id='不存在' WHERE id='P-001'")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET name=char(9,10,12288) WHERE id='P-001'")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET material=NULL WHERE id='P-001'")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET size_x=0 WHERE id='P-001'")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET size_x=-1 WHERE id='P-001'")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET size_x=NULL WHERE id='P-001'")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET radius=1 WHERE id='P-001'")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET radius=NULL WHERE id='P-003'")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET length=0 WHERE id='P-003'")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET size_x=1 WHERE id='P-003'")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET shape='sphere' WHERE id='P-001'")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET position_x=1e999 WHERE id='P-001'")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET position_y='text' WHERE id='P-001'")));
    REQUIRE(!inspector.run(QStringLiteral("UPDATE part SET size_x=1e999 WHERE id='P-001'")));

    QList<Part> parts;
    REQUIRE(repository.loadParts(QStringLiteral("EQ-001"), parts));
    const QString otherId = QStringLiteral("EQ'002");
    for (Part &part : parts)
        part.equipmentId = otherId;
    parts.first().name = QStringLiteral("零件 O'Hara'); DROP TABLE part; --");
    // 不同设备可复用同样的零件 ID，且名称中的引号按文本保存。
    REQUIRE(repository.saveEquipmentWithParts({otherId, QStringLiteral("另一设备")}, parts));
    QList<Part> loaded;
    REQUIRE(repository.loadParts(otherId, loaded));
    REQUIRE(loaded.size() == 3 && loaded.first().name == parts.first().name);
    REQUIRE(repository.loadParts(QStringLiteral("EQ-001"), loaded));
    REQUIRE(loaded.size() == 3 && loaded.first().name == QStringLiteral("底座"));
    REQUIRE(!repository.saveEquipmentWithParts({otherId, QStringLiteral("重复")}, parts));
    REQUIRE(inspector.count(QStringLiteral("SELECT count(*) FROM equipment")) == 2);

    for (Part &part : parts)
        part.equipmentId = QStringLiteral("EQ-invalid");
    const Equipment invalid{QStringLiteral("EQ-invalid"), QStringLiteral("校验失败")};
    parts.first().positionX = std::numeric_limits<double>::infinity();
    REQUIRE(!repository.saveEquipmentWithParts(invalid, parts));
    parts.first().positionX = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(!repository.saveEquipmentWithParts(invalid, parts));
    parts.first().positionX = 0;
    parts.first().sizeX = 0;
    REQUIRE(!repository.saveEquipmentWithParts(invalid, parts));
    REQUIRE(inspector.count(QStringLiteral("SELECT count(*) FROM equipment")) == 2);
    return true;
}

bool writeFailureRollsBack()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("rollback.sqlite"));
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(path));
    REQUIRE(repository.initialize());
    Inspector inspector;
    REQUIRE(inspector.open(path));
    REQUIRE(inspector.run(QStringLiteral(
        "CREATE TRIGGER fail_third BEFORE INSERT ON part "
        "WHEN NEW.equipment_id='EQ-FAIL' AND NEW.id='P-003' "
        "BEGIN SELECT RAISE(ABORT,'forced third part failure'); END")));
    QList<Part> parts;
    REQUIRE(repository.loadParts(QStringLiteral("EQ-001"), parts));
    for (Part &part : parts)
        part.equipmentId = QStringLiteral("EQ-FAIL");
    REQUIRE(!repository.saveEquipmentWithParts({QStringLiteral("EQ-FAIL"), QStringLiteral("应回滚")}, parts));
    REQUIRE(repository.lastError().contains(QStringLiteral("forced third part failure")));
    REQUIRE(repository.lastError().contains(QStringLiteral("P-003")));
    REQUIRE(inspector.count(QStringLiteral("SELECT count(*) FROM equipment")) == 1);
    REQUIRE(inspector.count(QStringLiteral("SELECT count(*) FROM part")) == 3);
    REQUIRE(inspector.run(QStringLiteral("DROP TRIGGER fail_third")));
    REQUIRE(repository.saveEquipmentWithParts({QStringLiteral("EQ-FAIL"), QStringLiteral("重试成功")}, parts));
    REQUIRE(inspector.count(QStringLiteral("SELECT count(*) FROM part")) == 6);
    return true;
}

bool initializationCommitFailureRollsBack()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("commit.sqlite"));
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(path));
    Inspector reader;
    REQUIRE(reader.open(path));
    REQUIRE(reader.run(QStringLiteral("BEGIN")));
    // 读事务持有 SHARED 锁，允许写入但阻止首次初始化 COMMIT。
    REQUIRE(reader.count(QStringLiteral("SELECT count(*) FROM sqlite_schema")) == 0);
    REQUIRE(!repository.initialize());
    REQUIRE(repository.lastError().contains(QStringLiteral("提交初始化事务失败")));
    REQUIRE(repository.lastError().contains(QStringLiteral("locked")));
    REQUIRE(reader.run(QStringLiteral("ROLLBACK")));
    REQUIRE(reader.count(QStringLiteral("SELECT count(*) FROM sqlite_schema WHERE name IN ('equipment','part')")) == 0);
    REQUIRE(repository.initialize());
    REQUIRE(reader.count(QStringLiteral("SELECT count(*) FROM equipment")) == 1);
    REQUIRE(reader.count(QStringLiteral("SELECT count(*) FROM part")) == 3);
    return true;
}

bool errorsAndExistingSchema()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    EquipmentRepository repository;
    QList<Equipment> equipment;
    REQUIRE(!repository.loadEquipment(equipment));
    REQUIRE(!repository.openDatabase(QString()));
    REQUIRE(!repository.openDatabase(temporary.path()));
    REQUIRE(repository.lastError().contains(QStringLiteral("打开数据库")));
    const QString broken = temporary.filePath(QStringLiteral("broken.sqlite"));
    QFile file(broken);
    REQUIRE(file.open(QIODevice::WriteOnly));
    const QByteArray original("this is not SQLite");
    REQUIRE(file.write(original) == original.size());
    file.close();
    const bool opened = repository.openDatabase(broken);
    REQUIRE(!opened || !repository.initialize());
    REQUIRE(!repository.lastError().isEmpty());
    REQUIRE(file.open(QIODevice::ReadOnly));
    REQUIRE(file.readAll() == original);
    file.close();

    const QString incompatible = temporary.filePath(QStringLiteral("old.sqlite"));
    Inspector inspector;
    REQUIRE(inspector.open(incompatible));
    REQUIRE(inspector.run(QStringLiteral("CREATE TABLE equipment(id TEXT,name TEXT)")));
    REQUIRE(inspector.run(QStringLiteral("INSERT INTO equipment VALUES('old','保留')")));
    REQUIRE(repository.openDatabase(incompatible));
    REQUIRE(!repository.initialize());
    REQUIRE(repository.lastError().contains(QStringLiteral("结构异常")));
    REQUIRE(inspector.count(QStringLiteral("SELECT count(*) FROM equipment")) == 1);
    REQUIRE(inspector.count(QStringLiteral("SELECT count(*) FROM sqlite_schema WHERE name='part'")) == 0);
    REQUIRE(!repository.loadEquipment(equipment));
    return true;
}

bool processRestartAndOutput()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString executable = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("qt_sqlite_demo.exe"));
    const QString path = temporary.filePath(QStringLiteral("restart.sqlite"));
    for (int i = 0; i < 2; ++i) {
        QProcess process;
        process.start(executable, {QStringLiteral("-platform"), QStringLiteral("offscreen"),
                                  QStringLiteral("--equipment-check"), QStringLiteral("--database"), path});
        REQUIRE(process.waitForFinished(10000));
        REQUIRE(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0);
        const QByteArray output = process.readAllStandardOutput();
        REQUIRE(output.contains("COUNTS: equipment=1 parts=3"));
        REQUIRE(QString::fromUtf8(output).contains(QStringLiteral("输送机构")));
        REQUIRE(output.contains("EQ-001/P-003"));
        REQUIRE(QString::fromUtf8(output).contains(QFileInfo(path).absoluteFilePath()));
    }
    QProcess failed;
    failed.start(executable, {QStringLiteral("-platform"), QStringLiteral("offscreen"),
                             QStringLiteral("--equipment-check"), QStringLiteral("--database"), temporary.path()});
    REQUIRE(failed.waitForFinished(10000));
    REQUIRE(failed.exitStatus() == QProcess::NormalExit && failed.exitCode() == 1);
    REQUIRE(QString::fromUtf8(failed.readAllStandardError()).contains(QStringLiteral("打开数据库")));
    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const bool persistence = persistenceAndFiltering();
    const bool constraints = constraintsAndValidation();
    const bool rollback = writeFailureRollsBack();
    const bool commit = initializationCommitFailureRollsBack();
    const bool errors = errorsAndExistingSchema();
    const bool restart = processRestartAndOutput();
    QTextStream(stdout) << "Persistence: " << persistence << " Constraints: " << constraints
                        << " Rollback: " << rollback << " Commit: " << commit
                        << " Errors: " << errors << " Restart: " << restart << Qt::endl;
    return persistence && constraints && rollback && commit && errors && restart ? 0 : 1;
}
