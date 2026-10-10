#include "equipmentwindow.h"
#include "equipmentcontroller.h"
#include "sceneview.h"
#include "collaborationdialog.h"
#include "cadimportprocess.h"
#include "numericutil.h"

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
#include <QTimer>
#include <QKeySequence>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QCloseEvent>
#include <QCheckBox>
#include <QColorDialog>
#include <QGridLayout>
#include <QSlider>
#include <QIcon>
#include <QPixmap>
#include <QHash>
#include <QPalette>

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

QString dimension(const std::optional<double> &value)
{
    return value ? NumericUtil::numberText(*value) : QString();
}
constexpr qint64 maximumFileSize = 5 * 1024 * 1024;
}

EquipmentWindow::EquipmentWindow(EquipmentController &controller)
    : controller_(controller), title_(new QLabel(this)), equipmentId_(new QLabel(this)),
      equipmentSelector_(new QComboBox(this)), search_(new QLineEdit(this)),
      partsList_(new QListWidget(this)), scene_(new SceneView(this)),
      prompt_(new QLabel(QStringLiteral("请选择零件"), this)), propertyDetails_(new QWidget(this)),
      count_(new QLabel(this)), status_(new QLabel(this)), cadImport_(new CadImportProcess(this))
{
    setWindowTitle(QStringLiteral("开目业务模拟 · 设备模型查看器 1.1.1"));
    resize(1440, 900);
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
    auto *importButton = new QPushButton(QStringLiteral("导入 JSON / STEP"), this);
    importButton_ = importButton;
    importButton->setObjectName(QStringLiteral("importButton"));
    toolbar->addWidget(importButton);
    cadCancelButton_ = new QPushButton(QStringLiteral("取消CAD导入"), this);
    cadCancelButton_->setObjectName(QStringLiteral("cadCancelButton"));
    cadCancelButton_->setEnabled(false);
    toolbar->addWidget(cadCancelButton_);
    connect(cadCancelButton_, &QPushButton::clicked, this, [this] { cancelCadImport(); });
    auto *sharedButton = new QPushButton(QStringLiteral("团队共享库"), this);
    sharedButton_ = sharedButton;
    sharedButton->setObjectName(QStringLiteral("sharedLibraryButton"));
    toolbar->addWidget(sharedButton);
    connect(sharedButton, &QPushButton::clicked, this, [this] {
        auto *dialog = new CollaborationDialog(controller_, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->onLocalImported = [this] {
            displayLoadedEquipment();
            refreshEquipmentChoices();
        };
        dialog->open();
    });
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

    auto *sceneGroup = new QGroupBox(QStringLiteral("装配工作台 · 原生三维"), body);
    auto *sceneLayout = new QVBoxLayout(sceneGroup);
    createAssemblyControls(sceneLayout, sceneGroup);
    auto *sceneToolbar = new QHBoxLayout;
    hideButton_ = new QPushButton(QStringLiteral("隐藏选中"), sceneGroup);
    hideButton_->setObjectName(QStringLiteral("hideSelectedButton"));
    isolateButton_ = new QPushButton(QStringLiteral("单件展示"), sceneGroup);
    isolateButton_->setObjectName(QStringLiteral("isolateSelectedButton"));
    auto *showAllButton = new QPushButton(QStringLiteral("显示全部"), sceneGroup);
    showAllButton->setObjectName(QStringLiteral("showAllButton"));
    auto *fitButton = new QPushButton(QStringLiteral("视角回正"), sceneGroup);
    fitButton->setObjectName(QStringLiteral("fitAllButton"));
    fitButton->setToolTip(QStringLiteral("恢复默认观察方向与缩放"));
    for (auto *button : {hideButton_, isolateButton_, showAllButton, fitButton})
        sceneToolbar->addWidget(button);
    sceneLayout->addLayout(sceneToolbar);
    auto *fitVisibleButton = new QPushButton(QStringLiteral("适配全部"), sceneGroup);
    fitVisibleButton->setObjectName(QStringLiteral("fitVisibleButton"));
    sceneToolbar->addWidget(fitVisibleButton);
    focusButton_ = new QPushButton(QStringLiteral("聚焦选中"), sceneGroup);
    focusButton_->setObjectName(QStringLiteral("focusSelectedButton"));
    sceneToolbar->addWidget(focusButton_);
    scene_->setObjectName(QStringLiteral("sceneView"));
    sceneLayout->addWidget(scene_, 1);
    auto *hint = new QLabel(QStringLiteral("左键拖动旋转 · 右键拖动平移 · 滚轮/中键缩放 · 短点选中 · 双击空白只清选择，保留单件展示\n单位mm / Z向上；展开与拉远只改变显示位置，不验证机械运动或碰撞约束。"), sceneGroup);
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
    cadSource_ = addValue(basicLayout, QStringLiteral("CAD来源 / SHA256"), QStringLiteral("cadSource"), propertyDetails_);
    detailsLayout->addLayout(basicLayout);
    auto *positionGroup = new QGroupBox(QStringLiteral("原始中心位置（毫米）"), propertyDetails_);
    positionGroup->setObjectName(QStringLiteral("positionGroup"));
    auto *positionLayout = new QFormLayout(positionGroup);
    positionX_ = addValue(positionLayout, QStringLiteral("X"), QStringLiteral("positionX"), positionGroup);
    positionY_ = addValue(positionLayout, QStringLiteral("Y"), QStringLiteral("positionY"), positionGroup);
    positionZ_ = addValue(positionLayout, QStringLiteral("Z"), QStringLiteral("positionZ"), positionGroup);
    detailsLayout->addWidget(positionGroup);
    auto *displayPositionGroup = new QGroupBox(QStringLiteral("当前显示位置（毫米，不保存到资料）"), propertyDetails_);
    displayPositionGroup->setObjectName(QStringLiteral("displayPositionGroup"));
    auto *displayPositionLayout = new QFormLayout(displayPositionGroup);
    displayPositionX_ = addValue(displayPositionLayout, QStringLiteral("X"), QStringLiteral("displayPositionX"), displayPositionGroup);
    displayPositionY_ = addValue(displayPositionLayout, QStringLiteral("Y"), QStringLiteral("displayPositionY"), displayPositionGroup);
    displayPositionZ_ = addValue(displayPositionLayout, QStringLiteral("Z"), QStringLiteral("displayPositionZ"), displayPositionGroup);
    displayOffset_ = addValue(displayPositionLayout, QStringLiteral("显示偏移XYZ"), QStringLiteral("displayOffset"), displayPositionGroup);
    detailsLayout->addWidget(displayPositionGroup);
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
    updateAssemblyControls();

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
            QString(), QStringLiteral("设备资料 (*.json *.step *.stp);;CAD STEP (*.step *.stp);;设备 JSON (*.json)"));
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
        scene_->focusVisible();
        showStatus(QStringLiteral("当前仅显示选中零件，点击“显示全部”恢复。"));
    });
    connect(showAllButton, &QPushButton::clicked, this, [this] {
        controller_.showAll();
        rebuildList();
        syncScene();
        scene_->focusVisible();
        showStatus(QStringLiteral("全部零件已恢复显示。"));
    });
    connect(fitButton, &QPushButton::clicked, this, [this] { scene_->fitAll(); });
    connect(fitVisibleButton, &QPushButton::clicked, this, [this] { scene_->focusVisible(); });
    connect(focusButton_, &QPushButton::clicked, this, [this] {
        if (!scene_->focusSelected()) showStatus(QStringLiteral("请先选择一个可见零件。"), true);
    });
    scene_->onPartPicked = [this](const QString &id) {
        const QString previousIsolation = controller_.isolatedPartId();
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
        updateColorSwatches();
        if (!id.isEmpty() && !previousIsolation.isEmpty() && previousIsolation != controller_.isolatedPartId())
            scene_->focusVisible();
        showStatus(QString());
    };
}

