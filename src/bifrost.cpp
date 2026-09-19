#include "bifrost.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QMenu>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QVBoxLayout>

using namespace BinaryNinja;

static constexpr const char* kMetaLeft  = "bifrost.left";
static constexpr const char* kMetaRight = "bifrost.right";

// BifrostContainer

BifrostContainer::BifrostContainer(QWidget* parent, Ref<BinaryView> data, ViewFrame* frame)
    : QWidget(parent), m_frame(frame)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header bar: one picker button per pane, split at the midpoint
    auto* header = new QWidget(this);
    header->setFixedHeight(24);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(2, 0, 2, 0);
    headerLayout->setSpacing(0);

    m_leftPickerBtn = new QPushButton(displayName(data), header);
    m_leftPickerBtn->setFlat(true);
    m_leftPickerBtn->setToolTip("Choose binary for left pane");

    m_rightPickerBtn = new QPushButton(displayName(data), header);
    m_rightPickerBtn->setFlat(true);
    m_rightPickerBtn->setToolTip("Choose binary for right pane");

    headerLayout->addWidget(m_leftPickerBtn, 1);
    headerLayout->addWidget(m_rightPickerBtn, 1);

    layout->addWidget(header);

    // Splitter with two LinearViews — both start on the same binary
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);

    m_leftView  = new LinearView(data, frame);
    m_rightView = new LinearView(data, frame);

    m_splitter->addWidget(m_leftView);
    m_splitter->addWidget(m_rightView);
    m_splitter->setSizes({500, 500});

    layout->addWidget(m_splitter);

    m_activeView = m_leftView;

    connect(qApp, &QApplication::focusChanged, this, &BifrostContainer::onFocusChanged);
    connect(m_leftPickerBtn,  &QPushButton::clicked, this, [this]() { showPicker(true);  });
    connect(m_rightPickerBtn, &QPushButton::clicked, this, [this]() { showPicker(false); });

    // Restore prior pane assignments from the active project (if any),
    // then push initial state so the sidebar has something to show.
    restoreProjectState();
    pushPaneState();
}

BifrostContainer::~BifrostContainer()
{
    // Release our callbacks (and, if we're the active driver, our BinaryView
    // refs) from the singleton while BN's core is still alive. Both are
    // owner-guarded so a destroyed non-active container can't wipe an active
    // diff view's state. (Refs still held at process exit are handled by the
    // singleton being deliberately leaked — see BifrostPaneState::instance.)
    auto& s = BifrostPaneState::instance();
    if (s.navOwner == this)
        s.set(nullptr, nullptr);
    s.clearNav(this);
}

// --- Display helpers ---------------------------------------------------------

QString BifrostContainer::displayName(Ref<BinaryView> data) const
{
    if (!data)
        return "(none)";

    // Use the project browser name when available
    auto projectFile = data->GetFile()->GetProjectFile();
    if (projectFile)
    {
        std::string name = projectFile->GetName();
        if (!name.empty())
            return QString::fromStdString(name);
    }

    // Outside a project: basename of the raw filename
    std::string path = data->GetFile()->GetFilename();
    size_t s = path.rfind('/');
    return QString::fromStdString(
        (s != std::string::npos) ? path.substr(s + 1) : path);
}

Ref<BinaryView> BifrostContainer::findViewByPath(
    const std::string& path,
    const std::vector<std::pair<Ref<BinaryView>, QString>>& views) const
{
    for (auto& [bv, name] : views)
    {
        if (bv->GetFile()->GetFilename() == path)
            return bv;
    }
    return nullptr;
}

// --- Project persistence -----------------------------------------------------

void BifrostContainer::pushPaneState()
{
    auto& s = BifrostPaneState::instance();
    s.set(m_leftView->getData(), m_rightView->getData());
    s.setNav(this,
             [this](uint64_t addr) { m_leftView->navigate(addr);  },
             [this](uint64_t addr) { m_rightView->navigate(addr); },
             nullptr);  // split view has no diff-highlight callback
}

