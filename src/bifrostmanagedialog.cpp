#include "bifrostmanagedialog.h"
#include "bifrostdiffdb.h"
#include "bifrostdiffstore.h"
#include "bifrostdiffview.h"
#include "bifrostexport.h"
#include "uicontext.h"

#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QVBoxLayout>

using namespace BinaryNinja;

// Item data role holding the diff's stored name.
static constexpr int RoleDiffName = Qt::UserRole;

static Ref<Project> currentProject()
{
    UIContext* ctx = UIContext::activeContext();
    if (!ctx) return nullptr;
    Ref<Project> project = ctx->getProject();
    return (project && project->IsOpen()) ? project : nullptr;
}

// "left  →  right   ·   timestamp"  (best-effort; falls back to just the name)
static QString subtitleFor(Ref<Metadata> diffData)
{
    if (!diffData || !diffData->IsKeyValueStore()) return {};
    auto get = [&](const char* k) -> QString {
        auto m = diffData->Get(k);
        return (m && m->IsString()) ? QString::fromStdString(m->GetString()) : QString();
    };
    QString left = get("left"), right = get("right"), ts = get("timestamp");
    QString s;
    if (!left.isEmpty() || !right.isEmpty()) s = left + "  →  " + right;
    if (!ts.isEmpty()) s += (s.isEmpty() ? "" : "   ·   ") + ts;
    return s;
}

BifrostManageDiffsDialog::BifrostManageDiffsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Bifrost – Diffs");
    setMinimumSize(420, 280);

    auto* layout = new QVBoxLayout(this);

    auto* heading = new QLabel("Saved diffs in this project:", this);
    layout->addWidget(heading);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_list, 1);

    auto* btnRow = new QHBoxLayout();
    m_openBtn   = new QPushButton("Open", this);
    m_exportBtn = new QPushButton("Export…", this);
    m_deleteBtn = new QPushButton("Delete…", this);
    m_saveToProjectBtn = new QPushButton("Save to Project", this);
    m_saveToProjectBtn->setToolTip(
        "Write this diff into the project as a .bndb file, so it appears in the\n"
        "project browser and opens with a double-click like a binary");
    btnRow->addWidget(m_openBtn);
    btnRow->addWidget(m_saveToProjectBtn);
    btnRow->addWidget(m_exportBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addStretch(1);
    auto* closeBtn = new QPushButton("Close", this);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    connect(m_list, &QListWidget::itemSelectionChanged, this, &BifrostManageDiffsDialog::updateButtons);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { openSelected(); });
    connect(m_openBtn,   &QPushButton::clicked, this, &BifrostManageDiffsDialog::openSelected);
    connect(m_saveToProjectBtn, &QPushButton::clicked, this,
            &BifrostManageDiffsDialog::saveSelectedToProject);
    connect(m_exportBtn, &QPushButton::clicked, this, &BifrostManageDiffsDialog::exportSelected);
    connect(m_deleteBtn, &QPushButton::clicked, this, &BifrostManageDiffsDialog::deleteSelected);
    connect(closeBtn,    &QPushButton::clicked, this, &QDialog::accept);

    refresh();
}

void BifrostManageDiffsDialog::refresh()
{
    m_list->clear();

    Ref<Project> project = currentProject();
    if (!project)
    {
        updateButtons();
        return;
    }

    for (auto& name : bifrostListDiffs(project))
    {
        QString qname = QString::fromStdString(name);
        QString sub   = subtitleFor(bifrostLoadDiff(name, project));

        auto* item = new QListWidgetItem(m_list);
        item->setText(sub.isEmpty() ? qname : (qname + "\n" + sub));
        item->setData(RoleDiffName, qname);
    }
    updateButtons();
}

void BifrostManageDiffsDialog::updateButtons()
{
    bool hasSelection = m_list->currentItem() != nullptr;
    m_openBtn->setEnabled(hasSelection);
    m_saveToProjectBtn->setEnabled(hasSelection);
    m_exportBtn->setEnabled(hasSelection);
    m_deleteBtn->setEnabled(hasSelection);
}

void BifrostManageDiffsDialog::saveSelectedToProject()
{
    auto* item = m_list->currentItem();
    if (!item) return;
    QString name = item->data(RoleDiffName).toString();

    Ref<Project> project = currentProject();
    if (!project) return;

    auto diffData = bifrostLoadDiff(name.toStdString(), project);
    if (!diffData)
    {
        QMessageBox::warning(this, "Save to project", "Could not load the selected diff.");
        return;
    }

    std::string error;
    if (!bifrostSaveDiffToProject(name.toStdString(), diffData, project, error))
    {
        QMessageBox::warning(this, "Save to project",
                             QString("Could not save the diff: %1")
                                 .arg(QString::fromStdString(error)));
        return;
    }

    QMessageBox::information(
        this, "Save to project",
        QString("Saved as \"%1.bndb\" in the project.\n\n"
                "It now appears in the project browser and opens with a "
                "double-click. The two binaries stay as their own project "
                "entries — the diff file holds no binary image.")
            .arg(name));
}

void BifrostManageDiffsDialog::openSelected()
{
    auto* item = m_list->currentItem();
    if (!item) return;
    QString name = item->data(RoleDiffName).toString();

    UIContext* ctx = UIContext::activeContext();
    Ref<Project> project = currentProject();
    if (!ctx || !project) return;

    auto diffData = bifrostLoadDiff(name.toStdString(), project);
    if (!diffData) return;

    QString tabTitle = QString("Diff: %1").arg(name);
    auto* view = new BifrostDiffView(nullptr, diffData, name);
    ctx->createTabForWidget(tabTitle, view);
    accept();
}

void BifrostManageDiffsDialog::exportSelected()
{
    auto* item = m_list->currentItem();
    if (!item) return;
    QString name = item->data(RoleDiffName).toString();

    Ref<Project> project = currentProject();
    if (!project) return;
    auto diffData = bifrostLoadDiff(name.toStdString(), project);
    if (!diffData)
    {
        QMessageBox::warning(this, "Export diff", "Could not load the selected diff.");
        return;
    }

    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(
        this, "Export diff", name + ".html",
        "HTML report (*.html);;JSON (*.json)", &selectedFilter);
    if (path.isEmpty()) return;

    // Decide the format from the chosen filter, then the extension.
    bool json = selectedFilter.contains("json", Qt::CaseInsensitive)
             || path.endsWith(".json", Qt::CaseInsensitive);
    if (json && !path.endsWith(".json", Qt::CaseInsensitive)) path += ".json";
    if (!json && !path.endsWith(".html", Qt::CaseInsensitive)) path += ".html";

    bool ok = json ? bifrost::exportDiffJson(diffData, path.toStdString())
                   : bifrost::exportDiffHtml(diffData, path.toStdString());

    if (ok)
        QMessageBox::information(this, "Export diff",
            QString("Exported to:\n%1").arg(path));
    else
        QMessageBox::warning(this, "Export diff",
            QString("Failed to write:\n%1").arg(path));
}

void BifrostManageDiffsDialog::deleteSelected()
{
    auto* item = m_list->currentItem();
    if (!item) return;
    QString name = item->data(RoleDiffName).toString();

    Ref<Project> project = currentProject();
    if (!project) return;

    auto reply = QMessageBox::question(
        this, "Delete diff",
        QString("Delete the saved diff \"%1\" from this project?").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    bifrostDeleteDiff(name.toStdString(), project);
    refresh();
}
