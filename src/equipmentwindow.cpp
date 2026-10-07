#include "equipmentwindow.h"
#include "equipmentcontroller.h"
#include "sceneview.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QKeySequence>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

namespace {
QLabel *addValue(QFormLayout *layout, const QString &caption, const QString &objectName, QWidget *parent)
{
    auto *value = new QLabel(parent);
    value->setObjectName(objectName);
    value->setTextFormat(Qt::PlainText);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    value->setWordWrap(true);
    layout->addRow(caption, value);
    return value;
}

QString number(double value) { return QString::number(value, 'g', 15); }
QString dimension(const std::optional<double> &value) { return value ? number(*value) : QString(); }
constexpr qint64 maximumFileSize = 5 * 1024 * 1024;
}

EquipmentWindow::EquipmentWindow(EquipmentController &controller)
    : controller_(controller), title_(new QLabel(this)), equipmentId_(new QLabel(this)),
      equipmentSelector_(new QComboBox(this)), search_(new QLineEdit(this)),
      partsList_(new QListWidget(this)), scene_(new SceneView(this)),
      prompt_(new QLabel(QStringLiteral("请选择零件"), this)), propertyDetails_(new QWidget(this)),
      count_(new QLabel(this)), status_(new QLabel(this))
{
    setWindowTitle(QStringLiteral("开目业务模拟 · 设备模型查看器"));
    resize(1320, 820);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);
    title_->setObjectName(QStringLiteral("equipmentTitle"));
    title_->setTextFormat(Qt::PlainText);
    title_->setText(QStringLiteral("设备模型查看器"));
    title_->setStyleSheet(QStringLiteral("font-size: 25px; font-weight: 600; color: #16324f;"));
    equipmentId_->setObjectName(QStringLiteral("equipmentId"));
    equipmentId_->setTextFormat(Qt::PlainText);
    layout->addWidget(title_);
    auto *toolbar = new QHBoxLayout;
    equipmentSelector_->setObjectName(QStringLiteral("equipmentSelector"));
    equipmentSelector_->setMinimumWidth(230);
    toolbar->addWidget(new QLabel(QStringLiteral("当前设备"), this));
    toolbar->addWidget(equipmentSelector_);
    auto *importButton = new QPushButton(QStringLiteral("导入设备 JSON"), this);
    importButton->setObjectName(QStringLiteral("importButton"));
    toolbar->addWidget(importButton);
    toolbar->addStretch();
    toolbar->addWidget(equipmentId_);
    layout->addLayout(toolbar);

    auto *body = new QSplitter(Qt::Horizontal, this);
    auto *listGroup = new QGroupBox(QStringLiteral("零件清单"), body);
    auto *listLayout = new QVBoxLayout(listGroup);
    search_->setObjectName(QStringLiteral("partSearch"));
    search_->setPlaceholderText(QStringLiteral("搜索名称或零件 ID"));
    search_->setClearButtonEnabled(true);
    listLayout->addWidget(search_);
    partsList_->setObjectName(QStringLiteral("partsList"));
    partsList_->setSelectionMode(QAbstractItemView::SingleSelection);
    listLayout->addWidget(partsList_);
    count_->setObjectName(QStringLiteral("partCount"));
    count_->setText(QStringLiteral("设备尚未加载"));
    listLayout->addWidget(count_);

    auto *sceneGroup = new QGroupBox(QStringLiteral("三维布局"), body);
    auto *sceneLayout = new QVBoxLayout(sceneGroup);
    auto *sceneToolbar = new QHBoxLayout;
    hideButton_ = new QPushButton(QStringLiteral("隐藏选中"), sceneGroup);
    hideButton_->setObjectName(QStringLiteral("hideSelectedButton"));
    isolateButton_ = new QPushButton(QStringLiteral("隔离查看"), sceneGroup);
    isolateButton_->setObjectName(QStringLiteral("isolateSelectedButton"));
    auto *showAllButton = new QPushButton(QStringLiteral("显示全部"), sceneGroup);
    showAllButton->setObjectName(QStringLiteral("showAllButton"));
    auto *fitButton = new QPushButton(QStringLiteral("查看全部"), sceneGroup);
    fitButton->setObjectName(QStringLiteral("fitAllButton"));
    for (auto *button : {hideButton_, isolateButton_, showAllButton, fitButton})
        sceneToolbar->addWidget(button);
    sceneLayout->addLayout(sceneToolbar);
    scene_->setObjectName(QStringLiteral("sceneView"));
    sceneLayout->addWidget(scene_, 1);
    auto *hint = new QLabel(QStringLiteral("拖动旋转 · 滚轮缩放 · 点击模型选择零件"), sceneGroup);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #52677b;"));
    sceneLayout->addWidget(hint);

    auto *propertiesGroup = new QGroupBox(QStringLiteral("只读属性"), body);
    auto *propertiesLayout = new QVBoxLayout(propertiesGroup);
    prompt_->setObjectName(QStringLiteral("selectionPrompt"));
    propertiesLayout->addWidget(prompt_);
    auto *scroll = new QScrollArea(propertiesGroup);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    propertyDetails_->setObjectName(QStringLiteral("propertyDetails"));
    auto *detailsLayout = new QVBoxLayout(propertyDetails_);
    detailsLayout->setContentsMargins(0, 0, 6, 0);
    auto *basicLayout = new QFormLayout;
    partId_ = addValue(basicLayout, QStringLiteral("零件 ID"), QStringLiteral("partId"), propertyDetails_);
    partName_ = addValue(basicLayout, QStringLiteral("名称"), QStringLiteral("partName"), propertyDetails_);
    material_ = addValue(basicLayout, QStringLiteral("材料"), QStringLiteral("material"), propertyDetails_);
    shape_ = addValue(basicLayout, QStringLiteral("形状"), QStringLiteral("shape"), propertyDetails_);
    detailsLayout->addLayout(basicLayout);
    auto *positionGroup = new QGroupBox(QStringLiteral("中心位置（毫米）"), propertyDetails_);
    positionGroup->setObjectName(QStringLiteral("positionGroup"));
    auto *positionLayout = new QFormLayout(positionGroup);
    positionX_ = addValue(positionLayout, QStringLiteral("X"), QStringLiteral("positionX"), positionGroup);
    positionY_ = addValue(positionLayout, QStringLiteral("Y"), QStringLiteral("positionY"), positionGroup);
    positionZ_ = addValue(positionLayout, QStringLiteral("Z"), QStringLiteral("positionZ"), positionGroup);
    detailsLayout->addWidget(positionGroup);
    boxDimensions_ = new QGroupBox(QStringLiteral("长方体尺寸（毫米）"), propertyDetails_);
    boxDimensions_->setObjectName(QStringLiteral("boxDimensions"));
    auto *boxLayout = new QFormLayout(boxDimensions_);
    sizeX_ = addValue(boxLayout, QStringLiteral("X"), QStringLiteral("sizeX"), boxDimensions_);
    sizeY_ = addValue(boxLayout, QStringLiteral("Y"), QStringLiteral("sizeY"), boxDimensions_);
    sizeZ_ = addValue(boxLayout, QStringLiteral("Z"), QStringLiteral("sizeZ"), boxDimensions_);
    detailsLayout->addWidget(boxDimensions_);
    cylinderDimensions_ = new QGroupBox(QStringLiteral("圆柱尺寸（毫米）"), propertyDetails_);
    cylinderDimensions_->setObjectName(QStringLiteral("cylinderDimensions"));
    auto *cylinderLayout = new QFormLayout(cylinderDimensions_);
    radius_ = addValue(cylinderLayout, QStringLiteral("半径"), QStringLiteral("radius"), cylinderDimensions_);
    length_ = addValue(cylinderLayout, QStringLiteral("长度"), QStringLiteral("length"), cylinderDimensions_);
    detailsLayout->addWidget(cylinderDimensions_);
    detailsLayout->addStretch();
    scroll->setWidget(propertyDetails_);
    propertiesLayout->addWidget(scroll, 1);
    clearSelectionButton_ = new QPushButton(QStringLiteral("清除选择"), propertiesGroup);
    clearSelectionButton_->setObjectName(QStringLiteral("clearSelectionButton"));
    propertiesLayout->addWidget(clearSelectionButton_);
    body->setSizes({230, 720, 300});
    body->setStretchFactor(1, 1);
    layout->addWidget(body, 1);
    status_->setObjectName(QStringLiteral("equipmentStatus"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    displayProperties();

    connect(partsList_, &QListWidget::itemSelectionChanged, this, &EquipmentWindow::selectFromList);
    connect(clearSelectionButton_, &QPushButton::clicked, this, &EquipmentWindow::clearSelection);
    // 窗口快捷键覆盖各控件焦点，只清选择，不修改搜索或显隐。
    auto *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut->setContext(Qt::WindowShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, &EquipmentWindow::clearSelection);
    connect(search_, &QLineEdit::textChanged, this, [this] {
        rebuildList();
        displayProperties();
        syncScene();
    });
    connect(equipmentSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0)
            return;
        const QString id = equipmentSelector_->itemData(index).toString();
        if (!loadEquipment(id) && controller_.equipment()) {
            const QSignalBlocker blocker(equipmentSelector_);
            equipmentSelector_->setCurrentIndex(equipmentSelector_->findData(controller_.equipment()->id));
        }
    });
    connect(importButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入设备资料"),
            QString(), QStringLiteral("设备资料 (*.json)"));
        if (!path.isEmpty())
            importFile(path);
    });
    connect(hideButton_, &QPushButton::clicked, this, [this] {
        if (!controller_.hideSelected()) {
            showStatus(controller_.lastError(), true);
            return;
        }
        rebuildList();
        syncScene();
        showStatus(QStringLiteral("选中零件已隐藏；属性仍可查看。"));
    });
    connect(isolateButton_, &QPushButton::clicked, this, [this] {
        if (!controller_.isolateSelected()) {
            showStatus(controller_.lastError(), true);
            return;
        }
        rebuildList();
        syncScene();
        showStatus(QStringLiteral("当前仅显示选中零件，点击“显示全部”恢复。"));
    });
    connect(showAllButton, &QPushButton::clicked, this, [this] {
        controller_.showAll();
        rebuildList();
        syncScene();
        showStatus(QStringLiteral("全部零件已恢复显示。"));
    });
    connect(fitButton, &QPushButton::clicked, this, [this] { scene_->fitAll(); });
    scene_->onPartPicked = [this](const QString &id) {
        // 模型拾取优先；目标被过滤时清搜索以恢复对应清单高亮。
        const bool listed = [&] {
            for (int row = 0; row < partsList_->count(); ++row) {
                if (partsList_->item(row)->data(Qt::UserRole).toString() == id)
                    return true;
            }
            return id.isEmpty();
        }();
        if (!controller_.selectPart(id)) {
            showStatus(controller_.lastError(), true);
            return;
        }
        if (!listed) {
            const QSignalBlocker blocker(search_);
            search_->clear();
            rebuildList();
        }
        restoreSelection();
        displayProperties();
        syncScene();
        showStatus(QString());
    };
}

