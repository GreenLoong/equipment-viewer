#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVariant>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    out << "Qt runtime: " << qVersion() << "\nArchitecture: " << QSysInfo::buildCpuArchitecture()
        << "\nSQL drivers: " << QSqlDatabase::drivers().join(QStringLiteral(", ")) << Qt::endl;
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 1;
    auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
    database.setDatabaseName(temporary.filePath(QStringLiteral("probe.sqlite")));
    auto fail = [&](const QString &message) {
        QTextStream(stderr) << "ENVIRONMENT FAIL: " << message << Qt::endl;
        return 1;
    };
    if (!database.open())
        return fail(database.lastError().text());
    const QString expected = QStringLiteral("SQLite 中文 O'Hara 验证");
    {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("SELECT sqlite_version()")) || !query.next())
            return fail(query.lastError().text());
        out << "SQLite runtime: " << query.value(0).toString() << Qt::endl;
        if (!query.exec(QStringLiteral("CREATE TABLE probe (content TEXT NOT NULL)")))
            return fail(query.lastError().text());
        if (!query.prepare(QStringLiteral("INSERT INTO probe VALUES (?)")))
            return fail(query.lastError().text());
        query.addBindValue(expected);
        if (!query.exec())
            return fail(query.lastError().text());
    }
    database.close();
    if (!database.open())
        return fail(database.lastError().text());
    {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("SELECT content FROM probe")) || !query.next())
            return fail(query.lastError().text());
        if (query.value(0).toString() != expected)
            return fail(QStringLiteral("Data mismatch after reopening database"));
    }
    database.close();
    out << "ENVIRONMENT PASS: create, bound insert, reopen, read" << Qt::endl;
    return 0;
}
