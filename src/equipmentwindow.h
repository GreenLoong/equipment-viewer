#pragma once

#include <QWidget>

class EquipmentController;
class QGroupBox;
class QLabel;
class QListWidget;
class QPushButton;

class EquipmentWindow : public QWidget {
public:
    explicit EquipmentWindow(EquipmentController &controller);
    bool loadEquipment(const QString &equipmentId);

private:
    void selectFromList();
    void restoreSelection();
    void clearSelection();
    void displayProperties();
    void showStatus(const QString &message, bool isError = false);

    EquipmentController &controller_;
    QLabel *title_;
    QLabel *equipmentId_;
    QListWidget *partsList_;
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
    QLabel *count_;
    QLabel *status_;
};
