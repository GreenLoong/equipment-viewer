#include "collaborationdialog.h"
#include "apiclient.h"
#include "equipmentcontroller.h"

#include <QComboBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDir>

namespace {
QJsonObject deviceDocument(const Equipment &equipment, const QList<Part> &parts)
{
    QJsonArray items;
    for (const Part &part : parts) {
        const QJsonArray dimensions = part.shape == QStringLiteral("box")
            ? QJsonArray{part.sizeX.value_or(0), part.sizeY.value_or(0), part.sizeZ.value_or(0)}
            : QJsonArray{part.radius.value_or(0), part.length.value_or(0)};
        items.append(QJsonObject{{"id", part.id}, {"name", part.name}, {"material", part.material},
            {"shape", part.shape}, {"dimensions", dimensions},
            {"position", QJsonArray{part.positionX, part.positionY, part.positionZ}}});
    }
    return {{"equipmentId", equipment.id}, {"equipmentName", equipment.name}, {"parts", items}};
}
}

CollaborationDialog::CollaborationDialog(EquipmentController &controller, QWidget *parent)
    : QDialog(parent), controller_(controller), client_(new ApiClient(this)),
      address_(new QLineEdit(QStringLiteral("http://127.0.0.1:8765"), this)),
      token_(new QLineEdit(this)), equipment_(new QComboBox(this)),
      refresh_(new QPushButton(QStringLiteral("刷新共享库"), this)),
      publish_(new QPushButton(QStringLiteral("发布当前设备"), this)),
      download_(new QPushButton(QStringLiteral("下载到本地"), this)), status_(new QLabel(this))
{
    setWindowTitle(QStringLiteral("团队共享设备库"));
    setWindowModality(Qt::WindowModal);
    resize(620, 330);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    address_->setObjectName(QStringLiteral("sharedAddress"));
    token_->setObjectName(QStringLiteral("sharedToken"));
    token_->setEchoMode(QLineEdit::Password);
    equipment_->setObjectName(QStringLiteral("sharedEquipment"));
    form->addRow(QStringLiteral("服务地址"), address_);
    form->addRow(QStringLiteral("访问令牌"), token_);
    form->addRow(QStringLiteral("共享设备"), equipment_);
    layout->addLayout(form);
    auto *actions = new QHBoxLayout;
    refresh_->setObjectName(QStringLiteral("sharedRefreshButton"));
    publish_->setObjectName(QStringLiteral("sharedPublishButton"));
    download_->setObjectName(QStringLiteral("sharedDownloadButton"));
    actions->addWidget(refresh_); actions->addWidget(publish_); actions->addWidget(download_);
    layout->addLayout(actions);
    auto *hint = new QLabel(QStringLiteral("共享资料保存于团队服务；下载后可离线查看。本地同ID资料不会被覆盖。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    status_->setObjectName(QStringLiteral("sharedStatus"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    layout->addStretch();
    auto *close = new QPushButton(QStringLiteral("关闭"), this);
    layout->addWidget(close);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(this, &QDialog::finished, this, [this] { client_->cancelAll(); });
    connect(refresh_, &QPushButton::clicked, this, &CollaborationDialog::refresh);
    connect(publish_, &QPushButton::clicked, this, &CollaborationDialog::publish);
    connect(download_, &QPushButton::clicked, this, &CollaborationDialog::download);
    connect(equipment_, &QComboBox::currentIndexChanged, this, [this] { download_->setEnabled(equipment_->count() > 0); });
    const QString config = qEnvironmentVariable("EQUIPMENT_SERVICE_CONFIG",
        QDir::current().filePath(QStringLiteral("api/runtime/client.local.json")));
    QFile file(config);
    if (file.open(QIODevice::ReadOnly)) {
        const auto settings = QJsonDocument::fromJson(file.read(16384)).object();
        const QString host = settings.value(QStringLiteral("host")).toString(QStringLiteral("127.0.0.1"));
        const int port = settings.value(QStringLiteral("port")).toInt(8765);
        address_->setText(QStringLiteral("http://%1:%2").arg(host).arg(port));
        token_->setText(settings.value(QStringLiteral("token")).toString());
    }
    setBusy(false);
}

bool CollaborationDialog::configure()
{
    if (client_->configure(QUrl(address_->text().trimmed()), token_->text()))
        return true;
    status(QStringLiteral("请输入有效HTTP(S)地址和访问令牌。"), true);
    return false;
}

void CollaborationDialog::setBusy(bool busy)
{
    address_->setEnabled(!busy); token_->setEnabled(!busy); equipment_->setEnabled(!busy);
    refresh_->setEnabled(!busy);
    publish_->setEnabled(!busy && controller_.equipment().has_value());
    download_->setEnabled(!busy && equipment_->count() > 0);
}

void CollaborationDialog::status(const QString &text, bool error)
{
    status_->setText(text);
    status_->setStyleSheet(error ? QStringLiteral("color:#b42318;") : QString());
}

void CollaborationDialog::refresh()
{
    if (!configure()) return;
    setBusy(true);
    status(QStringLiteral("正在读取共享库…"));
    client_->request("GET", QStringLiteral("/api/equipment"), {}, [this](ApiResult result) {
        if (result.ok() && result.data.isArray()) {
            equipment_->clear();
            for (const auto &item : result.data.toArray()) {
                const auto device = item.toObject();
                const QString id = device.value(QStringLiteral("id")).toString();
                equipment_->addItem(QStringLiteral("%1 (%2)").arg(device.value(QStringLiteral("name")).toString(), id), id);
            }
            status(QStringLiteral("已连接共享库，共 %1 台设备。" ).arg(equipment_->count()));
        } else
            status(result.error.isEmpty() ? QStringLiteral("共享库返回了无效设备列表。") : result.error, true);
        setBusy(false);
    });
}

void CollaborationDialog::publish()
{
    if (!controller_.equipment() || !configure()) return;
    const auto document = deviceDocument(*controller_.equipment(), controller_.parts());
    setBusy(true);
    status(QStringLiteral("正在发布当前设备…"));
    client_->request("POST", QStringLiteral("/api/equipment"), document, [this](ApiResult result) {
        setBusy(false);
        status(result.ok() ? QStringLiteral("设备已发布，其他客户端可刷新共享库查看。") : result.error, !result.ok());
    });
}

void CollaborationDialog::download()
{
    if (equipment_->currentIndex() < 0 || !configure()) return;
    const QString id = equipment_->currentData().toString();
    const QString target = QStringLiteral("/api/equipment?id=") + QString::fromLatin1(QUrl::toPercentEncoding(id));
    setBusy(true);
    status(QStringLiteral("正在下载设备…"));
    client_->request("GET", target, {}, [this, id](ApiResult result) {
        if (result.ok() && result.data.isObject()) {
            if (result.data.toObject().value(QStringLiteral("equipmentId")).toString() != id) {
                status(QStringLiteral("共享服务返回了不同设备，未导入资料。"), true);
                setBusy(false);
                return;
            }
            const QByteArray json = QJsonDocument(result.data.toObject()).toJson(QJsonDocument::Compact);
            if (controller_.importJson(json)) {
                if (onLocalImported) onLocalImported();
                status(QStringLiteral("已下载并保存到本地，可以离线查看。"));
            } else
                status(QStringLiteral("下载资料未覆盖本地：%1").arg(controller_.lastError()), true);
        } else
            status(result.error.isEmpty() ? QStringLiteral("共享资料格式不正确。") : result.error, true);
        setBusy(false);
    });
}