void EquipmentWindow::createAssemblyControls(QVBoxLayout *layout, QWidget *parent)
{
    auto *viewRow = new QHBoxLayout;
    viewModeSelector_ = new QComboBox(parent);
    viewModeSelector_->setObjectName(QStringLiteral("viewModeSelector"));
    viewModeSelector_->addItem(QStringLiteral("01 彩色装配"), 0);
    viewModeSelector_->addItem(QStringLiteral("02 工程轴测"), 1);
    viewModeSelector_->addItem(QStringLiteral("03 二维投影"), 2);
    viewModeSelector_->addItem(QStringLiteral("04 三视图"), 3);
    projectionSelector_ = new QComboBox(parent);
    projectionSelector_->setObjectName(QStringLiteral("projectionSelector"));
    projectionSelector_->addItem(QStringLiteral("前视 XZ"), int(SceneView::ViewMode::Front));
    projectionSelector_->addItem(QStringLiteral("俯视 XY"), int(SceneView::ViewMode::Top));
    projectionSelector_->addItem(QStringLiteral("侧视 YZ"), int(SceneView::ViewMode::Right));
    projectionSelector_->hide();
    axisPlacementSelector_ = new QComboBox(parent);
    axisPlacementSelector_->setObjectName(QStringLiteral("axisPlacementSelector"));
    axisPlacementSelector_->addItem(QStringLiteral("XYZ 右下角"), int(SceneView::AxisPlacement::Corner));
    axisPlacementSelector_->addItem(QStringLiteral("XYZ 模型旁"), int(SceneView::AxisPlacement::Model));
    axisPlacementSelector_->addItem(QStringLiteral("XYZ 同时显示"), int(SceneView::AxisPlacement::Both));
    axisPlacementSelector_->addItem(QStringLiteral("XYZ 隐藏"), int(SceneView::AxisPlacement::Hidden));
    dimensionsCheck_ = new QCheckBox(QStringLiteral("包络尺寸"), parent);
    dimensionsCheck_->setObjectName(QStringLiteral("dimensionsCheck"));
    dimensionsCheck_->setChecked(false);
    gridCheck_ = new QCheckBox(QStringLiteral("毫米网格"), parent);
    gridCheck_->setObjectName(QStringLiteral("gridCheck"));
    gridCheck_->setChecked(true);
    viewRow->addWidget(viewModeSelector_); viewRow->addWidget(projectionSelector_);
    viewRow->addWidget(axisPlacementSelector_); viewRow->addWidget(dimensionsCheck_); viewRow->addWidget(gridCheck_);
    layout->addLayout(viewRow);

    auto *offsetRows = new QGridLayout;
    explodeAxisSelector_ = new QComboBox(parent);
    explodeAxisSelector_->setObjectName(QStringLiteral("explodeAxisSelector"));
    explodeAxisSelector_->addItem(QStringLiteral("自动装配轴"), int(SceneView::ExplodeAxis::Auto));
    explodeAxisSelector_->addItem(QStringLiteral("展开 X"), int(SceneView::ExplodeAxis::X));
    explodeAxisSelector_->addItem(QStringLiteral("展开 Y"), int(SceneView::ExplodeAxis::Y));
    explodeAxisSelector_->addItem(QStringLiteral("展开 Z"), int(SceneView::ExplodeAxis::Z));
    explosionSlider_ = new QSlider(Qt::Horizontal, parent);
    explosionSlider_->setObjectName(QStringLiteral("explosionSlider")); explosionSlider_->setRange(0, 100);
    explosionValue_ = new QLabel(QStringLiteral("0%"), parent);
    explosionValue_->setObjectName(QStringLiteral("explosionValue"));
    auto *restore = new QPushButton(QStringLiteral("整体复原"), parent);
    restore->setObjectName(QStringLiteral("restoreDisplayButton"));
    offsetRows->addWidget(explodeAxisSelector_, 0, 0); offsetRows->addWidget(explosionSlider_, 0, 1);
    offsetRows->addWidget(explosionValue_, 0, 2); offsetRows->addWidget(restore, 0, 3);
    offsetRows->addWidget(new QLabel(QStringLiteral("选中件单独拉远"), parent), 1, 0);
    pullSlider_ = new QSlider(Qt::Horizontal, parent);
    pullSlider_->setObjectName(QStringLiteral("pullSlider")); pullSlider_->setRange(0, 100);
    pullValue_ = new QLabel(QStringLiteral("0%"), parent); pullValue_->setObjectName(QStringLiteral("pullValue"));
    partRestoreButton_ = new QPushButton(QStringLiteral("复原单件偏移"), parent);
    partRestoreButton_->setObjectName(QStringLiteral("restorePartButton"));
    offsetRows->addWidget(pullSlider_, 1, 1); offsetRows->addWidget(pullValue_, 1, 2); offsetRows->addWidget(partRestoreButton_, 1, 3);
    offsetRows->setColumnStretch(1, 1);
    layout->addLayout(offsetRows);
    auto *colorRow = new QHBoxLayout;
    pullDirectionSelector_ = new QComboBox(parent);
    pullDirectionSelector_->setObjectName(QStringLiteral("pullDirectionSelector"));
    pullDirectionSelector_->addItem(QStringLiteral("重合件备用正向"), 1);
    pullDirectionSelector_->addItem(QStringLiteral("重合件备用反向"), -1);
    partColorButton_ = new QPushButton(QStringLiteral("选中件颜色"), parent);
    partColorButton_->setObjectName(QStringLiteral("partColorButton"));
    autoColorButton_ = new QPushButton(QStringLiteral("恢复自动配色"), parent);
    autoColorButton_->setObjectName(QStringLiteral("autoColorButton"));
    colorRow->addWidget(pullDirectionSelector_); colorRow->addWidget(partColorButton_); colorRow->addWidget(autoColorButton_); colorRow->addStretch();
    layout->addLayout(colorRow);

    const auto applyView = [this] {
        const int mode = viewModeSelector_->currentData().toInt();
        projectionSelector_->setVisible(mode == 2);
        scene_->setViewMode(mode == 0 ? SceneView::ViewMode::Isometric : mode == 1 ? SceneView::ViewMode::Engineering
            : mode == 2 ? static_cast<SceneView::ViewMode>(projectionSelector_->currentData().toInt()) : SceneView::ViewMode::ThreeViews);
    };
    connect(viewModeSelector_, &QComboBox::currentIndexChanged, this, [applyView](int) { applyView(); });
    connect(projectionSelector_, &QComboBox::currentIndexChanged, this, [applyView](int) { applyView(); });
    connect(axisPlacementSelector_, &QComboBox::currentIndexChanged, this, [this](int) { scene_->setAxisPlacement(static_cast<SceneView::AxisPlacement>(axisPlacementSelector_->currentData().toInt())); });
    connect(dimensionsCheck_, &QCheckBox::toggled, this, [this](bool visible) { scene_->setDimensionsVisible(visible); });
    connect(gridCheck_, &QCheckBox::toggled, this, [this](bool visible) { scene_->setGridVisible(visible); });
    connect(explodeAxisSelector_, &QComboBox::currentIndexChanged, this, [this](int) { scene_->setExplodeAxis(static_cast<SceneView::ExplodeAxis>(explodeAxisSelector_->currentData().toInt())); scene_->focusVisible(); displayProperties(); showAssemblyStatus(QStringLiteral("展开轴已切换")); });
    connect(explosionSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (scene_->setExplosionFraction(value / 100.0)) { updateAssemblyControls(); displayProperties(); scene_->focusVisible(); showAssemblyStatus(QStringLiteral("整体有序向外展开")); }
    });
    connect(pullSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (scene_->setPullFraction(value / 100.0)) { updateAssemblyControls(); displayProperties(); scene_->focusVisible(); showAssemblyStatus(QStringLiteral("选中件单独拉远")); }
    });
    connect(pullDirectionSelector_, &QComboBox::currentIndexChanged, this, [this](int) { scene_->setPullDirection(pullDirectionSelector_->currentData().toInt()); displayProperties(); scene_->focusVisible(); showAssemblyStatus(QStringLiteral("重合件备用方向已切换")); });
    connect(restore, &QPushButton::clicked, this, [this] { scene_->restoreDisplayOffsets(); updateAssemblyControls(); displayProperties(); scene_->focusVisible(); showAssemblyStatus(QStringLiteral("整体与单件显示偏移已复原")); });
    connect(partRestoreButton_, &QPushButton::clicked, this, [this] { scene_->setPullFraction(0); updateAssemblyControls(); displayProperties(); scene_->focusVisible(); showAssemblyStatus(QStringLiteral("单件偏移已复原，其他零件不变")); });
    connect(partColorButton_, &QPushButton::clicked, this, [this] {
        const QString id = controller_.selectedPartId();
        const QString equipment = controller_.equipment() ? controller_.equipment()->id : QString();
        if (id.isEmpty()) return;
        const QColor color = QColorDialog::getColor(scene_->partColor(id), this, QStringLiteral("选中零件演示配色"));
        if (!color.isValid() || !controller_.equipment() || equipment != controller_.equipment()->id) return;
        if (scene_->setPartColor(id, color)) { updateColorSwatches(); updateAssemblyControls(); }
    });
    connect(autoColorButton_, &QPushButton::clicked, this, [this] { if (scene_->resetPartColor(controller_.selectedPartId())) { updateColorSwatches(); updateAssemblyControls(); } });
}

