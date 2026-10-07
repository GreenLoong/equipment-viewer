#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

struct Record {
    qint64 id;
    QString content;
    QString createdAt;
};

class RecordStore {
public:
    RecordStore();
    ~RecordStore();
    RecordStore(const RecordStore &) = delete;
    RecordStore &operator=(const RecordStore &) = delete;

    bool open(const QString &filePath);
    bool add(const QString &content);
    bool load(QList<Record> &records);
    QString sqliteVersion();
    QString lastError() const { return error_; }
    QString databasePath() const { return database_.databaseName(); }

private:
    QString connectionName_;
    QSqlDatabase database_;
    QString error_;
};
