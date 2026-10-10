#pragma once

#include <QDialog>
#include <QJsonObject>
#include "apiclient.h"
#include <functional>

class ApiClient;
class EquipmentController;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QListWidget;
class QPlainTextEdit;
class QCloseEvent;

/** 团队共享库窗口，通过HTTP发布/下载，不持有数据库凭证。 */
class CollaborationDialog : public QDialog {
public:
    /** 控制器由主窗口持有；对话框关闭后取消自己的网络请求。 */
    CollaborationDialog(EquipmentController &controller, QWidget *parent = nullptr);
    /** 成功下载到本地后通知主窗口同步模型；不跨主窗口生命周期使用。 */
    std::function<void()> onLocalImported;
    /** 未提交草稿必须显式保存/放弃，关闭请求只取消等待并保留窗口。 */
    void reject() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void request(const QByteArray &method, const QString &path, const QJsonValue &body,
                 std::function<void(ApiResult)> callback);
    bool configure();
    void refresh();
    void publish();
    void download();
    void setBusy(bool busy);
    void status(const QString &text, bool error = false);
    bool hasScope() const;
    void loadPartScope();
    void refreshNotes();
    void selectNote();
    void newNote();
    void discardDraft();
    void saveNote();
    void displayNote(const QJsonObject &note, bool preserveDraft);
    void noteStatus(const QString &text, bool error = false);

    EquipmentController &controller_;
    ApiClient *client_;
    QLineEdit *address_;
    QLineEdit *token_;
    QComboBox *equipment_;
    QPushButton *refresh_;
    QPushButton *publish_;
    QPushButton *download_;
    QLabel *status_;
    QComboBox *partScope_;
    QLineEdit *author_;
    QListWidget *notes_;
    QPlainTextEdit *saved_;
    QPlainTextEdit *draft_;
    QLabel *noteMeta_;
    QLabel *noteStatus_;
    QPushButton *noteNew_;
    QPushButton *noteSave_;
    QPushButton *noteRefresh_;
    QPushButton *noteDiscard_;
    QString loadedEquipmentId_;
    QString loadedPartId_;
    QJsonObject currentNote_;
    QString createRequestId_;
    QByteArray createPayload_;
    bool dirty_ = false;
    bool busy_ = false;
    quint64 operation_ = 0;
};
