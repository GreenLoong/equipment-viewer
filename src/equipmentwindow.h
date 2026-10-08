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
class CadImportProcess;
class QCloseEvent;

/** 设备浏览窗口：负责操作入口及显示，不直接执行SQL。 */
class EquipmentWindow : public QWidget {
public:
    /** 借用控制器，调用方须保证控制器在窗口销毁后仍有效。 */
    explicit EquipmentWindow(EquipmentController &controller);
    /** 在派生成员仍存活时停止转换，清除窗口回调。 */
    ~EquipmentWindow() override;
    /** 打开指定设备，加载失败保留当前显示并提示。 */
    bool loadEquipment(const QString &equipmentId);
    /** 导入JSON资料并显示新设备；失败有明确错误且保留原资料。 */
    bool importData(const QByteArray &json);
    /** 读取至多5MiB的JSON或异步启动至多100MiB的STEP/STP，不覆盖已有设备。 */
    bool importFile(const QString &filePath);
    /** 后台启动CAD转换；true仅表示已启动，最终结果通过状态栏显示。 */
    bool importCadFile(const QString &filePath);
    /** 取消自己拥有的CAD进程；未确认退出返回false。 */
    bool cancelCadImport();
    /** 查询后台导入状态，旋转/选择和显隐操作在等待期间继续可用。 */
    bool cadImportInProgress() const;

protected:
    void closeEvent(QCloseEvent *event) override;

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
    void setCadBusy(bool busy);

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
    QLabel *cadSource_;
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
    CadImportProcess *cadImport_;
    QPushButton *importButton_;
    QPushButton *cadCancelButton_;
    QPushButton *sharedButton_;
    bool closing_ = false;
    bool cadBusy_ = false;
};
