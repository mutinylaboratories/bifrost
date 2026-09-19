#include "bifrostsidebar.h"
#include "bifrostdiffengine.h"
#include "bifrostdiffstore.h"
#include "sidebar.h"
#include "theme.h"
#include "uicontext.h"

#include <QtCore/QDateTime>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtSvg/QSvgRenderer>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>

using namespace BinaryNinja;

// Item data roles
static constexpr int RoleAddress       = Qt::UserRole;      // uint64_t — function start
static constexpr int RoleProjectFileId = Qt::UserRole + 1;  // QString  — project file ID

// Diff tree roles
static constexpr int RoleLeftAddr  = Qt::UserRole;
static constexpr int RoleRightAddr = Qt::UserRole + 1;
static constexpr int RoleStatus    = Qt::UserRole + 2;

static constexpr int ColStatus     = 0;
static constexpr int ColFunction   = 1;
static constexpr int ColSimilarity = 2;
static constexpr int ColConfidence = 3;
static constexpr int ColAlgorithm  = 4;
static constexpr int ColLeftAddr   = 5;
static constexpr int ColRightAddr  = 6;
static constexpr int DiffColumnCount = 7;

// Refine a stored/legacy status into the displayed status vocabulary.
// "matched" splits into "identical" vs "changed" using the similarity score.
static QString diffDisplayStatus(const QString& legacy, double similarity)
{
    if (legacy == "added" || legacy == "removed" || legacy == "identical"
        || legacy == "changed")
        return legacy;
    return similarity >= 0.999 ? "identical" : "changed";
}

// Background tint per displayed status.
static QColor diffStatusColor(const QString& status)
{
    if (status == "added")   return QColor(40, 120, 40, 60);
    if (status == "removed") return QColor(160, 40, 40, 60);
    if (status == "changed") return QColor(190, 130, 30, 55);
    return QColor(0, 0, 0, 0); // identical → no tint
}

static QImage renderSvgIcon(const QString& resource, int size)
{
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QSvgRenderer renderer(resource);
    QPainter painter(&image);
    renderer.render(&painter);
    return image;
}

static QString labelForView(Ref<BinaryView> bv)
{
    auto pf = bv->GetFile()->GetProjectFile();
    if (pf)
    {
        std::string name = pf->GetName();
        if (!name.empty())
            return QString::fromStdString(name);
    }
    std::string path = bv->GetFile()->GetFilename();
    size_t s = path.rfind('/');
    return QString::fromStdString(
        (s != std::string::npos) ? path.substr(s + 1) : path);
}

// BifrostSidebarWidget

