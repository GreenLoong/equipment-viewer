#pragma once

#include <QWidget>
#include <QByteArray>

class EquipmentController;
class SceneView;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

/** 设备浏览窗口：负责操作入口及显示，不直接执行SQL。 */
class EquipmentWindow : public QWidget {
public:
    /** 借用控制器，调用方须保证控制器在窗口销毁后仍有效。 */
    explicit EquipmentWindow(EquipmentController &controller);
    /** 打开指定设备，加载失败保留当前显示并提示。 */
    bool loadEquipment(const QString &equipmentId);
    /** 导入JSON资料并显示新设备；失败有明确错误且保留原资料。 */
    bool importData(const QByteArray &json);
    /** 读取至多5MiB的JSON文件并调用导入，不覆盖已有设备。 */
    bool importFile(const QString &filePath);

private:
    void selectFromList();
    void restoreSelection();
    void clearSelection();
    void displayProperties();
    void rebuildList();
    void syncScene();
    void displayLoadedEquipment();
    bool refreshEquipmentChoices();
    void showStatus(const QString &message, bool isError = false);

    EquipmentController &controller_;
    QLabel *title_;
    QLabel *equipmentId_;
    QComboBox *equipmentSelector_;
    QLineEdit *search_;
    QListWidget *partsList_;
    SceneView *scene_;
    QLabel *prompt_;
    QWidget *propertyDetails_;
    QLabel *partId_;
    QLabel *partName_;
    QLabel *material_;
    QLabel *shape_;
    QLabel *positionX_;
    QLabel *positionY_;
    QLabel *positionZ_;
    QGroupBox *boxDimensions_;
    QLabel *sizeX_;
    QLabel *sizeY_;
    QLabel *sizeZ_;
    QGroupBox *cylinderDimensions_;
    QLabel *radius_;
    QLabel *length_;
    QPushButton *clearSelectionButton_;
    QPushButton *hideButton_;
    QPushButton *isolateButton_;
    QLabel *count_;
    QLabel *status_;
};
