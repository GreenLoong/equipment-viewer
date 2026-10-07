#include "equipmentimporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUuid>
#include <QVariant>

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            QTextStream(stderr) << "FAIL at line " << __LINE__ << ": " << #condition << Qt::endl; \
            return false; \
        } \
    } while (false)

namespace {
QJsonObject validDocument()
{
    return QJsonDocument::fromJson(R"({
        "equipmentId":"EQ-IMPORT", "equipmentName":"Learning equipment",
        "parts":[
            {"id":"P-1","name":"Block","material":"Steel","shape":"box",
             "dimensions":[20,30,40],"position":[-10,0,20]},
            {"id":"P-2","name":"Block","material":"Steel","shape":"cylinder",
             "dimensions":[5,60],"position":[0,10,40]}
        ]
    })").object();
}

QByteArray bytes(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject changedPart(const QString &key, const QJsonValue &value)
{
    QJsonObject root = validDocument();
    QJsonArray parts = root.value(QStringLiteral("parts")).toArray();
    QJsonObject part = parts.at(1).toObject();
    part.insert(key, value);
    parts[1] = part;
    root.insert(QStringLiteral("parts"), parts);
    return root;
}

bool rejectedWithoutChangingOutput(const QByteArray &json, const QString &errorFragment)
{
    Equipment equipment{QStringLiteral("original-id"), QStringLiteral("original-name")};
    Part original;
    original.equipmentId = equipment.id;
    original.id = QStringLiteral("old-part");
    original.name = QStringLiteral("old-name");
    original.material = QStringLiteral("old-material");
    original.shape = QStringLiteral("box");
    original.positionX = -123;
    original.sizeX = 1;
    original.sizeY = 2;
    original.sizeZ = 3;
    QList<Part> parts{original};
    QString error;
    REQUIRE(!EquipmentImporter::parse(json, equipment, parts, error));
    REQUIRE(error.contains(errorFragment));
    REQUIRE(equipment.id == QStringLiteral("original-id"));
    REQUIRE(equipment.name == QStringLiteral("original-name"));
    REQUIRE(parts.size() == 1 && parts.first().id == original.id);
    REQUIRE(parts.first().equipmentId == original.equipmentId);
    REQUIRE(parts.first().name == original.name && parts.first().material == original.material);
    REQUIRE(parts.first().shape == original.shape && parts.first().positionX == -123);
    REQUIRE(parts.first().sizeX == 1 && parts.first().sizeY == 2 && parts.first().sizeZ == 3);
    REQUIRE(!parts.first().radius && !parts.first().length);
    return true;
}

bool validMapping()
{
    Equipment equipment;
    QList<Part> parts;
    QString error = QStringLiteral("old error");
    QJsonObject root = validDocument();
    root.insert(QStringLiteral("futureField"), true);
    root.insert(QStringLiteral("equipmentId"), QStringLiteral(" EQ-IMPORT "));
    REQUIRE(EquipmentImporter::parse(bytes(root), equipment, parts, error));
    REQUIRE(error.isEmpty());
    REQUIRE(equipment.id == QStringLiteral(" EQ-IMPORT "));
    REQUIRE(parts.size() == 2 && parts.first().equipmentId == equipment.id);
    REQUIRE(parts.first().name == parts.last().name && parts.first().id != parts.last().id);
    REQUIRE(parts.first().positionX == -10 && parts.first().positionZ == 20);
    REQUIRE(parts.first().sizeX == 20 && parts.first().sizeY == 30 && parts.first().sizeZ == 40);
    REQUIRE(!parts.first().radius && !parts.first().length);
    REQUIRE(parts.last().radius == 5 && parts.last().length == 60);
    REQUIRE(!parts.last().sizeX && !parts.last().sizeY && !parts.last().sizeZ);
    root.insert(QStringLiteral("parts"), QJsonArray());
    REQUIRE(EquipmentImporter::parse(bytes(root), equipment, parts, error));
    REQUIRE(parts.isEmpty());
    return true;
}

bool malformedTypesAndFields()
{
    REQUIRE(rejectedWithoutChangingOutput("{broken", QStringLiteral("JSON")));
    REQUIRE(rejectedWithoutChangingOutput("[]", QStringLiteral("根节点")));
    QJsonObject root = validDocument();
    root.remove(QStringLiteral("equipmentName"));
    REQUIRE(rejectedWithoutChangingOutput(bytes(root), QStringLiteral("equipmentName")));
    root = validDocument();
    root.insert(QStringLiteral("equipmentId"), 123);
    REQUIRE(rejectedWithoutChangingOutput(bytes(root), QStringLiteral("equipmentId")));
    root.insert(QStringLiteral("equipmentId"), QStringLiteral(" \t　"));
    REQUIRE(rejectedWithoutChangingOutput(bytes(root), QStringLiteral("equipmentId")));
    root = validDocument();
    root.remove(QStringLiteral("parts"));
    REQUIRE(rejectedWithoutChangingOutput(bytes(root), QStringLiteral("parts")));
    root.insert(QStringLiteral("parts"), false);
    REQUIRE(rejectedWithoutChangingOutput(bytes(root), QStringLiteral("parts")));
    root.insert(QStringLiteral("parts"), QJsonArray{1});
    REQUIRE(rejectedWithoutChangingOutput(bytes(root), QStringLiteral("parts[0]")));
    for (const QString &key : {QStringLiteral("id"), QStringLiteral("name"),
                               QStringLiteral("material"), QStringLiteral("shape")}) {
        REQUIRE(rejectedWithoutChangingOutput(bytes(changedPart(key, QJsonValue())), key));
        REQUIRE(rejectedWithoutChangingOutput(bytes(changedPart(key, QStringLiteral("\t　"))), key));
    }
    REQUIRE(rejectedWithoutChangingOutput(bytes(changedPart(QStringLiteral("shape"), QStringLiteral("sphere"))),
                                         QStringLiteral("shape")));
    REQUIRE(rejectedWithoutChangingOutput(bytes(changedPart(QStringLiteral("id"), QStringLiteral("P-1"))),
                                         QStringLiteral("重复")));
    return true;
}

bool invalidGeometry()
{
    REQUIRE(rejectedWithoutChangingOutput(bytes(changedPart(QStringLiteral("position"),
                                          QJsonArray{1, 2})), QStringLiteral("position")));
    REQUIRE(rejectedWithoutChangingOutput(bytes(changedPart(QStringLiteral("position"),
                                          QJsonArray{1, QStringLiteral("2"), 3})), QStringLiteral("position[1]")));
    REQUIRE(rejectedWithoutChangingOutput(bytes(changedPart(QStringLiteral("position"),
                                          QJsonArray{1, QJsonValue(), 3})), QStringLiteral("position[1]")));
    REQUIRE(rejectedWithoutChangingOutput(bytes(changedPart(QStringLiteral("dimensions"),
                                          QJsonArray{1, 2, 3})), QStringLiteral("dimensions")));
    REQUIRE(rejectedWithoutChangingOutput(bytes(changedPart(QStringLiteral("dimensions"),
                                          QJsonArray{0, 10})), QStringLiteral("dimensions[0]")));
    REQUIRE(rejectedWithoutChangingOutput(bytes(changedPart(QStringLiteral("dimensions"),
                                          QJsonArray{5, -10})), QStringLiteral("dimensions[1]")));
    REQUIRE(rejectedWithoutChangingOutput(bytes(changedPart(QStringLiteral("dimensions"),
                                          QJsonArray{QStringLiteral("5"), 10})), QStringLiteral("dimensions[0]")));
    QByteArray overflow = bytes(validDocument());
    overflow.replace("[-10,0,20]", "[1e999,0,20]");
    REQUIRE(rejectedWithoutChangingOutput(overflow, QStringLiteral("JSON")));
    return true;
}

bool partCountLimit()
{
    QJsonObject root = validDocument();
    QJsonObject source = root.value(QStringLiteral("parts")).toArray().first().toObject();
    QJsonArray many;
    for (int i = 0; i < 10000; ++i) {
        source.insert(QStringLiteral("id"), QStringLiteral("P-%1").arg(i));
        many.append(source);
    }
    root.insert(QStringLiteral("parts"), many);
    Equipment equipment;
    QList<Part> parts;
    QString error;
    REQUIRE(EquipmentImporter::parse(bytes(root), equipment, parts, error));
    REQUIRE(parts.size() == 10000);
    source.insert(QStringLiteral("id"), QStringLiteral("P-over-limit"));
    many.append(source);
    root.insert(QStringLiteral("parts"), many);
    REQUIRE(rejectedWithoutChangingOutput(bytes(root), QStringLiteral("10000")));
    return true;
}

// 只连接 QTemporaryDir 的文件，用触发器制造第二个零件写入失败。
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
        return database.open();
    }
    bool run(const QString &sql)
    {
        QSqlQuery query(database);
        return query.exec(sql);
    }
    int count(const QString &sql)
    {
        QSqlQuery query(database);
        return query.exec(sql) && query.next() ? query.value(0).toInt() : -1;
    }
};