void EquipmentWindow::updateAssemblyControls()
{
    const QSignalBlocker explosionBlock(explosionSlider_), pullBlock(pullSlider_);
    explosionSlider_->setValue(qRound(scene_->explosionFraction() * 100));
    pullSlider_->setValue(qRound(scene_->pullFraction() * 100));
    explosionValue_->setText(QStringLiteral("%1%").arg(explosionSlider_->value()));
    pullValue_->setText(QStringLiteral("%1%").arg(pullSlider_->value()));
    const bool selected = controller_.selectedPart() != nullptr;
    pullSlider_->setEnabled(selected); partRestoreButton_->setEnabled(selected); pullDirectionSelector_->setEnabled(selected);
    partColorButton_->setEnabled(selected); autoColorButton_->setEnabled(selected);
    focusButton_->setEnabled(selected && !controller_.hiddenPartIds().contains(controller_.selectedPartId()));
    partColorButton_->setStyleSheet(selected ? QStringLiteral("border-left: 12px solid %1;").arg(scene_->partColor(controller_.selectedPartId()).name()) : QString());
}

void EquipmentWindow::updateColorSwatches()
{
    QHash<QString, const Part *> parts;
    for (const Part &part : controller_.parts()) parts.insert(part.id, &part);
    for (int row = 0; row < partsList_->count(); ++row) {
        auto *item = partsList_->item(row); QPixmap swatch(16, 16);
        const QString id = item->data(Qt::UserRole).toString();
        swatch.fill(scene_->partColor(id)); item->setIcon(QIcon(swatch));
        const Part *part = parts.value(id);
        if (!part) continue;
        const bool hidden = controller_.hiddenPartIds().contains(id);
        item->setText(QStringLiteral("%1 (%2)%3").arg(part->name, id, hidden ? QStringLiteral(" · 已隐藏") : QString()));
        item->setForeground(hidden ? QColor(QStringLiteral("#778899")) : palette().color(QPalette::Text));
        QFont font = item->font(); font.setItalic(hidden); item->setFont(font);
    }
}

