#pragma once

#include <QDialog>
#include <functional>

class ApiClient;
class EquipmentController;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

/** 团队共享库窗口，通过HTTP发布/下载，不持有数据库凭证。 */
class CollaborationDialog : public QDialog {
public:
    /** 控制器由主窗口持有；对话框关闭后取消自己的网络请求。 */
    CollaborationDialog(EquipmentController &controller, QWidget *parent = nullptr);
    /** 成功下载到本地后通知主窗口同步模型；不跨主窗口生命周期使用。 */
    std::function<void()> onLocalImported;

private:
    bool configure();
    void refresh();
    void publish();
    void download();
    void setBusy(bool busy);
    void status(const QString &text, bool error = false);

    EquipmentController &controller_;
    ApiClient *client_;
    QLineEdit *address_;
    QLineEdit *token_;
    QComboBox *equipment_;
    QPushButton *refresh_;
    QPushButton *publish_;
    QPushButton *download_;
    QLabel *status_;
};
