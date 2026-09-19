#include "bifrostdiffdialog.h"
#include "bifrostdiffdb.h"
#include "bifrostdiffengine.h"
#include "bifrostdiffstore.h"
#include "bifrostdiffview.h"
#include "uicontext.h"

#include <QtCore/QDateTime>
#include <QtCore/QPointer>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

using namespace BinaryNinja;

BifrostDiffDialog::BifrostDiffDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Bifrost – Run Diff");
    setMinimumWidth(380);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    UIContext* ctx       = UIContext::activeContext();
    Ref<Project> project = ctx ? ctx->getProject() : nullptr;

    m_leftCombo  = new QComboBox(this);
    m_rightCombo = new QComboBox(this);

    if (project && project->IsOpen())
    {
        for (auto& pfile : project->GetFiles())
        {
            QString label = QString::fromStdString(pfile->GetName());
            m_projectFiles.push_back(pfile);
            m_leftCombo->addItem(label);
            m_rightCombo->addItem(label);
        }
        if (m_rightCombo->count() >= 2)
            m_rightCombo->setCurrentIndex(1);
    }
    else
    {
        m_leftCombo->addItem("(no project open)");
        m_rightCombo->addItem("(no project open)");
        m_leftCombo->setEnabled(false);
        m_rightCombo->setEnabled(false);
    }

    m_diffNameEdit = new QLineEdit(this);
    m_diffNameEdit->setPlaceholderText("e.g. v1-vs-v2");

    // Suggest a default name from the two selected binaries
    auto updateDefaultName = [this]() {
        int li = m_leftCombo->currentIndex();
        int ri = m_rightCombo->currentIndex();
        if (li >= 0 && ri >= 0 && li < (int)m_projectFiles.size() && ri < (int)m_projectFiles.size())
        {
            // Strip extensions for brevity, sharing the diff view's helper so
            // the two cannot drift apart (it strips trailing extensions only —
            // removing them anywhere turned "a.out" into "aut").
            QString ln = BifrostDiffView::normalizeName(
                QString::fromStdString(m_projectFiles[li]->GetName()));
            QString rn = BifrostDiffView::normalizeName(
                QString::fromStdString(m_projectFiles[ri]->GetName()));
            m_diffNameEdit->setPlaceholderText(ln + "-vs-" + rn);
        }
    };
    connect(m_leftCombo,  qOverload<int>(&QComboBox::currentIndexChanged), this, updateDefaultName);
    connect(m_rightCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, updateDefaultName);
    updateDefaultName();

    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->addRow("Left binary:",  m_leftCombo);
    form->addRow("Right binary:", m_rightCombo);
    form->addRow("Diff name:",    m_diffNameEdit);

    // Off by default: every run would otherwise add another project entry.
    m_saveToProjectCheck = new QCheckBox("Save to project as a .bndb file", this);
    m_saveToProjectCheck->setToolTip(
        "Also write the diff into the project as \"<name>.bndb\", so it appears in\n"
        "the project browser and opens with a double-click. The file holds no\n"
        "binary image — just the diff (a few KB).");
    form->addRow(QString(), m_saveToProjectCheck);

    layout->addLayout(form);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setVisible(false);
    layout->addWidget(m_statusLabel);

    auto* buttons = new QDialogButtonBox(this);
    auto* runBtn  = buttons->addButton("Run Diff", QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);

    connect(runBtn,  &QPushButton::clicked,      this, &BifrostDiffDialog::onRunClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    layout->addWidget(buttons);
}