void EquipmentWindow::showAssemblyStatus(const QString &action)
{
    const Part *part = controller_.selectedPart();
    showStatus(part ? QStringLiteral("%1 · %2 · %3；源位置和尺寸不写回资料。")
        .arg(action, part->name, controller_.isIsolationActive() ? QStringLiteral("单件展示") : QStringLiteral("整机展示"))
        : QStringLiteral("%1 · %2；源位置和尺寸不写回资料。")
        .arg(action, controller_.isIsolationActive() ? QStringLiteral("单件展示（未选中）") : QStringLiteral("整机展示")));
}

EquipmentWindow::~EquipmentWindow()
{
    closing_ = true;
    // 子QObject在QWidget基类销毁时才释放；显式提前取消，回调不能接触已析构成员。
    delete cadImport_;
    cadImport_ = nullptr;
}

void EquipmentWindow::closeEvent(QCloseEvent *event)
{
    closing_ = true;
    if (!cancelCadImport()) {
        closing_ = false;
        event->ignore();
        showStatus(QStringLiteral("CAD进程退出尚未确认，请稍后重试关闭。"), true);
        return;
    }
    QWidget::closeEvent(event);
}

bool EquipmentWindow::cadImportInProgress() const
{
    return cadBusy_;
}

void EquipmentWindow::setCadBusy(bool busy)
{
    cadBusy_ = busy;
    importButton_->setEnabled(!busy);
    cadCancelButton_->setEnabled(busy);
    // 转换期间保留当前设备的交互；禁止其他导入入口竞争提交新设备。
    sharedButton_->setEnabled(!busy);
}

