#include "cadresultparser.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QIODevice>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUuid>
#include <limits>

#define CHECK(x) do { if (!(x)) { QTextStream(stderr) << "FAIL " << __LINE__ << ": " << #x << Qt::endl; return false; } } while (false)

namespace {
// BASELINE_SCHEMA: 以下定义来自30b5fcc，用于模拟已有文件而非调用迁移实现。
QString nonBlank(const QString &column)
{
    return QStringLiteral("length(trim(%1, char(9,10,11,12,13,32,133,160,5760,"
                          "8192,8193,8194,8195,8196,8197,8198,8199,8200,8201,8202,"
                          "8232,8233,8239,8287,12288))) > 0").arg(column);
}

QString equipmentSchema()
{
    return QStringLiteral("CREATE TABLE equipment ("
                          "id TEXT NOT NULL PRIMARY KEY CHECK(%1), "
                          "name TEXT NOT NULL CHECK(%2)) STRICT")
        .arg(nonBlank(QStringLiteral("id")), nonBlank(QStringLiteral("name")));
}

QString partSchema()
{
    return QStringLiteral(
        "CREATE TABLE part ("
        "equipment_id TEXT NOT NULL CHECK(%1), "
        "id TEXT NOT NULL CHECK(%2), "
        "name TEXT NOT NULL CHECK(%3), "
        "material TEXT NOT NULL CHECK(%4), "
        "shape TEXT NOT NULL CHECK(shape IN ('box','cylinder')), "
        "position_x REAL NOT NULL CHECK(abs(position_x) <= 1.7976931348623157e308), "
        "position_y REAL NOT NULL CHECK(abs(position_y) <= 1.7976931348623157e308), "
        "position_z REAL NOT NULL CHECK(abs(position_z) <= 1.7976931348623157e308), "
        "size_x REAL, size_y REAL, size_z REAL, radius REAL, length REAL, "
        "PRIMARY KEY(equipment_id,id), "
        "FOREIGN KEY(equipment_id) REFERENCES equipment(id), "
        // CHECK 遇到 NULL 本来会放行，所以正尺寸还必须显式检查 IS NOT NULL。
        "CHECK((shape = 'box' AND size_x IS NOT NULL AND size_y IS NOT NULL "
        "AND size_z IS NOT NULL AND size_x > 0 AND size_y > 0 AND size_z > 0 "
        "AND size_x <= 1.7976931348623157e308 AND size_y <= 1.7976931348623157e308 "
        "AND size_z <= 1.7976931348623157e308 AND radius IS NULL AND length IS NULL) "
        "OR (shape = 'cylinder' AND radius IS NOT NULL AND length IS NOT NULL "
        "AND radius > 0 AND length > 0 AND radius <= 1.7976931348623157e308 "
        "AND length <= 1.7976931348623157e308 "
        "AND size_x IS NULL AND size_y IS NULL AND size_z IS NULL))) STRICT")
        .arg(nonBlank(QStringLiteral("equipment_id")), nonBlank(QStringLiteral("id")),
             nonBlank(QStringLiteral("name")), nonBlank(QStringLiteral("material")));
}



struct Inspector {
    QString connection = QUuid::createUuid().toString();
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
    ~Inspector() { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); }
    bool open(const QString &path) { db.setDatabaseName(path); return db.open(); }
    bool run(const QString &sql) { QSqlQuery q(db); if(q.exec(sql)) return true; QTextStream(stderr)<<q.lastError().text()<<Qt::endl; return false; }
    QVariant value(const QString &sql) { QSqlQuery q(db); if (!q.exec(sql) || !q.next()) return {}; return q.value(0); }
};
bool repositoryForeignKeys(const QString &path)
{
    for (const auto &name : QSqlDatabase::connectionNames()) {
        const auto db = QSqlDatabase::database(name, false);
        if (db.databaseName() != path || name.startsWith('{')) continue;
        QSqlQuery query(db);
        return query.exec("PRAGMA foreign_keys") && query.next() && query.value(0).toInt() == 1;
    }
    return false;
}
QJsonObject protocol()
{
    return {{"protocol", "equipment-cad-mesh"}, {"version", 1}, {"unit", "mm"},
        {"sourceName", "motor.step"}, {"sourceSha256", QString(64, 'a')},
        {"parts", QJsonArray{QJsonObject{{"id", "solid'1"}, {"name", "曲面"}, {"material", "未提供"},
        {"shape", "mesh"}, {"dimensions", QJsonArray{2,2,2}}, {"position", QJsonArray{4,5,6}},
        {"mesh", QJsonObject{{"vertices", QJsonArray{QJsonArray{-1,-1,-1}, QJsonArray{1,-1,1}, QJsonArray{0,1,0}}},
                            {"triangles", QJsonArray{QJsonArray{0,1,2}}}}}}}}};
}
bool parserAndCodec()
{
    Equipment equipment{"old", "old"}; QList<Part> parts; QString error;
    CHECK(CadResultParser::parse(QJsonDocument(protocol()).toJson(), equipment, parts, error));
    CHECK(equipment.id == "CAD-" + QString(64, 'a') && equipment.id.size() == 68);
    CHECK(equipment.name == "motor.step" && parts.size() == 1 && parts.first().shape == "mesh");
    QByteArray blob;
    CHECK(MeshGeometryCodec::encode(*parts.first().mesh, blob, error));
    CHECK(blob.size() == 12 + 24*3 + 12 && blob.first(4) == "EVM1");
    MeshGeometry decoded;
    CHECK(MeshGeometryCodec::decode(blob, decoded, error));
    CHECK(decoded.vertices == parts.first().mesh->vertices && decoded.triangles == parts.first().mesh->triangles);
    for (QByteArray bad : {blob.first(11), blob + 'x', QByteArray("EVM1\xff\xff\xff\xff\x01\0\0\0", 12)}) {
        CHECK(!MeshGeometryCodec::decode(bad, decoded, error));
        CHECK(decoded.vertices.size() == 3);
    }
    MeshGeometry bad = decoded;
    bad.vertices[0][0] = std::numeric_limits<double>::quiet_NaN();
    CHECK(!MeshGeometryCodec::validate(bad,error));
    bad = decoded; bad.triangles[0] = {0,0,2}; CHECK(!MeshGeometryCodec::validate(bad,error));
    bad = decoded; bad.triangles[0] = {0,1,9}; CHECK(!MeshGeometryCodec::validate(bad,error));
    bad = decoded; bad.vertices[2] = bad.vertices[1]; CHECK(!MeshGeometryCodec::validate(bad,error));
    QByteArray badBlob = blob;
    QDataStream stream(&badBlob, QIODevice::ReadWrite);
    stream.setVersion(QDataStream::Qt_6_5); stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
    stream.device()->seek(12); stream << std::numeric_limits<double>::quiet_NaN();
    CHECK(!MeshGeometryCodec::decode(badBlob,decoded,error));
    CHECK(!MeshGeometryCodec::validateBounds(decoded,{2,2,2},
        {std::numeric_limits<double>::infinity(),0,0},error));
    auto invalid = protocol();
    auto array = invalid["parts"].toArray(); auto part = array[0].toObject();
    auto mesh = part["mesh"].toObject(); mesh["triangles"] = QJsonArray{QJsonArray{0,1.5,2}};
    part["mesh"] = mesh; array[0] = part; invalid["parts"] = array;
    CHECK(!CadResultParser::parse(QJsonDocument(invalid).toJson(), equipment, parts, error));
    CHECK(equipment.id.startsWith("CAD-") && parts.first().mesh->triangles[0][1] == 1);
    invalid = protocol(); invalid["unit"] = "m";
    CHECK(!CadResultParser::parse(QJsonDocument(invalid).toJson(), equipment, parts, error));
    invalid = protocol(); invalid["unexpected"] = 1;
    CHECK(!CadResultParser::parse(QJsonDocument(invalid).toJson(), equipment, parts, error));
    invalid = protocol(); array = invalid["parts"].toArray(); part = array[0].toObject();
    part["dimensions"] = QJsonArray{0.1,2,2}; array[0] = part; invalid["parts"] = array;
    CHECK(!CadResultParser::parse(QJsonDocument(invalid).toJson(), equipment, parts, error));
    CHECK(!CadResultParser::parse(QByteArray("\xff",1), equipment,parts,error));
    invalid=protocol(); array=invalid["parts"].toArray();
    for(int n=1;n<1001;++n) { part=array[0].toObject(); part["id"]=QString::number(n); array.append(part); }
    invalid["parts"]=array;
    CHECK(!CadResultParser::parse(QJsonDocument(invalid).toJson(),equipment,parts,error));
    return true;
}
bool persistence()
{
    QTemporaryDir temp; CHECK(temp.isValid()); const QString path = temp.filePath("cad.sqlite");
    Equipment equipment; QList<Part> parts; QString error;
    CHECK(CadResultParser::parse(QJsonDocument(protocol()).toJson(), equipment, parts, error));
    {
        EquipmentRepository repo; CHECK(repo.openDatabase(path)); CHECK(repo.initialize());
        CHECK(repo.saveEquipmentWithParts(equipment, parts));
        CHECK(!repo.saveEquipmentWithParts(equipment, parts));
        QList<Part> large(1001, parts.first());
        for(int i=0;i<large.size();++i) { large[i].equipmentId="too-many"; large[i].id=QString::number(i); }
        CHECK(!repo.saveEquipmentWithParts({"too-many","invalid"},large));
        auto invalid=parts; invalid[0].equipmentId="overflow";
        auto oversized=std::make_shared<MeshGeometry>(*parts[0].mesh);
        oversized->vertices.resize(MeshGeometryCodec::maxVertices+1);
        invalid[0].mesh=oversized;
        CHECK(!repo.saveEquipmentWithParts({"overflow","invalid"},invalid));
    }
    {
        EquipmentRepository repo; CHECK(repo.openDatabase(path)); CHECK(repo.initialize());
        QList<Part> loaded; CHECK(repo.loadParts(equipment.id,loaded));
        CHECK(loaded.size()==1 && loaded.first().mesh->vertices==parts.first().mesh->vertices);
        CHECK(loaded.first().cadSourceName=="motor.step" && loaded.first().cadSourceSha256==QString(64,'a'));
        Inspector i; CHECK(i.open(path));
        CHECK(i.run("UPDATE part SET mesh_blob=zeroblob(12) WHERE shape='mesh'"));
        CHECK(!repo.loadParts(equipment.id,loaded));
        CHECK(repo.lastError().contains("BLOB"));
    }
    return true;
}
bool migration()
{
    QTemporaryDir temp; CHECK(temp.isValid());
    const QString path=temp.filePath("legacy.sqlite"); Inspector i; CHECK(i.open(path));
    CHECK(i.run(equipmentSchema())); CHECK(i.run(partSchema()));
    CHECK(i.run("INSERT INTO equipment VALUES('EQ''old','既有')"));
    CHECK(i.run("INSERT INTO part(rowid,equipment_id,id,name,material,shape,position_x,position_y,position_z,size_x,size_y,size_z,radius,length) "
                "VALUES(42,'EQ''old','P''old','引号零件','钢','box',1,2,3,4,5,6,NULL,NULL)"));
    CHECK(i.run("INSERT INTO part(rowid,equipment_id,id,name,material,shape,position_x,position_y,position_z,size_x,size_y,size_z,radius,length) "
                "VALUES(99,'EQ''old','shaft','轴','钢','cylinder',-1,-2,-3,NULL,NULL,NULL,8,9)"));
    CHECK(i.run("CREATE INDEX part_name_kept ON part(name)"));
    CHECK(i.run("CREATE TABLE audit(value TEXT)"));
    CHECK(i.run("CREATE TRIGGER part_update_kept AFTER UPDATE ON part BEGIN INSERT INTO audit VALUES(NEW.name); END"));
    const auto oldTrigger=i.value("SELECT sql FROM sqlite_schema WHERE name='part_update_kept'");
    for(int n=0;n<2;++n) {
        EquipmentRepository repo; CHECK(repo.openDatabase(path));
        if(!repo.initialize()) { QTextStream(stderr)<<repo.lastError()<<Qt::endl; return false; }
        QList<Part> parts; CHECK(repo.loadParts("EQ'old",parts)); CHECK(parts.size()==2 && parts[0].sizeY==5);
        CHECK(parts[0].positionX==1 && parts[0].positionY==2 && parts[0].positionZ==3);
        CHECK(parts[1].radius==8 && parts[1].length==9 && parts[1].positionX==-1);
        CHECK(i.value("SELECT rowid FROM part WHERE shape='box'").toInt()==42);
        CHECK(i.value("SELECT rowid FROM part WHERE shape='cylinder'").toInt()==99);
        CHECK(repositoryForeignKeys(path));
        CHECK(i.value("SELECT count(*) FROM equipment").toInt()==1);
        CHECK(i.value("SELECT sql FROM sqlite_schema WHERE name='part_update_kept'")==oldTrigger);
        CHECK(i.value("SELECT count(*) FROM sqlite_schema WHERE name='part_name_kept'").toInt()==1);
    }
    CHECK(i.run("UPDATE part SET name='更新'")); CHECK(i.value("SELECT count(*) FROM audit").toInt()==2);
    // 临时表名冲突拒绝升级；另用读锁制造COMMIT失败，核实DDL与已有资料一起回滚。
    const QString failed=temp.filePath("collision.sqlite"); Inspector collision; CHECK(collision.open(failed));
    CHECK(collision.run(equipmentSchema())); CHECK(collision.run(partSchema()));
    CHECK(collision.run("CREATE TABLE __ev_part_v2(preserved TEXT)"));
    { EquipmentRepository repo; CHECK(repo.openDatabase(failed)); CHECK(!repo.initialize());
      CHECK(repositoryForeignKeys(failed)); }
    CHECK(collision.value("SELECT sql FROM sqlite_schema WHERE name='part'").toString()==partSchema());
    CHECK(collision.value("SELECT count(*) FROM sqlite_schema WHERE name='__ev_part_v2'").toInt()==1);
    const QString rollback=temp.filePath("rollback.sqlite"); Inspector locked; CHECK(locked.open(rollback));
    CHECK(locked.run(equipmentSchema())); CHECK(locked.run(partSchema()));
    CHECK(locked.run("INSERT INTO equipment VALUES('existing','protected')"));
    CHECK(locked.run("BEGIN")); CHECK(locked.value("SELECT count(*) FROM equipment").toInt()==1);
    { EquipmentRepository repo; CHECK(repo.openDatabase(rollback)); CHECK(!repo.initialize());
      CHECK(repo.lastError().contains("提交初始化事务失败")); CHECK(repositoryForeignKeys(rollback)); }
    CHECK(locked.run("ROLLBACK"));
    CHECK(locked.value("SELECT sql FROM sqlite_schema WHERE name='part'").toString()==partSchema());
    CHECK(locked.value("SELECT count(*) FROM equipment").toInt()==1);
    CHECK(locked.value("SELECT count(*) FROM sqlite_schema WHERE name='__ev_part_v2'").toInt()==0);
    // 在Repository自己的连接建立TEMP冲突；外部连接的TEMP表与其互不相见。
    const QString tempCollision=temp.filePath("temp-collision.sqlite"); Inspector t; CHECK(t.open(tempCollision));
    CHECK(t.run(equipmentSchema())); CHECK(t.run(partSchema()));
    { EquipmentRepository repo; CHECK(repo.openDatabase(tempCollision));
      for(const auto &name:QSqlDatabase::connectionNames()) {
          auto db=QSqlDatabase::database(name,false);
          if(db.databaseName()==tempCollision && !name.startsWith('{')) {
              QSqlQuery query(db); CHECK(query.exec("CREATE TEMP TABLE __ev_part_v2(preserved TEXT)"));
          }
      }
      CHECK(!repo.initialize()); CHECK(repositoryForeignKeys(tempCollision)); }
    CHECK(t.value("SELECT sql FROM sqlite_schema WHERE name='part'").toString()==partSchema());
    return true;
}
bool converterInteroperability(const QStringList &probePaths)
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    const QString databasePath = temp.filePath("converter-roundtrip.sqlite");
    QList<Equipment> expectedEquipment;
    QList<QList<Part>> expectedParts;
    {
        EquipmentRepository repo;
        CHECK(repo.openDatabase(databasePath)); CHECK(repo.initialize());
        for (const auto &path : probePaths) {
            QFile file(path); CHECK(file.open(QIODevice::ReadOnly));
            CHECK(file.size() > 0 && quint64(file.size()) <= MeshGeometryCodec::maxBytes);
            Equipment equipment; QList<Part> parts; QString error;
            if (!CadResultParser::parse(file.readAll(), equipment, parts, error)) {
                QTextStream(stderr) << path << ": " << error << Qt::endl;
                return false;
            }
            CHECK(repo.saveEquipmentWithParts(equipment, parts));
            CHECK(!repo.saveEquipmentWithParts(equipment, parts));
            expectedEquipment.append(equipment); expectedParts.append(parts);
        }
    }
    // 新Repository连接模拟关闭后重新打开；所有字段和double网格必须逐项无损。
    EquipmentRepository reopened;
    CHECK(reopened.openDatabase(databasePath)); CHECK(reopened.initialize());
    for (qsizetype model = 0; model < expectedEquipment.size(); ++model) {
        QList<Part> actual; CHECK(reopened.loadParts(expectedEquipment[model].id, actual));
        const auto &expected = expectedParts[model]; CHECK(actual.size() == expected.size());
        quint64 vertices = 0, triangles = 0;
        for (qsizetype index = 0; index < expected.size(); ++index) {
            const Part &a = actual[index], &e = expected[index];
            CHECK(a.equipmentId == e.equipmentId && a.id == e.id && a.name == e.name
                  && a.material == e.material && a.shape == "mesh");
            CHECK(a.positionX == e.positionX && a.positionY == e.positionY && a.positionZ == e.positionZ);
            CHECK(a.sizeX == e.sizeX && a.sizeY == e.sizeY && a.sizeZ == e.sizeZ && !a.radius && !a.length);
            CHECK(a.cadSourceName == e.cadSourceName && a.cadSourceSha256 == e.cadSourceSha256);
            CHECK(a.mesh && a.mesh->vertices == e.mesh->vertices && a.mesh->triangles == e.mesh->triangles);
            vertices += quint64(a.mesh->vertices.size()); triangles += quint64(a.mesh->triangles.size());
        }
        QTextStream(stdout) << "ConverterRoundtrip " << expectedEquipment[model].name
            << " parts=" << actual.size() << " vertices=" << vertices << " triangles=" << triangles
            << " accepted=1" << Qt::endl;
    }
    return true;
}
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    const bool codec=parserAndCodec(), stored=persistence(), upgraded=migration();
    const bool interoperable=converterInteroperability(app.arguments().mid(1));
    QTextStream(stdout)<<"ParserCodec="<<codec<<" Persistence="<<stored<<" Migration="<<upgraded
                       <<" Interoperability="<<interoperable<<Qt::endl;
    return codec&&stored&&upgraded&&interoperable ? 0 : 1;
}