BifrostSidebarWidget::BifrostSidebarWidget(ViewFrame* /* frame */, Ref<BinaryView> /* data */)
    : SidebarWidget("Bifrost")
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* tabs = new QTabWidget(this);
    layout->addWidget(tabs);

    // ── Functions tab ──────────────────────────────────────────────────────
    auto* functionsTab = new QWidget(tabs);
    auto* funcLayout   = new QVBoxLayout(functionsTab);
    funcLayout->setContentsMargins(0, 4, 0, 0);
    funcLayout->setSpacing(4);

    // Filter row
    auto* filterRow    = new QWidget(functionsTab);
    auto* filterLayout = new QHBoxLayout(filterRow);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setSpacing(4);

    m_filter = new QLineEdit(filterRow);
    m_filter->setPlaceholderText("Filter functions…");
    m_filter->setClearButtonEnabled(true);

    m_refreshBtn = new QPushButton("↺", filterRow);
    m_refreshBtn->setFixedWidth(28);
    m_refreshBtn->setToolTip("Refresh function list");

    filterLayout->addWidget(m_filter);
    filterLayout->addWidget(m_refreshBtn);
    funcLayout->addWidget(filterRow);

    // Stacked widget: page 0 = empty state, page 1 = function tree
    m_stack = new QStackedWidget(functionsTab);

    m_emptyLabel = new QLabel("Open a project to view\nfunctions across binaries.", m_stack);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);

    // Function tree: Function | Address | Binary
    m_tree = new QTreeWidget(m_stack);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({"Function", "Address", "Binary"});
    m_tree->setRootIsDecorated(false);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(2, Qt::AscendingOrder);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    m_stack->addWidget(m_emptyLabel);  // index 0
    m_stack->addWidget(m_tree);        // index 1
    funcLayout->addWidget(m_stack);

    tabs->addTab(functionsTab, "Functions");

    // ── Diff tab ───────────────────────────────────────────────────────────
    auto* diffTab    = new QWidget(tabs);
    auto* diffLayout = new QVBoxLayout(diffTab);
    diffLayout->setContentsMargins(0, 4, 0, 0);
    diffLayout->setSpacing(4);

    m_diffHeaderLabel = new QLabel(diffTab);
    m_diffHeaderLabel->setWordWrap(true);
    m_diffHeaderLabel->setTextFormat(Qt::RichText);

    m_diffStatusLabel = new QLabel("Load binaries in the Bifrost split view,\nthen click Run Diff.", diffTab);
    m_diffStatusLabel->setAlignment(Qt::AlignCenter);
    m_diffStatusLabel->setWordWrap(true);

    m_runDiffBtn = new QPushButton("Run Diff", diffTab);
    m_runDiffBtn->setToolTip("Diff functions between the two Bifrost panes and save results to project");

    m_diffFilter = new QComboBox(diffTab);
    m_diffFilter->addItems({"All", "Changed / added / removed", "Changed", "Added", "Removed"});
    m_diffFilter->setToolTip("Filter which diff rows are shown");

    m_prevChangeBtn = new QPushButton("◀ Prev", diffTab);
    m_nextChangeBtn = new QPushButton("Next ▶", diffTab);
    m_prevChangeBtn->setToolTip("Jump to the previous changed/added/removed function (Shift+F8)");
    m_nextChangeBtn->setToolTip("Jump to the next changed/added/removed function (F8)");
    m_prevChangeBtn->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F8));
    m_nextChangeBtn->setShortcut(QKeySequence(Qt::Key_F8));
    auto* navRow = new QHBoxLayout();
    navRow->setContentsMargins(0, 0, 0, 0);
    navRow->addWidget(m_prevChangeBtn);
    navRow->addWidget(m_nextChangeBtn);

    // Diff tree: Status | Function | Sim | Conf | Algorithm | Left Addr | Right Addr
    m_diffTree = new QTreeWidget(diffTab);
    m_diffTree->setColumnCount(DiffColumnCount);
    m_diffTree->setHeaderLabels(
        {"Status", "Function", "Sim", "Conf", "Algorithm", "Left Addr", "Right Addr"});
    m_diffTree->setRootIsDecorated(false);
    m_diffTree->setSortingEnabled(true);
    m_diffTree->setAlternatingRowColors(true);
    m_diffTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_diffTree->header()->setSectionResizeMode(ColStatus,     QHeaderView::ResizeToContents);
    m_diffTree->header()->setSectionResizeMode(ColFunction,   QHeaderView::Stretch);
    m_diffTree->header()->setSectionResizeMode(ColSimilarity, QHeaderView::ResizeToContents);
    m_diffTree->header()->setSectionResizeMode(ColConfidence, QHeaderView::ResizeToContents);
    m_diffTree->header()->setSectionResizeMode(ColAlgorithm,  QHeaderView::ResizeToContents);
    m_diffTree->header()->setSectionResizeMode(ColLeftAddr,   QHeaderView::ResizeToContents);
    m_diffTree->header()->setSectionResizeMode(ColRightAddr,  QHeaderView::ResizeToContents);

    diffLayout->addWidget(m_diffHeaderLabel);
    diffLayout->addWidget(m_diffStatusLabel);
    diffLayout->addWidget(m_runDiffBtn);
    diffLayout->addWidget(m_diffFilter);
    diffLayout->addLayout(navRow);
    diffLayout->addWidget(m_diffTree, 1);

    tabs->addTab(diffTab, "Diff");

    // ── Connections ────────────────────────────────────────────────────────
    connect(m_filter,     &QLineEdit::textChanged,        this, &BifrostSidebarWidget::applyFilter);
    connect(m_refreshBtn, &QPushButton::clicked,           this, &BifrostSidebarWidget::populate);
    connect(m_tree,       &QTreeWidget::itemDoubleClicked, this, &BifrostSidebarWidget::onItemDoubleClicked);
    connect(m_runDiffBtn, &QPushButton::clicked,           this, &BifrostSidebarWidget::runDiff);
    connect(m_diffTree,   &QTreeWidget::itemClicked,       this, &BifrostSidebarWidget::onDiffItemClicked);
    connect(m_diffFilter, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { applyDiffFilter(); });
    connect(m_prevChangeBtn, &QPushButton::clicked, this, [this]() { stepChange(-1); });
    connect(m_nextChangeBtn, &QPushButton::clicked, this, [this]() { stepChange(+1); });

    // Listen for diff data changes from BifrostDiffView (keyed by `this` so it
    // is removed precisely on destruction).
    auto& state = BifrostPaneState::instance();
    state.addDiffObserver(this, [this]() { loadDiffFromState(); });

    populate();
    loadDiffFromState();
}

