#include "equipmentwindow.h"
#include "equipmentcontroller.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
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

QString number(double value)
{
    return QString::number(value, 'g', 15);
}

QString dimension(const std::optional<double> &value)
{
    return value ? number(*value) : QString();
}
}

EquipmentWindow::EquipmentWindow(EquipmentController &controller)
    : controller_(controller), title_(new QLabel(this)), equipmentId_(new QLabel(this)),
      partsList_(new QListWidget(this)), prompt_(new QLabel(QStringLiteral("请选择零件"), this)),
      propertyDetails_(new QWidget(this)), count_(new QLabel(this)),
      status_(new QLabel(this))
{
    setWindowTitle(QStringLiteral("设备资料查看"));
    resize(900, 660);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
    title_->setObjectName(QStringLiteral("equipmentTitle"));
    title_->setTextFormat(Qt::PlainText);
    title_->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 600;"));
    equipmentId_->setObjectName(QStringLiteral("equipmentId"));
    equipmentId_->setTextFormat(Qt::PlainText);
    layout->addWidget(title_);
    layout->addWidget(equipmentId_);

    auto *body = new QHBoxLayout;
    auto *listGroup = new QGroupBox(QStringLiteral("零件清单"), this);
    auto *listLayout = new QVBoxLayout(listGroup);
    partsList_->setObjectName(QStringLiteral("partsList"));
    partsList_->setSelectionMode(QAbstractItemView::SingleSelection);
    listLayout->addWidget(partsList_);
    auto *propertiesGroup = new QGroupBox(QStringLiteral("只读属性"), this);
    auto *propertiesLayout = new QVBoxLayout(propertiesGroup);
    prompt_->setObjectName(QStringLiteral("selectionPrompt"));
    propertiesLayout->addWidget(prompt_);
    propertyDetails_->setObjectName(QStringLiteral("propertyDetails"));
    auto *detailsLayout = new QVBoxLayout(propertyDetails_);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
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
    propertiesLayout->addWidget(propertyDetails_);
    propertiesLayout->addStretch();
    clearSelectionButton_ = new QPushButton(QStringLiteral("清除选择"), propertiesGroup);
    clearSelectionButton_->setObjectName(QStringLiteral("clearSelectionButton"));
    propertiesLayout->addWidget(clearSelectionButton_);
    body->addWidget(listGroup, 1);
    body->addWidget(propertiesGroup, 2);
    layout->addLayout(body, 1);
    count_->setObjectName(QStringLiteral("partCount"));
    count_->setText(QStringLiteral("设备尚未加载"));
    layout->addWidget(count_);
    status_->setObjectName(QStringLiteral("equipmentStatus"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    displayProperties();

    connect(partsList_, &QListWidget::itemSelectionChanged, this, &EquipmentWindow::selectFromList);
    connect(clearSelectionButton_, &QPushButton::clicked, this, &EquipmentWindow::clearSelection);
}

bool EquipmentWindow::loadEquipment(const QString &equipmentId)
{
    if (!controller_.loadEquipment(equipmentId)) {
        showStatus(controller_.lastError(), true);
        return false;
    }
    const QSignalBlocker blocker(partsList_);
    title_->setText(controller_.equipment()->name);
    equipmentId_->setText(QStringLiteral("设备 ID：%1").arg(controller_.equipment()->id));
    partsList_->clear();
    for (const Part &part : controller_.parts()) {
        auto *item = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(part.name, part.id), partsList_);
        item->setData(Qt::UserRole, part.id);
    }
    partsList_->setCurrentRow(-1);
    count_->setText(QStringLiteral("%1 个零件").arg(controller_.parts().size()));
    displayProperties();
    showStatus(QStringLiteral("设备资料已加载。"));
    return true;
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
    const QSignalBlocker blocker(partsList_);
    partsList_->clearSelection();
    partsList_->setCurrentRow(-1);
    controller_.selectPart(QString());
    displayProperties();
    showStatus(QString());
}

void EquipmentWindow::displayProperties()
{
    // 每次先清空所有值，包括将被隐藏的形状字段，避免旧资料残留。
    for (QLabel *label : {partId_, partName_, material_, shape_, positionX_, positionY_, positionZ_,
                         sizeX_, sizeY_, sizeZ_, radius_, length_})
        label->clear();
    boxDimensions_->hide();
    cylinderDimensions_->hide();
    // 指针只在本次同步更新中使用，不跨重新加载保存。
    const Part *part = controller_.selectedPart();
    prompt_->setVisible(!part);
    propertyDetails_->setVisible(part != nullptr);
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
    status_->setStyleSheet(isError ? QStringLiteral("color: #b42318;") : QString());
}