void BifrostContainer::saveProjectState() const
{
    UIContext* ctx = UIContext::activeContext();
    if (!ctx)
        return;
    Ref<Project> project = ctx->getProject();
    if (!project || !project->IsOpen())
        return;

    project->StoreMetadata(kMetaLeft,  new Metadata(m_leftView->getData()->GetFile()->GetFilename()));
    project->StoreMetadata(kMetaRight, new Metadata(m_rightView->getData()->GetFile()->GetFilename()));
}

void BifrostContainer::restoreProjectState()
{
    UIContext* ctx = UIContext::activeContext();
    if (!ctx)
        return;
    Ref<Project> project = ctx->getProject();
    if (!project || !project->IsOpen())
        return;

    Ref<Metadata> leftMeta  = project->QueryMetadata(kMetaLeft);
    Ref<Metadata> rightMeta = project->QueryMetadata(kMetaRight);
    if (!leftMeta && !rightMeta)
        return;

    auto views = ctx->getAvailableBinaryViews();

    if (leftMeta && leftMeta->IsString())
    {
        Ref<BinaryView> bv = findViewByPath(leftMeta->GetString(), views);
        if (bv)
            loadLeft(bv);
    }

    if (rightMeta && rightMeta->IsString())
    {
        Ref<BinaryView> bv = findViewByPath(rightMeta->GetString(), views);
        if (bv)
            loadRight(bv);
    }
}

// --- Binary picker -----------------------------------------------------------

void BifrostContainer::showPicker(bool isLeft)
{
    UIContext* ctx = UIContext::activeContext();
    if (!ctx)
        return;

    auto views = ctx->getAvailableBinaryViews();
    if (views.empty())
        return;

    QMenu menu(this);
    for (auto& [bv, name] : views)
    {
        QAction* action = menu.addAction(name);
        connect(action, &QAction::triggered, this, [this, bv = bv, isLeft]() {
            if (isLeft)
                loadLeft(bv);
            else
                loadRight(bv);
        });
    }

    QPushButton* btn = isLeft ? m_leftPickerBtn : m_rightPickerBtn;
    menu.exec(btn->mapToGlobal(QPoint(0, btn->height())));
}

void BifrostContainer::loadLeft(Ref<BinaryView> data)
{
    auto* next = new LinearView(data, m_frame);
    m_splitter->replaceWidget(0, next);
    delete m_leftView;
    m_leftView   = next;
    m_activeView = m_leftView;
    m_leftPickerBtn->setText(displayName(data));
    pushPaneState();
    saveProjectState();
}

void BifrostContainer::loadRight(Ref<BinaryView> data)
{
    auto* next = new LinearView(data, m_frame);
    m_splitter->replaceWidget(1, next);
    delete m_rightView;
    m_rightView = next;
    m_rightPickerBtn->setText(displayName(data));
    pushPaneState();
    saveProjectState();
}

// --- Focus tracking ----------------------------------------------------------

void BifrostContainer::onFocusChanged(QWidget* /* old */, QWidget* now)
{
    if (!now)
        return;

    for (QWidget* w = now; w; w = w->parentWidget())
    {
        if (w == m_leftView)
        {
            m_activeView = m_leftView;
            return;
        }
        if (w == m_rightView)
        {
            m_activeView = m_rightView;
            return;
        }
    }
}

// --- BifrostViewType ---------------------------------------------------------

BifrostViewType* BifrostViewType::m_instance = nullptr;

BifrostViewType::BifrostViewType()
    : ViewType("Bifrost", "Bifrost Split View")
{
}

int BifrostViewType::getPriority(Ref<BinaryView> data, const QString& /* filename */)
{
    if (data->GetLength() == 0)
        return 0;
    return 25;
}

QWidget* BifrostViewType::create(Ref<BinaryView> data, ViewFrame* viewFrame)
{
    return new BifrostContainer(nullptr, data, viewFrame);
}

void BifrostViewType::init()
{
    m_instance = new BifrostViewType();
    ViewType::registerViewType(m_instance);
}