BifrostSidebarWidget::~BifrostSidebarWidget()
{
    // Remove exactly our own observer so a later notify can't fire on us after
    // we're destroyed (use-after-free).
    BifrostPaneState::instance().removeDiffObserver(this);
}

// ── Diff list from state ──────────────────────────────────────────────────────

void BifrostSidebarWidget::loadDiffFromState()
{
    auto& state = BifrostPaneState::instance();
    Ref<Metadata> diffData = state.activeDiffData;

    if (!diffData || !diffData->IsKeyValueStore())
    {
        m_diffHeaderLabel->clear();
        m_diffStatusLabel->setText("Load binaries in the Bifrost split view,\nthen click Run Diff.");
        m_diffTree->clear();
        return;
    }

    auto lm = diffData->Get("left");
    auto rm = diffData->Get("right");
    auto tm = diffData->Get("timestamp");

    QString leftName  = lm && lm->IsString() ? QString::fromStdString(lm->GetString())  : "(unknown)";
    QString rightName = rm && rm->IsString() ? QString::fromStdString(rm->GetString())   : "(unknown)";
    QString timestamp = tm && tm->IsString() ? QString::fromStdString(tm->GetString())   : "";

    auto funcsMeta = diffData->Get("functions");
    if (!funcsMeta || !funcsMeta->IsArray())
    {
        m_diffHeaderLabel->setText("<i>Diff data contains no function list.</i>");
        m_diffStatusLabel->clear();
        return;
    }

    int added = 0, removed = 0, identical = 0, changed = 0;

    m_diffTree->setSortingEnabled(false);
    m_diffTree->clear();

    for (auto& entry : funcsMeta->GetArray())
    {
        if (!entry || !entry->IsKeyValueStore()) continue;

        auto nm = entry->Get("name"), st = entry->Get("status");
        auto la = entry->Get("leftAddr"), ra = entry->Get("rightAddr");

        QString  name   = nm && nm->IsString()          ? QString::fromStdString(nm->GetString()) : "";
        QString  legacy = st && st->IsString()          ? QString::fromStdString(st->GetString()) : "";
        uint64_t laddr  = la && la->IsUnsignedInteger() ? la->GetUnsignedInteger() : 0;
        uint64_t raddr  = ra && ra->IsUnsignedInteger() ? ra->GetUnsignedInteger() : 0;

        // Schema-v2 fields (guarded so v1 diffs still load).
        auto sim  = entry->Get("similarity");
        auto conf = entry->Get("confidence");
        auto alg  = entry->Get("algorithm");
        double similarity = sim  && sim->IsDouble()  ? sim->GetDouble()  : 1.0;
        double confidence = conf && conf->IsDouble() ? conf->GetDouble() : 1.0;
        QString algorithm = alg  && alg->IsString()  ? QString::fromStdString(alg->GetString()) : "";

        QString status = diffDisplayStatus(legacy, similarity);
        addDiffRow(status, name, similarity, confidence, algorithm, laddr, raddr);

        if (status == "added")          ++added;
        else if (status == "removed")   ++removed;
        else if (status == "changed")   ++changed;
        else                            ++identical;
    }

    m_diffTree->setSortingEnabled(true);
    m_diffTree->sortByColumn(ColStatus, Qt::AscendingOrder);
    applyDiffFilter();

    m_diffHeaderLabel->setText(
        QString("<b>Left:</b> %1 &nbsp; <b>Right:</b> %2 &nbsp; <small>%3</small><br>"
                "<small>%4 identical &nbsp; "
                "<span style='color:#c93;'>%5 changed</span> &nbsp; "
                "<span style='color:#4a4;'>%6 added</span> &nbsp; "
                "<span style='color:#c44;'>%7 removed</span></small>")
            .arg(leftName.toHtmlEscaped(), rightName.toHtmlEscaped(),
                 timestamp.toHtmlEscaped())
            .arg(identical).arg(changed).arg(added).arg(removed));

    m_diffStatusLabel->clear();
}

