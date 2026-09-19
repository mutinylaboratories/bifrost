#pragma once

#include "binaryninjaapi.h"

#include <QtWidgets/QDialog>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>

// Modal dialog listing every diff saved in the current project's metadata, with
// buttons to open one in a diff view or delete it. This is where the otherwise
// unreachable bifrostDeleteDiff is exposed.
class BifrostManageDiffsDialog : public QDialog
{
    Q_OBJECT

    QListWidget* m_list;
    QPushButton* m_openBtn;
    QPushButton* m_saveToProjectBtn;
    QPushButton* m_exportBtn;
    QPushButton* m_deleteBtn;

    void refresh();
    void updateButtons();

public:
    explicit BifrostManageDiffsDialog(QWidget* parent = nullptr);

private Q_SLOTS:
    void openSelected();
    void saveSelectedToProject();
    void exportSelected();
    void deleteSelected();
};