bool EquipmentWindow::refreshEquipmentChoices()
{
    QList<Equipment> equipment;
    if (!controller_.availableEquipment(equipment)) {
        showStatus(controller_.lastError(), true);
        return false;
    }
    const QSignalBlocker blocker(equipmentSelector_);
    equipmentSelector_->clear();
    for (const Equipment &item : equipment)
        equipmentSelector_->addItem(QStringLiteral("%1 (%2)").arg(item.name, item.id), item.id);
    equipmentSelector_->setCurrentIndex(controller_.equipment()
        ? equipmentSelector_->findData(controller_.equipment()->id) : -1);
    return true;
}

void EquipmentWindow::displayLoadedEquipment()
{
    title_->setText(controller_.equipment()->name);
    equipmentId_->setText(QStringLiteral("设备 ID：%1").arg(controller_.equipment()->id));
    const QSignalBlocker blocker(search_);
    search_->clear();
    rebuildList();
    displayProperties();
    scene_->setParts(controller_.parts());
    syncScene();
}

bool EquipmentWindow::loadEquipment(const QString &equipmentId)
{
    if (!controller_.loadEquipment(equipmentId)) {
        showStatus(controller_.lastError(), true);
        return false;
    }
    displayLoadedEquipment();
    if (!refreshEquipmentChoices())
        return true; // 资料已完整加载，设备列表故障单独提示，不谎称回滚加载。
    showStatus(QStringLiteral("设备资料已加载。"));
    return true;
}