// ── Diff item click → navigate the diff view panes ──────────────────────────

void BifrostSidebarWidget::onDiffItemClicked(QTreeWidgetItem* item, int)
{
    if (!item) return;

    uint64_t laddr  = item->data(0, RoleLeftAddr).toULongLong();
    uint64_t raddr  = item->data(0, RoleRightAddr).toULongLong();
    QString  status = item->data(0, RoleStatus).toString();

    auto& state = BifrostPaneState::instance();
    if (state.highlightEntry)
    {
        // Diff view is open: it navigates both panes and applies highlighting.
        state.highlightEntry(laddr, raddr, status.toStdString());
    }
    else
    {
        // Split view (no diff view): just move the panes.
        if (laddr && state.navigateLeft)  state.navigateLeft(laddr);
        if (raddr && state.navigateRight) state.navigateRight(raddr);
    }
}

// Populate the tree from all project files
void BifrostSidebarWidget::populate()
{
    m_tree->setSortingEnabled(false);
    m_tree->clear();

    UIContext* ctx = UIContext::activeContext();
    if (!ctx)
        return;

    const QString filter = m_filter->text();

    std::map<std::string, Ref<BinaryView>> openByProjectId;
    for (auto& [bv, _] : ctx->getAvailableBinaryViews())
    {
        auto pf = bv->GetFile()->GetProjectFile();
        if (pf)
            openByProjectId[pf->GetId()] = bv;
    }

    Ref<Project> project = ctx->getProject();
    if (!project || !project->IsOpen())
    {
        m_filter->setEnabled(false);
        m_refreshBtn->setEnabled(false);
        m_stack->setCurrentIndex(0);
        return;
    }

    m_filter->setEnabled(true);
    m_refreshBtn->setEnabled(true);

    for (auto& pfile : project->GetFiles())
    {
        QString label = QString::fromStdString(pfile->GetName());
        QString pfId  = QString::fromStdString(pfile->GetId());

        auto it = openByProjectId.find(pfile->GetId());
        if (it != openByProjectId.end())
        {
            addFunctions(it->second, label, pfId, filter);
        }
        else
        {
            Ref<BinaryView> bv = Load(pfile, false);
            if (bv)
                addFunctions(bv, label, pfId, filter);
        }
    }

    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(2, Qt::AscendingOrder);
    m_stack->setCurrentIndex(1);
}

void BifrostSidebarWidget::addFunctions(Ref<BinaryView> data,
                                         const QString& label,
                                         const QString& projectFileId,
                                         const QString& filter)
{
    const bool hasFilter = !filter.isEmpty();

    for (auto& func : data->GetAnalysisFunctionList())
    {
        QString name = QString::fromStdString(func->GetSymbol()->GetFullName());
        if (hasFilter && !name.contains(filter, Qt::CaseInsensitive))
            continue;

        QString addr = QString("0x%1").arg(func->GetStart(), 0, 16);

        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, name);
        item->setText(1, addr);
        item->setText(2, label);
        item->setData(0, RoleAddress,       QVariant::fromValue(func->GetStart()));
        item->setData(0, RoleProjectFileId, projectFileId);
    }
}

