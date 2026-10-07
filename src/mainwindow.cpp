#include "mainwindow.h"
#include "recordstore.h"

#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

MainWindow::MainWindow(RecordStore &store)
    : store_(store), input_(new QLineEdit(this)), table_(new QTableWidget(this)),
      status_(new QLabel(this)), count_(new QLabel(this))
{
    setWindowTitle(QStringLiteral("Qt + SQLite 记录示例"));
    resize(780, 480);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
    auto *title = new QLabel(QStringLiteral("我的记录"), this);
    title->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 600;"));
    layout->addWidget(title);
    layout->addWidget(new QLabel(QStringLiteral("输入一条记录并保存，重新打开后仍可查看。"), this));

    auto *inputRow = new QHBoxLayout;
    input_->setObjectName(QStringLiteral("recordInput"));
    input_->setPlaceholderText(QStringLiteral("请输入记录（最多 200 个字符）"));
    input_->setMaxLength(200);
    input_->setMinimumHeight(34);
    auto *save = new QPushButton(QStringLiteral("保存记录"), this);
    save->setObjectName(QStringLiteral("saveButton"));
    save->setMinimumHeight(34);
    auto *reload = new QPushButton(QStringLiteral("刷新"), this);
    reload->setObjectName(QStringLiteral("refreshButton"));
    reload->setMinimumHeight(34);
    inputRow->addWidget(input_);
    inputRow->addWidget(save);
    inputRow->addWidget(reload);
    layout->addLayout(inputRow);

    table_->setObjectName(QStringLiteral("recordsTable"));
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({QStringLiteral("编号"), QStringLiteral("记录内容"),
                                       QStringLiteral("保存时间")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    layout->addWidget(table_, 1);
    layout->addWidget(count_);
    status_->setObjectName(QStringLiteral("statusLabel"));
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *path = new QLabel(QStringLiteral("数据库：%1").arg(store_.databasePath()), this);
    path->setTextFormat(Qt::PlainText);
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(path);

    connect(save, &QPushButton::clicked, this, &MainWindow::saveRecord);
    connect(input_, &QLineEdit::returnPressed, this, &MainWindow::saveRecord);
    connect(reload, &QPushButton::clicked, this, [this] {
        if (refresh())
            showStatus(QStringLiteral("列表已刷新。"));
    });
}

bool MainWindow::initialize()
{
    if (!refresh())
        return false;
    const QString version = store_.sqliteVersion();
    if (version.isEmpty()) {
        showStatus(QStringLiteral("读取 SQLite 版本失败：%1").arg(store_.lastError()), true);
        return false;
    }
    showStatus(QStringLiteral("数据库已连接 · Qt %1 · SQLite %2")
                   .arg(QString::fromLatin1(qVersion()), version));
    input_->setFocus();
    return true;
}

bool MainWindow::refresh()
{
    QList<Record> records;
    if (!store_.load(records)) {
        showStatus(QStringLiteral("读取列表失败：%1").arg(store_.lastError()), true);
        return false;
    }
    table_->setRowCount(static_cast<int>(records.size()));
    for (int row = 0; row < records.size(); ++row) {
        const Record &record = records.at(row);
        table_->setItem(row, 0, new QTableWidgetItem(QString::number(record.id)));
        auto *content = new QTableWidgetItem(record.content);
        content->setToolTip(record.content);
        table_->setItem(row, 1, content);
        const QString localTime = QDateTime::fromString(record.createdAt, Qt::ISODateWithMs)
                                      .toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        table_->setItem(row, 2, new QTableWidgetItem(localTime));
    }
    count_->setText(QStringLiteral("共 %1 条记录 · 最新记录在最上方").arg(records.size()));
    return true;
}

void MainWindow::saveRecord()
{
    if (!store_.add(input_->text())) {
        showStatus(QStringLiteral("保存失败：%1").arg(store_.lastError()), true);
        return;
    }
    input_->clear();
    if (refresh()) {
        showStatus(QStringLiteral("已保存。"));
    } else {
        showStatus(QStringLiteral("记录已保存，但列表读取失败：%1。请点击刷新。")
                       .arg(store_.lastError()), true);
    }
    input_->setFocus();
}

void MainWindow::showStatus(const QString &message, bool isError)
{
    status_->setTextFormat(Qt::PlainText);
    status_->setText(message);
    status_->setStyleSheet(isError ? QStringLiteral("color: #b42318;")
                                 : QStringLiteral("color: #187144;"));
}
