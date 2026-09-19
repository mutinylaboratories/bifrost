#pragma once

#include "binaryninjaapi.h"

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>

// Modal dialog that lets the user pick a left and right binary from the open
// project, then runs the diff and saves it to the project metadata database.
// Optionally also writes the diff into the project as a .bndb file, so it shows
// up in the project browser next to the binaries (see bifrostdiffdb.h).
class BifrostDiffDialog : public QDialog
{
    Q_OBJECT

    QComboBox*  m_leftCombo;
    QComboBox*  m_rightCombo;
    QLineEdit*  m_diffNameEdit;
    QCheckBox*  m_saveToProjectCheck;
    QLabel*     m_statusLabel;

    // Parallel list of project files populated into the combo boxes
    std::vector<BinaryNinja::Ref<BinaryNinja::ProjectFile>> m_projectFiles;

    // True while a background diff is running, to reject re-entrant Run clicks.
    bool m_running = false;

public:
    explicit BifrostDiffDialog(QWidget* parent = nullptr);

private Q_SLOTS:
    void onRunClicked();
};