bool readSavedImport(const QString &path)
{
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(path));
    REQUIRE(repository.initialize());
    QList<Part> parts;
    REQUIRE(repository.loadParts(QStringLiteral("EQ-IMPORT"), parts));
    REQUIRE(parts.size() == 2 && parts.first().sizeX == 20 && parts.last().radius == 5);
    REQUIRE(parts.first().name == parts.last().name);
    return true;
}

bool transactionalPersistence()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("import.sqlite"));
    Equipment equipment;
    QList<Part> parts;
    QString error;
    REQUIRE(EquipmentImporter::parse(bytes(validDocument()), equipment, parts, error));
    {
        EquipmentRepository repository;
        REQUIRE(repository.openDatabase(path));
        REQUIRE(repository.initialize());
        REQUIRE(repository.saveEquipmentWithParts(equipment, parts));
        REQUIRE(!repository.saveEquipmentWithParts(equipment, parts));
        REQUIRE(repository.lastError().contains(QStringLiteral("EQ-IMPORT")));
    }
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(),
                {QStringLiteral("--read-import"), path});
    REQUIRE(child.waitForFinished(10000));
    REQUIRE(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0);

    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(path));
    REQUIRE(repository.initialize());
    Inspector inspector;
    REQUIRE(inspector.open(path));
    REQUIRE(inspector.count(QStringLiteral("SELECT count(*) FROM equipment")) == 2);
    REQUIRE(inspector.count(QStringLiteral("SELECT count(*) FROM part")) == 5);
    REQUIRE(inspector.run(QStringLiteral(
        "CREATE TRIGGER fail_import BEFORE INSERT ON part "
        "WHEN NEW.equipment_id='EQ-FAIL-IMPORT' AND NEW.id='P-2' "
        "BEGIN SELECT RAISE(ABORT,'forced import failure'); END")));
    QJsonObject root = validDocument();
    root.insert(QStringLiteral("equipmentId"), QStringLiteral("EQ-FAIL-IMPORT"));
    REQUIRE(EquipmentImporter::parse(bytes(root), equipment, parts, error));
    REQUIRE(!repository.saveEquipmentWithParts(equipment, parts));
    REQUIRE(repository.lastError().contains(QStringLiteral("forced import failure")));
    REQUIRE(inspector.count(QStringLiteral("SELECT count(*) FROM equipment WHERE id='EQ-FAIL-IMPORT'")) == 0);
    REQUIRE(inspector.count(QStringLiteral("SELECT count(*) FROM part WHERE equipment_id='EQ-FAIL-IMPORT'")) == 0);
    REQUIRE(inspector.count(QStringLiteral("SELECT count(*) FROM part")) == 5);
    REQUIRE(inspector.run(QStringLiteral("DROP TRIGGER fail_import")));
    REQUIRE(repository.saveEquipmentWithParts(equipment, parts));
    root.insert(QStringLiteral("equipmentId"), QStringLiteral("EQ-EMPTY-IMPORT"));
    root.insert(QStringLiteral("parts"), QJsonArray());
    REQUIRE(EquipmentImporter::parse(bytes(root), equipment, parts, error));
    REQUIRE(repository.saveEquipmentWithParts(equipment, parts));
    REQUIRE(repository.loadParts(equipment.id, parts) && parts.isEmpty());
    return true;
}