bool EquipmentWindow::cancelCadImport()
{
    if (!cadImport_) return true;
    const bool hadWork = cadImport_->isBusy() || cadCancelButton_->isEnabled();
    const bool stopped = cadImport_->cancel();
    if (!stopped && !closing_) showStatus(cadImport_->lastError(), true);
    else if (hadWork && !closing_)
        showStatus(QStringLiteral("CAD导入已取消；原设备和资料保留。"));
    setCadBusy(cadImport_->isBusy());
    return stopped;
}

bool EquipmentWindow::importCadFile(const QString &filePath)
{
    if (closing_) return false;
    if (cadBusy_) {
        showStatus(QStringLiteral("CAD转换或资料提交正在进行，请等待或取消后再导入。"), true);
        return false;
    }
    const bool started = cadImport_->start(filePath, [this](QByteArray bytes, QString error) {
        if (closing_) return;
        setCadBusy(false);
        if (!error.isEmpty()) { showStatus(error, true); return; }
        if (!controller_.importCadResult(bytes)) {
            const QString message = controller_.lastError();
            refreshEquipmentChoices();
            showStatus(message, true);
            return;
        }
        displayLoadedEquipment();
        if (refreshEquipmentChoices())
            showStatus(QStringLiteral("CAD曲面网格已导入并保存；尺寸为全局外包络，非制造标注。"));
    });
    if (!started) { showStatus(cadImport_->lastError(), true); return false; }
    // 进程结束到排队业务回调完成之前也属于导入，不能在此间隙重入其他导入。
    setCadBusy(true);
    showStatus(QStringLiteral("正在后台转换CAD，可继续旋转和查看当前设备；最长120秒。"));
    return true;
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
    scene_->setParts(controller_.parts());
    {
        const QSignalBlocker viewBlock(viewModeSelector_), projectionBlock(projectionSelector_), axisBlock(axisPlacementSelector_),
            explodeAxisBlock(explodeAxisSelector_), directionBlock(pullDirectionSelector_), dimensionsBlock(dimensionsCheck_), gridBlock(gridCheck_);
        viewModeSelector_->setCurrentIndex(0); projectionSelector_->setCurrentIndex(0); projectionSelector_->hide();
        axisPlacementSelector_->setCurrentIndex(0); explodeAxisSelector_->setCurrentIndex(0); pullDirectionSelector_->setCurrentIndex(0);
        dimensionsCheck_->setChecked(false); gridCheck_->setChecked(true);
    }
    scene_->setViewMode(SceneView::ViewMode::Isometric); scene_->setAxisPlacement(SceneView::AxisPlacement::Corner);
    scene_->setDimensionsVisible(false); scene_->setGridVisible(true); scene_->setExplodeAxis(SceneView::ExplodeAxis::Auto);
    scene_->setPullDirection(1); scene_->restoreDisplayOffsets();
    rebuildList();
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
    if (cadImportInProgress()) {
        showStatus(QStringLiteral("CAD转换正在进行，请等待或取消后再导入。"), true);
        return false;
    }
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
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("step") || suffix == QStringLiteral("stp"))
        return importCadFile(filePath);
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
    updateColorSwatches();
}

