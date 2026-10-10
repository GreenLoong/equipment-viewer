#pragma once

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QString>
#include <QUuid>

/**
 * 单个 QSQLITE 连接的建立、打开与释放。
 * 设备仓库与原记录示例原先各写一份相同的连接生命周期，集中到此处后
 * 连接名唯一、1000毫秒忙等待、打开前创建目录的行为只有一处定义。
 * 连接名在析构时移除，避免同一进程重开数据库时残留旧连接。
 */
class SqliteConnection {
public:
    /** 注册独立连接名并设置忙等待；同一对象只能用于一个数据库文件。 */
    SqliteConnection()
        : connectionName_(QUuid::createUuid().toString(QUuid::WithoutBraces)),
          database_(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_))
    {
        database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=1000"));
    }

    /** 先关闭再移除连接名；析构必须早于QCoreApplication退出。 */
    ~SqliteConnection()
    {
        database_.close();
        database_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName_);
    }

    SqliteConnection(const SqliteConnection &) = delete;
    SqliteConnection &operator=(const SqliteConnection &) = delete;

    /**
     * 打开指定数据库文件并创建其上级目录。
     * 失败写入可定位错误并返回false：路径为空白、目录不可创建，或
     * openFailurePrefix 加 SQLite 原生错误（前缀为空时只写原生错误），
     * 由调用方决定是否补充文件路径等定位信息。
     */
    bool open(const QString &filePath, QString &error, const QString &openFailurePrefix = QString())
    {
        database_.close();
        database_.setDatabaseName(QString());
        if (filePath.trimmed().isEmpty()) {
            error = QStringLiteral("数据库路径不能为空。");
            return false;
        }
        const QFileInfo file(filePath);
        database_.setDatabaseName(file.absoluteFilePath());
        if (!QDir().mkpath(file.absolutePath())) {
            error = QStringLiteral("无法创建数据库目录：%1").arg(file.absolutePath());
            return false;
        }
        if (!database_.open()) {
            error = openFailurePrefix + database_.lastError().text();
            return false;
        }
        return true;
    }

    void close() { database_.close(); }
    bool isOpen() const { return database_.isOpen(); }
    QSqlDatabase &database() { return database_; }
    QString databasePath() const { return database_.databaseName(); }

private:
    QString connectionName_;
    QSqlDatabase database_;
};