bool realSampleFiles(const QString &directory)
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    EquipmentRepository repository;
    REQUIRE(repository.openDatabase(temporary.filePath(QStringLiteral("samples.sqlite"))));
    REQUIRE(repository.initialize());
    for (const QString &name : {QStringLiteral("conveyor-learning.json"),
                               QStringLiteral("support-assembly-learning.json")}) {
        QFile file(QDir(directory).filePath(name));
        REQUIRE(file.open(QIODevice::ReadOnly));
        Equipment equipment;
        QList<Part> parts;
        QString error;
        REQUIRE(EquipmentImporter::parse(file.readAll(), equipment, parts, error));
        REQUIRE(repository.saveEquipmentWithParts(equipment, parts));
        QList<Part> loaded;
        REQUIRE(repository.loadParts(equipment.id, loaded));
        REQUIRE(loaded.size() == parts.size() && !loaded.isEmpty());
    }
    QList<Equipment> equipment;
    REQUIRE(repository.loadEquipment(equipment));
    REQUIRE(equipment.size() == 3);
    return true;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--read-import"))
        return readSavedImport(QString::fromLocal8Bit(argv[2])) ? 0 : 1;
    if (argc != 2) {
        QTextStream(stderr) << "Usage: import_tests <samples-directory>" << Qt::endl;
        return 1;
    }
    const bool mapping = validMapping();
    const bool fields = malformedTypesAndFields();
    const bool geometry = invalidGeometry();
    const bool limit = partCountLimit();
    const bool persistence = transactionalPersistence();
    const bool samples = realSampleFiles(QString::fromLocal8Bit(argv[1]));
    QTextStream(stdout) << "Mapping: " << mapping << " Fields: " << fields
                        << " Geometry: " << geometry << " Limit: " << limit
                        << " Persistence: " << persistence << " Samples: " << samples << Qt::endl;
    return mapping && fields && geometry && limit && persistence && samples ? 0 : 1;
}
