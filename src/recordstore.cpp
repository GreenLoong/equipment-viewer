#include "recordstore.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

RecordStore::RecordStore()
    : connectionName_(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      database_(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_))
{
    database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=1000"));
}

RecordStore::~RecordStore()
{
    database_.close();
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
}

bool RecordStore::open(const QString &filePath)
{
    error_.clear();
    database_.close();
    if (filePath.trimmed().isEmpty()) {
        error_ = QStringLiteral("数据库路径不能为空。");
        return false;
    }
    const QFileInfo fileInfo(filePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        error_ = QStringLiteral("无法创建数据库目录：%1").arg(fileInfo.absolutePath());
        return false;
    }
    database_.setDatabaseName(fileInfo.absoluteFilePath());
    if (!database_.open()) {
        error_ = database_.lastError().text();
        return false;
    }
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS demo_records ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "content TEXT NOT NULL CHECK(length(trim(content)) BETWEEN 1 AND 200), "
            "created_at TEXT NOT NULL)"))) {
        error_ = query.lastError().text();
        database_.close();
        return false;
    }
    return true;
}

bool RecordStore::add(const QString &content)
{
    error_.clear();
    const QString trimmed = content.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 200) {
        error_ = QStringLiteral("请输入 1～200 个字符的记录，首尾空白会自动去除。");
        return false;
    }
    if (!database_.isOpen()) {
        error_ = QStringLiteral("数据库未打开。");
        return false;
    }
    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO demo_records (content, created_at) VALUES (?, ?)"))) {
        error_ = query.lastError().text();
        return false;
    }
    // 绑定值让引号和 SQL 片段按普通文本保存。
    query.addBindValue(trimmed);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        error_ = query.lastError().text();
        return false;
    }
    return true;
}

bool RecordStore::load(QList<Record> &records)
{
    error_.clear();
    records.clear();
    if (!database_.isOpen()) {
        error_ = QStringLiteral("数据库未打开。");
        return false;
    }
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT id, content, created_at FROM demo_records ORDER BY id DESC"))) {
        error_ = query.lastError().text();
        return false;
    }
    while (query.next()) {
        records.append({query.value(0).toLongLong(), query.value(1).toString(),
                        query.value(2).toString()});
    }
    if (query.lastError().isValid()) {
        error_ = query.lastError().text();
        records.clear();
        return false;
    }
    return true;
}

QString RecordStore::sqliteVersion()
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT sqlite_version()")) || !query.next()) {
        error_ = query.lastError().text();
        return {};
    }
    return query.value(0).toString();
}
