#include "recordstore.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

RecordStore::RecordStore() = default;

// 连接名注册与释放由SqliteConnection统一负责。
RecordStore::~RecordStore() = default;

bool RecordStore::open(const QString &filePath)
{
    error_.clear();
    // 连接关闭/目录创建/打开与设备仓库共用同一实现。
    if (!connection_.open(filePath, error_))
        return false;
    QSqlQuery query(connection_.database());
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS demo_records ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "content TEXT NOT NULL CHECK(length(trim(content)) BETWEEN 1 AND 200), "
            "created_at TEXT NOT NULL)"))) {
        error_ = query.lastError().text();
        connection_.close();
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
    if (!connection_.isOpen()) {
        error_ = QStringLiteral("数据库未打开。");
        return false;
    }
    QSqlQuery query(connection_.database());
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
    if (!connection_.isOpen()) {
        error_ = QStringLiteral("数据库未打开。");
        return false;
    }
    QSqlQuery query(connection_.database());
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
    QSqlQuery query(connection_.database());
    if (!query.exec(QStringLiteral("SELECT sqlite_version()")) || !query.next()) {
        error_ = query.lastError().text();
        return {};
    }
    return query.value(0).toString();
}