void BifrostDiffDialog::onRunClicked()
{
    if (m_running) return;   // a diff is already in flight

    int li = m_leftCombo->currentIndex();
    int ri = m_rightCombo->currentIndex();

    if (li < 0 || ri < 0 ||
        li >= (int)m_projectFiles.size() ||
        ri >= (int)m_projectFiles.size())
    {
        m_statusLabel->setText("Please select two binaries.");
        m_statusLabel->setVisible(true);
        return;
    }
    if (li == ri)
    {
        m_statusLabel->setText("Left and right binaries must be different.");
        m_statusLabel->setVisible(true);
        return;
    }

    // Resolve diff name: use text field, fall back to placeholder
    QString diffNameQ = m_diffNameEdit->text().trimmed();
    if (diffNameQ.isEmpty())
        diffNameQ = m_diffNameEdit->placeholderText();
    if (diffNameQ.isEmpty())
        diffNameQ = "diff";
    std::string diffName = diffNameQ.toStdString();

    m_statusLabel->setText("Running diff…");
    m_statusLabel->setVisible(true);
    QApplication::processEvents();

    UIContext* ctx       = UIContext::activeContext();
    Ref<Project> project = ctx ? ctx->getProject() : nullptr;
    if (!project || !project->IsOpen())
    {
        m_statusLabel->setText("No project is open.");
        return;
    }

    auto& leftPFile  = m_projectFiles[li];
    auto& rightPFile = m_projectFiles[ri];

    // Prefer already-open views, fall back to Load()
    auto resolveView = [&](Ref<ProjectFile> pfile) -> Ref<BinaryView> {
        if (ctx)
        {
            for (auto& [bv, _] : ctx->getAvailableBinaryViews())
            {
                auto pf = bv->GetFile()->GetProjectFile();
                if (pf && pf->GetId() == pfile->GetId())
                    return bv;
            }
        }
        return Load(pfile, false);
    };

    Ref<BinaryView> leftBv  = resolveView(leftPFile);
    Ref<BinaryView> rightBv = resolveView(rightPFile);

    if (!leftBv || !rightBv)
    {
        m_statusLabel->setText("Failed to load one or both binaries.");
        return;
    }

    std::string leftName  = leftPFile->GetName();
    std::string rightName = rightPFile->GetName();

    m_running = true;
    m_statusLabel->setText("Running structural diff…");
    m_statusLabel->setVisible(true);

    // Run on a worker thread; the callback fires on the main thread. The save
    // and view-open proceed even if the dialog was closed meanwhile; only the
    // dialog's own widgets are touched behind the QPointer guard.
    // Read the checkbox now — the dialog may be gone by the time this finishes.
    const bool saveToProject = m_saveToProjectCheck->isChecked();

    QPointer<BifrostDiffDialog> self = this;
    bifrost::ComputeAsync(
        leftBv, rightBv,
        [self, diffName, leftName, rightName, saveToProject](bifrost::DiffResult diff) {
            UIContext* ctx       = UIContext::activeContext();
            Ref<Project> project = ctx ? ctx->getProject() : nullptr;
            if (!project || !project->IsOpen())
            {
                if (self) { self->m_running = false;
                            self->m_statusLabel->setText("No project is open."); }
                return;
            }

            std::vector<DiffFuncEntry> entries = bifrost::toStoreEntries(diff);
            std::string timestamp = QDateTime::currentDateTime()
                                        .toString(Qt::ISODate).toStdString();
            bifrostSaveDiff(diffName, leftName, rightName, timestamp, entries, project);

            auto diffData = bifrostLoadDiff(diffName, project);

            // Optionally also publish it as a .bndb entry in the project browser.
            if (saveToProject && diffData)
            {
                std::string error;
                if (!bifrostSaveDiffToProject(diffName, diffData, project, error))
                    QMessageBox::warning(
                        nullptr, "Bifrost",
                        QString("The diff ran and was saved to the project, but it "
                                "could not be written as a .bndb file: %1")
                            .arg(QString::fromStdString(error)));
            }

            // Automatically open the diff view.
            if (diffData && ctx)
            {
                QString tabTitle = QString("Diff: %1").arg(QString::fromStdString(diffName));
                auto* view = new BifrostDiffView(nullptr, diffData,
                                                 QString::fromStdString(diffName));
                ctx->createTabForWidget(tabTitle, view);
            }

            if (self) { self->m_running = false; self->accept(); }
        });
}