bool EquipmentWindow::importData(const QByteArray &json)
{
    if (json.size() > maximumFileSize) {
        showStatus(QStringLiteral("文件超过 5 MiB，请拆分设备资料后再导入。"), true);
        return false;
    }
    if (!controller_.importJson(json)) {
        const QString error = controller_.lastError();
        refreshEquipmentChoices();
        showStatus(error, true);
        return false;
    }
    displayLoadedEquipment();
    if (!refreshEquipmentChoices())
        return true; // 保存与打开已成功，保留设备列表读取故障的提示。
    showStatus(QStringLiteral("设备已导入并保存，重新启动后可从设备列表打开。"));
    return true;
}

bool EquipmentWindow::importFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        showStatus(QStringLiteral("无法打开设备文件：%1").arg(file.errorString()), true);
        return false;
    }
    const QByteArray json = file.read(maximumFileSize + 1);
    if (file.error() != QFileDevice::NoError) {
        showStatus(QStringLiteral("读取设备文件失败：%1").arg(file.errorString()), true);
        return false;
    }
    return importData(json);
}

void EquipmentWindow::rebuildList()
{
    const QSignalBlocker blocker(partsList_);
    partsList_->clear();
    const QString term = search_->text().trimmed();
    bool selectionListed = controller_.selectedPartId().isEmpty();
    for (const Part &part : controller_.parts()) {
        if (!term.isEmpty() && !part.name.contains(term, Qt::CaseInsensitive)
            && !part.id.contains(term, Qt::CaseInsensitive))
            continue;
        const bool hidden = controller_.hiddenPartIds().contains(part.id);
        const QString text = QStringLiteral("%1 (%2)%3").arg(part.name, part.id,
            hidden ? QStringLiteral(" · 已隐藏") : QString());
        auto *item = new QListWidgetItem(text, partsList_);
        item->setData(Qt::UserRole, part.id);
        if (hidden) {
            item->setForeground(QColor(QStringLiteral("#778899")));
            QFont font = item->font();
            font.setItalic(true);
            item->setFont(font);
        }
        if (part.id == controller_.selectedPartId()) {
            partsList_->setCurrentItem(item);
            selectionListed = true;
        }
    }
    if (!selectionListed)
        controller_.selectPart(QString());
    if (controller_.selectedPartId().isEmpty())
        partsList_->setCurrentRow(-1);
    count_->setText(term.isEmpty()
        ? QStringLiteral("%1 个零件").arg(controller_.parts().size())
        : QStringLiteral("显示 %1 / %2 个零件").arg(partsList_->count()).arg(controller_.parts().size()));
}