void BifrostSidebarWidget::applyFilter(const QString& /* text */)
{
    populate();
}

void BifrostSidebarWidget::onItemDoubleClicked(QTreeWidgetItem* item, int /* column */)
{
    if (!item)
        return;

    uint64_t addr    = item->data(0, RoleAddress).toULongLong();
    QString  pfId    = item->data(0, RoleProjectFileId).toString();

    UIContext* ctx = UIContext::activeContext();
    if (!ctx)
        return;

    for (auto& [bv, _] : ctx->getAvailableBinaryViews())
    {
        auto pf = bv->GetFile()->GetProjectFile();
        if (pf && QString::fromStdString(pf->GetId()) == pfId)
        {
            ctx->navigateForBinaryView(bv, addr);
            return;
        }
    }

    Ref<Project> project = ctx->getProject();
    if (project && project->IsOpen())
    {
        Ref<ProjectFile> pfile = project->GetFileById(pfId.toStdString());
        if (pfile)
            ctx->openProjectFile(pfile);
    }
}

void BifrostSidebarWidget::addDiffRow(const QString& status, const QString& name,
                                      double similarity, double confidence,
                                      const QString& algorithm,
                                      uint64_t leftAddr, uint64_t rightAddr)
{
    auto addrStr = [](uint64_t a) -> QString {
        return a ? QString("0x%1").arg(a, 0, 16) : QStringLiteral("—");
    };
    const bool scored = (status != "added" && status != "removed");

    auto* item = new QTreeWidgetItem(m_diffTree);
    item->setText(ColStatus,     status);
    item->setText(ColFunction,   name);
    item->setText(ColSimilarity, scored ? QString::number(similarity, 'f', 2) : QStringLiteral("—"));
    item->setText(ColConfidence, scored ? QString::number(confidence, 'f', 2) : QStringLiteral("—"));
    item->setText(ColAlgorithm,  algorithm);
    item->setText(ColLeftAddr,   addrStr(leftAddr));
    item->setText(ColRightAddr,  addrStr(rightAddr));

    item->setData(0, RoleLeftAddr,  QVariant::fromValue(leftAddr));
    item->setData(0, RoleRightAddr, QVariant::fromValue(rightAddr));
    item->setData(0, RoleStatus,    status);

    QColor bg = diffStatusColor(status);
    if (bg.alpha() > 0)
        for (int c = 0; c < DiffColumnCount; ++c)
            item->setBackground(c, bg);
}

void BifrostSidebarWidget::applyDiffFilter()
{
    const int idx = m_diffFilter ? m_diffFilter->currentIndex() : 0;
    for (int i = 0; i < m_diffTree->topLevelItemCount(); ++i)
    {
        auto* item = m_diffTree->topLevelItem(i);
        const QString st = item->data(0, RoleStatus).toString();
        bool show = true;
        switch (idx)
        {
        case 1: show = (st != "identical"); break;          // Changed / added / removed
        case 2: show = (st == "changed");   break;
        case 3: show = (st == "added");     break;
        case 4: show = (st == "removed");   break;
        default: show = true;               break;          // All
        }
        item->setHidden(!show);
    }
}

void BifrostSidebarWidget::stepChange(int dir)
{
    const int n = m_diffTree->topLevelItemCount();
    if (n == 0) return;

    QTreeWidgetItem* cur = m_diffTree->currentItem();
    int curIdx = cur ? m_diffTree->indexOfTopLevelItem(cur) : (dir > 0 ? -1 : n);

    for (int step = 1; step <= n; ++step)
    {
        int i = curIdx + dir * step;
        if (i < 0 || i >= n) break;            // no wrap-around
        QTreeWidgetItem* it = m_diffTree->topLevelItem(i);
        if (it->isHidden()) continue;
        if (it->data(0, RoleStatus).toString() == "identical") continue;
        m_diffTree->setCurrentItem(it);
        m_diffTree->scrollToItem(it);
        onDiffItemClicked(it, 0);
        return;
    }
}

