#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QTableWidget;
class RecordStore;

class MainWindow : public QWidget {
public:
    explicit MainWindow(RecordStore &store);
    bool initialize();

private:
    bool refresh();
    void saveRecord();
    void showStatus(const QString &message, bool isError = false);

    RecordStore &store_;
    QLineEdit *input_;
    QTableWidget *table_;
    QLabel *status_;
    QLabel *count_;
};