void EquipmentWindow::selectFromList()
{
    const auto selected = partsList_->selectedItems();
    const QString id = selected.isEmpty() ? QString() : selected.first()->data(Qt::UserRole).toString();
    const QString previousIsolation = controller_.isolatedPartId();
    if (!controller_.selectPart(id)) {
        showStatus(controller_.lastError(), true);
        restoreSelection();
        return;
    }
    syncScene();
    updateColorSwatches();
    if (!id.isEmpty() && !previousIsolation.isEmpty() && previousIsolation != controller_.isolatedPartId())
        scene_->focusVisible();
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
    updateAssemblyControls();
    displayProperties();
}

void EquipmentWindow::displayProperties()
{
    // 同时清空被隐藏的字段，保证形状切换和清除选择不会残留旧资料。
    for (QLabel *label : {partId_, partName_, material_, shape_, positionX_, positionY_, positionZ_,
                         sizeX_, sizeY_, sizeZ_, radius_, length_, cadSource_, displayPositionX_, displayPositionY_, displayPositionZ_, displayOffset_})
        label->clear();
    boxDimensions_->hide();
    cylinderDimensions_->hide();
    boxDimensions_->setTitle(QStringLiteral("长方体尺寸（毫米）"));
    auto *positionGroup = findChild<QGroupBox *>(QStringLiteral("positionGroup"));
    positionGroup->setTitle(QStringLiteral("原始中心位置（毫米）"));
    cadSource_->hide();
    cadSource_->setToolTip(QString());
    const Part *part = controller_.selectedPart();
    prompt_->setVisible(!part);
    prompt_->setText(controller_.isIsolationActive() ? QStringLiteral("未选中；仍为单件展示，显示全部退出隔离") : QStringLiteral("请选择零件"));
    propertyDetails_->setVisible(part != nullptr);
    hideButton_->setEnabled(part != nullptr);
    isolateButton_->setEnabled(part != nullptr);
    if (!part)
        return;
    partId_->setText(part->id);
    partName_->setText(part->name);
    material_->setText(part->material);
    positionX_->setText(NumericUtil::numberText(part->positionX));
    positionY_->setText(NumericUtil::numberText(part->positionY));
    positionZ_->setText(NumericUtil::numberText(part->positionZ));
    const auto offset = scene_->displayOffset(part->id);
    displayPositionX_->setText(NumericUtil::extendedNumberText(
        static_cast<long double>(part->positionX) + offset[0], 15));
    displayPositionY_->setText(NumericUtil::extendedNumberText(
        static_cast<long double>(part->positionY) + offset[1], 15));
    displayPositionZ_->setText(NumericUtil::extendedNumberText(
        static_cast<long double>(part->positionZ) + offset[2], 15));
    displayOffset_->setText(QStringLiteral("%1 / %2 / %3 mm")
        .arg(NumericUtil::extendedNumberText(offset[0], 15),
             NumericUtil::extendedNumberText(offset[1], 15),
             NumericUtil::extendedNumberText(offset[2], 15)));
    if (part->shape == QStringLiteral("box") || part->shape == QStringLiteral("mesh")) {
        const bool mesh = part->shape == QStringLiteral("mesh");
        shape_->setText(mesh ? QStringLiteral("CAD曲面网格") : QStringLiteral("长方体"));
        if (mesh) {
            boxDimensions_->setTitle(QStringLiteral("全局XYZ外包络（毫米，非制造标注）"));
            positionGroup->setTitle(QStringLiteral("原始全局外包络中心（毫米）"));
            cadSource_->setText(QStringLiteral("%1\nSHA256: %2…")
                .arg(part->cadSourceName, part->cadSourceSha256.left(16)));
            cadSource_->setToolTip(part->cadSourceSha256);
            cadSource_->show();
        }
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