void BifrostSidebarWidget::runDiff()
{
    auto& state = BifrostPaneState::instance();
    Ref<BinaryView> leftBv  = state.leftData;
    Ref<BinaryView> rightBv = state.rightData;

    if (!leftBv || !rightBv)
    {
        m_diffStatusLabel->setText("Load binaries in the Bifrost split view,\nthen click Run Diff.");
        m_diffHeaderLabel->clear();
        m_diffTree->clear();
        return;
    }

    QString leftName  = QString::fromStdString(bifrost::bvDisplayName(leftBv));
    QString rightName = QString::fromStdString(bifrost::bvDisplayName(rightBv));

    m_runDiffBtn->setEnabled(false);
    m_diffStatusLabel->setText("Running structural diff…");

    // Run on a worker thread; the callback fires on the main thread.
    QPointer<BifrostSidebarWidget> self = this;
    bifrost::ComputeAsync(leftBv, rightBv,
        [self, leftName, rightName](bifrost::DiffResult diff) {
            if (!self) return;
            self->m_runDiffBtn->setEnabled(true);
            self->applyDiffResult(diff, leftName, rightName);
        });
}

void BifrostSidebarWidget::applyDiffResult(const bifrost::DiffResult& diff,
                                           const QString& leftName, const QString& rightName)
{
    // Populate diff tree
    m_diffTree->setSortingEnabled(false);
    m_diffTree->clear();
    for (auto& fm : diff.functions)
    {
        QString nm = QString::fromStdString(!fm.leftName.empty() ? fm.leftName : fm.rightName);
        addDiffRow(QString::fromLatin1(bifrost::MatchStatusString(fm.status)), nm,
                   fm.similarity, fm.confidence,
                   QString::fromStdString(fm.algorithm), fm.leftAddr, fm.rightAddr);
    }
    m_diffTree->setSortingEnabled(true);
    m_diffTree->sortByColumn(ColStatus, Qt::AscendingOrder);
    applyDiffFilter();

    m_diffHeaderLabel->setText(
        QString("<b>Left:</b> %1 &nbsp; <b>Right:</b> %2<br>"
                "<small>%3 identical &nbsp; "
                "<span style='color:#c93;'>%4 changed</span> &nbsp; "
                "<span style='color:#4a4;'>%5 added</span> &nbsp; "
                "<span style='color:#c44;'>%6 removed</span></small>")
            .arg(leftName.toHtmlEscaped(), rightName.toHtmlEscaped())
            .arg(diff.identical).arg(diff.changed).arg(diff.added).arg(diff.removed));
    m_diffStatusLabel->clear();

    // Save to project metadata database
    UIContext* ctx = UIContext::activeContext();
    if (!ctx) return;
    Ref<Project> project = ctx->getProject();
    if (!project || !project->IsOpen()) return;

    std::vector<DiffFuncEntry> storeEntries = bifrost::toStoreEntries(diff);
    std::string timestamp = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
    std::string diffName = leftName.toStdString() + "-vs-" + rightName.toStdString();
    bifrostSaveDiff(diffName, leftName.toStdString(), rightName.toStdString(),
                    timestamp, storeEntries, project);
}

void BifrostSidebarWidget::notifyViewChanged(ViewFrame* /* frame */)
{
    populate();
}

void BifrostSidebarWidget::notifyThemeChanged()
{
    SidebarWidget::notifyThemeChanged();
}

// BifrostSidebarWidgetType

BifrostSidebarWidgetType* BifrostSidebarWidgetType::m_instance = nullptr;

BifrostSidebarWidgetType::BifrostSidebarWidgetType()
    : SidebarWidgetType(renderSvgIcon(":/bifrost/images/icon_sidebar.svg", 28), "Bifrost")
{
}

SidebarWidget* BifrostSidebarWidgetType::createWidget(
    ViewFrame* frame, Ref<BinaryView> data)
{
    return new BifrostSidebarWidget(frame, data);
}

void BifrostSidebarWidgetType::init()
{
    m_instance = new BifrostSidebarWidgetType();
    Sidebar::addSidebarWidgetType(m_instance);
}
