#include "mainwindow.h"
#include "recordstore.h"

#include <QFile>
#include <QApplication>
#include <QDebug>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            QTextStream(stderr) << "FAIL at line " << __LINE__ << ": " << #condition << Qt::endl; \
            return false; \
        } \
    } while (false)
#define REQUIRE_EQUAL(actual, expected) REQUIRE((actual) == (expected))

bool persistenceAndBoundText()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("中文目录/records.sqlite"));
    const QString text = QStringLiteral("中文 O'Hara'); DROP TABLE demo_records; -- 🙂");
    {
        RecordStore store;
        REQUIRE(store.open(path));
        REQUIRE(store.add(QStringLiteral("第一条")));
        REQUIRE(store.add(QStringLiteral("  ") + text + QStringLiteral("  ")));
    }
    {
        RecordStore reopened;
        REQUIRE(reopened.open(path));
        QList<Record> records;
        REQUIRE(reopened.load(records));
        REQUIRE_EQUAL(records.size(), 2);
        REQUIRE_EQUAL(records.at(0).content, text);
        REQUIRE_EQUAL(records.at(1).content, QStringLiteral("第一条"));
        REQUIRE(records.at(0).id > records.at(1).id);
        REQUIRE(!records.at(0).createdAt.isEmpty());
        REQUIRE(!reopened.sqliteVersion().isEmpty());
    }
    return true;
}

bool validationAndFailedOpen()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    RecordStore store;
    REQUIRE(!store.add(QStringLiteral("未连接")));
    REQUIRE(!store.open(QString()));
    REQUIRE(!store.open(temporary.path())); // 目录不能作为数据库文件。
    REQUIRE(store.open(temporary.filePath(QStringLiteral("valid.sqlite"))));
    REQUIRE(!store.add(QStringLiteral(" \t\n　 ")));
    REQUIRE(!store.add(QString(201, QLatin1Char('a'))));
    REQUIRE(store.add(QString(200, QLatin1Char('a'))));
    QList<Record> records;
    REQUIRE(store.load(records));
    REQUIRE_EQUAL(records.size(), 1);

    const QString broken = temporary.filePath(QStringLiteral("not-a-database.sqlite"));
    QFile file(broken);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("this is not a SQLite file");
    file.close();
    REQUIRE(!store.open(broken));
    REQUIRE(!store.add(QStringLiteral("不能写入损坏数据库")));
    REQUIRE(!store.load(records));
    return true;
}

bool windowSaveAndReload()
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("window.sqlite"));
    {
        RecordStore store;
        REQUIRE(store.open(path));
        MainWindow window(store);
        REQUIRE(window.initialize());
        window.show();
        auto *input = window.findChild<QLineEdit *>(QStringLiteral("recordInput"));
        auto *save = window.findChild<QPushButton *>(QStringLiteral("saveButton"));
        auto *table = window.findChild<QTableWidget *>(QStringLiteral("recordsTable"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        REQUIRE(input && save && table && status);
        QTest::mouseClick(save, Qt::LeftButton);
        REQUIRE_EQUAL(table->rowCount(), 0);
        REQUIRE(status->text().contains(QStringLiteral("保存失败")));
        input->setText(QStringLiteral("我的第一条记录"));
        QTest::mouseClick(save, Qt::LeftButton);
        REQUIRE_EQUAL(table->rowCount(), 1);
        REQUIRE(input->text().isEmpty());
        REQUIRE_EQUAL(table->item(0, 1)->text(), QStringLiteral("我的第一条记录"));
        input->setText(QStringLiteral("按回车保存"));
        QTest::keyClick(input, Qt::Key_Return);
        REQUIRE_EQUAL(table->rowCount(), 2);
    }
    {
        RecordStore reopened;
        REQUIRE(reopened.open(path));
        MainWindow window(reopened);
        REQUIRE(window.initialize());
        auto *table = window.findChild<QTableWidget *>(QStringLiteral("recordsTable"));
        REQUIRE(table);
        REQUIRE_EQUAL(table->rowCount(), 2);
        REQUIRE_EQUAL(table->item(0, 1)->text(), QStringLiteral("按回车保存"));
    }
    return true;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const bool persistence = persistenceAndBoundText();
    const bool validation = validationAndFailedOpen();
    const bool window = windowSaveAndReload();
    QTextStream(stdout) << "Persistence: " << persistence << " Validation: " << validation
                        << " Window: " << window << Qt::endl;
    return persistence && validation && window ? 0 : 1;
}