void EquipmentWindow::selectFromList()
{
    const auto selected = partsList_->selectedItems();
    const QString id = selected.isEmpty() ? QString() : selected.first()->data(Qt::UserRole).toString();
    if (!controller_.selectPart(id)) {
        showStatus(controller_.lastError(), true);
        restoreSelection();
        return;
    }
    displayProperties();
    syncScene();
    showStatus(QString());
}

void EquipmentWindow::restoreSelection()
{
    const QSignalBlocker blocker(partsList_);
    partsList_->clearSelection();
    partsList_->setCurrentRow(-1);
    for (int row = 0; row < partsList_->count(); ++row) {
        auto *item = partsList_->item(row);
        if (item->data(Qt::UserRole).toString() == controller_.selectedPartId()) {
            partsList_->setCurrentItem(item);
            break;
        }
    }
}

void EquipmentWindow::clearSelection()
{
    controller_.selectPart(QString());
    restoreSelection();
    displayProperties();
    syncScene();
    showStatus(QString());
}

void EquipmentWindow::syncScene()
{
    scene_->setSelectedPart(controller_.selectedPartId());
    scene_->setHiddenParts(controller_.hiddenPartIds());
}

void EquipmentWindow::displayProperties()
{
    // 同时清空被隐藏的字段，保证形状切换和清除选择不会残留旧资料。
    for (QLabel *label : {partId_, partName_, material_, shape_, positionX_, positionY_, positionZ_,
                         sizeX_, sizeY_, sizeZ_, radius_, length_})
        label->clear();
    boxDimensions_->hide();
    cylinderDimensions_->hide();
    const Part *part = controller_.selectedPart();
    prompt_->setVisible(!part);
    propertyDetails_->setVisible(part != nullptr);
    hideButton_->setEnabled(part != nullptr);
    isolateButton_->setEnabled(part != nullptr);
    if (!part)
        return;
    partId_->setText(part->id);
    partName_->setText(part->name);
    material_->setText(part->material);
    positionX_->setText(number(part->positionX));
    positionY_->setText(number(part->positionY));
    positionZ_->setText(number(part->positionZ));
    if (part->shape == QStringLiteral("box")) {
        shape_->setText(QStringLiteral("长方体"));
        sizeX_->setText(dimension(part->sizeX));
        sizeY_->setText(dimension(part->sizeY));
        sizeZ_->setText(dimension(part->sizeZ));
        boxDimensions_->show();
    } else if (part->shape == QStringLiteral("cylinder")) {
        shape_->setText(QStringLiteral("圆柱"));
        radius_->setText(dimension(part->radius));
        length_->setText(dimension(part->length));
        cylinderDimensions_->show();
    }
}

void EquipmentWindow::showStatus(const QString &message, bool isError)
{
    status_->setText(message);
    status_->setStyleSheet(isError ? QStringLiteral("color: #b42318;") : QStringLiteral("color: #42647d;"));
}
