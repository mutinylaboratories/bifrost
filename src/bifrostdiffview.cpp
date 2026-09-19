#include "bifrostdiffview.h"
#include "bifrostdiffdb.h"
#include "bifrostdiffstore.h"
#include "bifrostfeatures.h"
#include "bifrostmatch.h"
#include "action.h"
#include "filecontext.h"
#include "fontsettings.h"
#include "pane.h"
#include "uicontext.h"

#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtGui/QShowEvent>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QVBoxLayout>

#include <map>

using namespace BinaryNinja;

// ── Helpers ────────────────────────────────────────────────────────────────────

/*static*/ bool BifrostDiffView::isSemanticToken(BNInstructionTextTokenType t)
{
    switch (t)
    {
    case InstructionToken: case RegisterToken: case OperandSeparatorToken:
    case TextToken: case BeginMemoryOperandToken: case EndMemoryOperandToken:
    case IntegerToken: case FloatingPointToken:
        return true;
    default: return false;
    }
}

/*static*/ std::string BifrostDiffView::blockSignature(Ref<BasicBlock> block)
{
    Ref<DisassemblySettings> settings = new DisassemblySettings();
    std::string sig;
    for (auto& line : block->GetDisassemblyText(settings))
    {
        for (auto& tok : line.tokens)
            if (isSemanticToken(tok.type)) { sig += tok.text; sig += ' '; }
        sig += '\n';
    }
    return sig;
}

/*static*/ QString BifrostDiffView::bvDisplayName(Ref<BinaryView> bv)
{
    if (!bv) return "(none)";
    auto pf = bv->GetFile()->GetProjectFile();
    if (pf) { auto n = pf->GetName(); if (!n.empty()) return QString::fromStdString(n); }
    auto p = bv->GetFile()->GetFilename();
    auto s = p.rfind('/');
    return QString::fromStdString((s != std::string::npos) ? p.substr(s + 1) : p);
}

// Strip common binary / database extensions so a diff saved against
// "foo.dylib" still resolves when the user has "foo.bndb" open.
//
// Trailing extensions only, and repeatedly, so "foo.dylib.bndb" and "foo.dylib"
// both reduce to "foo". This used to use QString::remove, which deletes the
// substring ANYWHERE in the name — "a.out" became "aut" and "libssl.so.1"
// became "libssl.1" — silently merging binaries that are actually distinct.
/*static*/ QString BifrostDiffView::normalizeName(QString n)
{
    for (bool stripped = true; stripped; )
    {
        stripped = false;
        for (auto ext : {QLatin1String(".bndb"), QLatin1String(".dylib"),
                         QLatin1String(".so"),   QLatin1String(".exe"),
                         QLatin1String(".dll"),  QLatin1String(".o"),
                         QLatin1String(".bin")})
            if (n.endsWith(ext, Qt::CaseInsensitive))
            {
                n.chop(ext.size());
                stripped = true;
                break;
            }
    }
    return n;
}

Ref<BinaryView> BifrostDiffView::findBvByName(const QString& name) const
{
    const QString target = normalizeName(name);

    // Score a candidate: 2 = analyzed view whose (extension-stripped) name
    // matches, 1 = raw view match, 0 = no match. Preferring the analyzed view
    // is what makes the panes show disassembly instead of a raw hex dump.
    auto score = [&](Ref<BinaryView> bv) -> int {
        if (!bv) return 0;
        if (normalizeName(bvDisplayName(bv)) != target) return 0;
        // A view whose FileContext is no longer open can never produce a pane —
        // makeSplitPane bails on exactly that — so it must never win. This is
        // what keeps the immortal singleton's stale leftData/rightData (which
        // outlive their FileContext, since ownership runs FileContext→BinaryView
        // and never the reverse) from shadowing the live, tab-backed view.
        if (!fileContextForBv(bv)) return 0;
        return (bv->GetTypeName() == "Raw") ? 1 : 2;
    };

    Ref<BinaryView> best;
    int bestScore = 0;
    auto consider = [&](Ref<BinaryView> bv) {
        int s = score(bv);
        if (s > bestScore) { bestScore = s; best = bv; }
    };

    auto& state = BifrostPaneState::instance();
    consider(state.leftData);
    consider(state.rightData);

    if (UIContext* ctx = UIContext::activeContext())
        for (auto& [bv, _] : ctx->getAvailableBinaryViews())
            consider(bv);

    return best;
}

static Ref<Function> funcAtAddr(Ref<BinaryView> bv, uint64_t addr)
{
    if (!bv || !addr) return nullptr;
    auto funcs = bv->GetAnalysisFunctionsForAddress(addr);
    for (auto& f : funcs) if (f->GetStart() == addr) return f;
    if (!funcs.empty()) return funcs[0];
    return nullptr;
}

/*static*/ FileContext* BifrostDiffView::fileContextForBv(Ref<BinaryView> bv)
{
    if (!bv) return nullptr;
    auto meta = bv->GetFile();
    for (auto* fc : FileContext::getOpenFileContexts())
        if (fc->getMetadata() == meta)
            return fc;
    return nullptr;
}

bool BifrostDiffView::sideHighlightSafe(ViewFrame* frame, Ref<BinaryView> bv) const
{
    // Our pane must be built, and the owning FileContext's current view frame
    // must have a materialized widget. This mirrors exactly the pointer BN
    // dereferences (without a null check) when a user highlight changes:
    // FileContext::GetCurrentView() → currentViewFrame->getCurrentWidget().
    if (!frame || !bv) return false;
    FileContext* fc = fileContextForBv(bv);
    if (!fc) return false;
    ViewFrame* cur = fc->getCurrentViewFrame();
    return cur && cur->getCurrentWidget() != nullptr;
}

SplitPaneWidget* BifrostDiffView::makeSplitPane(Ref<BinaryView> bv, ViewFrame*& frameOut,
                                                FileContext*& fcOut)
{
    FileContext* fc = fileContextForBv(bv);
    if (!fc) return nullptr;

    frameOut = new ViewFrame(m_frameSplit, fc, "Linear");
    fcOut    = fc;
    auto* pane = new ViewPane(frameOut);
    return new SplitPaneWidget(pane, fc);
}

/*static*/ void BifrostDiffView::detachFrame(ViewFrame*& frame, FileContext*& fc)
{
    if (frame && fc)
    {
        fc->removeFrame(frame);
        // removeFrame does not necessarily clear the current-frame pointer, and
        // leaving it pointing at a frame Qt is about to delete is exactly the
        // dangling-pointer crash this exists to prevent.
        if (fc->getCurrentViewFrame() == frame)
            fc->setCurrentViewFrame(nullptr);
    }
    frame = nullptr;
    fc    = nullptr;
}

// ── Construction ──────────────────────────────────────────────────────────────

BifrostDiffView::BifrostDiffView(QWidget* parent,
                                  Ref<Metadata> diffData,
                                  const QString& diffName)
    : QWidget(parent), m_diffName(diffName)
{
    init(diffData);
}

// Opened from a diff .bndb: the diff rides in the database's metadata.
BifrostDiffView::BifrostDiffView(QWidget* parent, Ref<BinaryView> diffBv)
    : QWidget(parent), m_diffBv(diffBv)
{
    m_diffName = normalizeName(bvDisplayName(diffBv));
    init(bifrostDiffFromBinaryView(diffBv));
}

QFont BifrostDiffView::getFont()
{
    return getMonospaceFont(this);
}

void BifrostDiffView::init(Ref<Metadata> diffData)
{
    // Register the widget↔View mapping so View::getViewFromWidget resolves
    // when BN opens this as a diff database's view.
    setupView(this);

    // Parse binary names so we can resolve BVs.
    if (diffData && diffData->IsKeyValueStore())
    {
        auto lm = diffData->Get("left");
        auto rm = diffData->Get("right");
        if (lm && lm->IsString()) m_leftBvName  = QString::fromStdString(lm->GetString());
        if (rm && rm->IsString()) m_rightBvName = QString::fromStdString(rm->GetString());
    }
    m_leftBv  = findBvByName(m_leftBvName);
    m_rightBv = findBvByName(m_rightBvName);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // The two panes side by side — diff list lives in the sidebar. Navigation
    // and block/instruction highlighting are driven entirely from sidebar clicks
    // (see navigateToEntry); there is no per-view toolbar.
    m_frameSplit = new QSplitter(Qt::Horizontal, this);
    m_frameSplit->setChildrenCollapsible(false);

    auto makePlaceholder = [&](const QString& bvName) -> QLabel* {
        auto* ph = new QLabel(
            QString("Loading: %1…").arg(bvName.isEmpty() ? "(unknown)" : bvName),
            m_frameSplit);
        ph->setAlignment(Qt::AlignCenter);
        ph->setWordWrap(true);
        return ph;
    };

    m_frameSplit->addWidget(makePlaceholder(m_leftBvName));
    m_frameSplit->addWidget(makePlaceholder(m_rightBvName));
    m_frameSplit->setSizes({500, 500});
    outerLayout->addWidget(m_frameSplit, 1);

    // Register as the active diff (nav callbacks + diff data), keyed by `this`.
    m_diffData = diffData;
    registerActive();

    bindSaveAction();

    // Defer pane creation until after createTabForWidget has parented us.
    QTimer::singleShot(0, this, [this]() {
        initPanes();
        // Notify sidebar now that panes are ready.
        BifrostPaneState::instance().notifyDiffChanged();
    });
}

void BifrostDiffView::bindSaveAction()
{
    // A diff opened FROM a .bndb is already a project file — leave File▸Save
    // alone there so BN keeps saving the database itself.
    if (m_diffBv)
        return;

    // BN registers its File▸Save action inside the closed-source UI, so resolve
    // the name at runtime instead of hard-coding a guess. Binding on this
    // view's own handler means it only applies while the diff view has focus.
    const std::set<QString> registered = UIAction::getAllRegisteredActions();

    QStringList bound;
    for (const QString& name : {QStringLiteral("Save File"), QStringLiteral("Save")})
    {
        if (!registered.count(name))
            continue;
        m_actionHandler.bindAction(name, UIAction(
            [this](const UIActionContext&) { saveDiffToProject(); },
            [this](const UIActionContext&) -> bool { return m_diffData != nullptr; }));
        bound << name;
    }

    // Logged so a name mismatch is diagnosable from the log rather than
    // presenting as "Save silently does nothing".
    if (bound.isEmpty())
        LogWarn("Bifrost: no save action matched; File>Save will not save the diff");
    else
        LogInfo("Bifrost: diff view bound save action(s): %s",
                qPrintable(bound.join(", ")));
}

void BifrostDiffView::saveDiffToProject()
{
    if (!m_diffData)
        return;

    UIContext* ctx       = UIContext::activeContext();
    Ref<Project> project = ctx ? ctx->getProject() : nullptr;
    if (!project || !project->IsOpen())
    {
        QMessageBox::warning(this, "Bifrost", "No project is open.");
        return;
    }

    QString name = m_diffName.isEmpty() ? QString("diff") : m_diffName;

    std::string error;
    if (!bifrostSaveDiffToProject(name.toStdString(), m_diffData, project, error))
    {
        QMessageBox::warning(this, "Bifrost",
                             QString("Could not save the diff: %1")
                                 .arg(QString::fromStdString(error)));
        return;
    }

    LogInfo("Bifrost: saved diff \"%s\" into the project", qPrintable(name));
}

BifrostDiffView::~BifrostDiffView()
{
    clearHighlights();

    // Detach our panes from their FileContexts while the frames are still
    // alive. This must happen here, in the derived destructor body, because Qt
    // deletes our child widgets afterwards — and a FileContext left pointing at
    // a deleted frame is a use-after-free on the next highlight.
    detachFrame(m_leftFrame,  m_leftFc);
    detachFrame(m_rightFrame, m_rightFc);

    // Clear our own callbacks/diff (owner-guarded so we never wipe another diff
    // view's state), then let the sidebar refresh from whatever is active now.
    auto& state = BifrostPaneState::instance();
    state.clearNav(this);
    state.clearDiff(this);
    state.notifyDiffChanged();
}

void BifrostDiffView::registerActive()
{
    auto& state = BifrostPaneState::instance();
    state.setNav(this,
        [this](uint64_t addr) { if (m_leftFrame  && m_leftBv)  m_leftFrame->navigate(m_leftBv, addr); },
        [this](uint64_t addr) { if (m_rightFrame && m_rightBv) m_rightFrame->navigate(m_rightBv, addr); },
        [this](uint64_t l, uint64_t r, const std::string& status) {
            navigateToEntry(l, r, QString::fromStdString(status));
        });
    state.setDiff(this, m_diffData);
}

void BifrostDiffView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Re-activate when this tab is shown so switching between open diff views
    // hands the sidebar and panes to the one in front.
    registerActive();
    BifrostPaneState::instance().notifyDiffChanged();

    // A pane that failed to build has no other way back: removing the toolbar
    // deleted reloadPanes(), and the retry chain never restarts on its own.
    // Bringing the tab to the front re-arms the budget, so opening the missing
    // binary by hand and switching back now recovers. The auto-open latches are
    // deliberately NOT reset — re-opening on every tab switch would be noise,
    // and a manual open is already visible to initPanes.
    const bool stuck = (!m_leftFrame  && !m_leftBvName.isEmpty())
                    || (!m_rightFrame && !m_rightBvName.isEmpty());
    if (stuck && !m_retryPending)
    {
        m_resolveAttempts = 0;
        initPanes();
    }
}

// ── Deferred pane initialisation ──────────────────────────────────────────────

void BifrostDiffView::initPanes()
{
    // Un-latch a resolved-but-unusable view before re-resolving. Without this a
    // BinaryView whose FileContext has closed stays latched forever, so every
    // retry re-runs makeSplitPane on it and fails identically. Only done for a
    // side with no frame — a built pane's BinaryView is still in use by the
    // navigation callbacks registerActive() installed.
    if (!m_leftFrame  && m_leftBv  && !fileContextForBv(m_leftBv))  m_leftBv  = nullptr;
    if (!m_rightFrame && m_rightBv && !fileContextForBv(m_rightBv)) m_rightBv = nullptr;

    if (!m_leftBv  && !m_leftBvName.isEmpty())  m_leftBv  = findBvByName(m_leftBvName);
    if (!m_rightBv && !m_rightBvName.isEmpty()) m_rightBv = findBvByName(m_rightBvName);

    // Build a side only if it isn't already built, so retries (from the async
    // auto-open) never disturb a pane that is already showing. Returns true when
    // a new ViewFrame was created for this side.
    auto buildSide = [&](Ref<BinaryView> bv, const QString& bvName,
                         ViewFrame*& frameOut, SplitPaneWidget*& widgetOut,
                         FileContext*& fcOut,
                         int splitterIndex) -> bool
    {
        if (frameOut) return false;   // already built — leave it alone

        QWidget* replacement = nullptr;
        bool built = false;
        if (bv)
        {
            widgetOut = makeSplitPane(bv, frameOut, fcOut);
            if (widgetOut)
            {
                replacement = widgetOut;
                built = true;
                // Point the frame at the analyzed view's first function so it
                // opens on disassembly rather than a raw hex dump.
                if (frameOut)
                {
                    uint64_t addr = 0;
                    auto funcs = bv->GetAnalysisFunctionList();
                    if (!funcs.empty()) addr = funcs[0]->GetStart();
                    else                addr = bv->GetEntryPoint();
                    frameOut->navigate(bv, addr);
                }
            }
        }
        if (!replacement)
        {
            auto* ph = new QLabel(
                QString("Opening %1 from the project…\n\n"
                        "If this persists, open %1 yourself, then switch away "
                        "from this tab and back.")
                    .arg(bvName.isEmpty() ? "(unknown)" : bvName),
                m_frameSplit);
            ph->setAlignment(Qt::AlignCenter);
            ph->setWordWrap(true);
            replacement = ph;
        }

        QWidget* old = m_frameSplit->widget(splitterIndex);
        m_frameSplit->replaceWidget(splitterIndex, replacement);
        delete old;
        return built;
    };

    bool builtLeft  = buildSide(m_leftBv,  m_leftBvName,  m_leftFrame,
                                m_leftWidget,  m_leftFc,  0);
    bool builtRight = buildSide(m_rightBv, m_rightBvName, m_rightFrame,
                                m_rightWidget, m_rightFc, 1);

    // Kick ViewPaneHeaders to install the IL-level subtype widget (new frames only).
    auto connectKick = [](ViewFrame* frame, SplitPaneWidget* w) {
        if (!frame || !w) return;
        QObject::connect(frame, &ViewFrame::notifyViewChanged, w,
            [w](ViewFrame*) {
                w->updateStatus();
                w->enumerateViewPanes([](ViewPane* vp) {
                    vp->sendViewChange();
                    vp->updateStatus();
                });
            }, Qt::QueuedConnection);
    };
    if (builtLeft)  connectKick(m_leftFrame,  m_leftWidget);
    if (builtRight) connectKick(m_rightFrame, m_rightWidget);

    // If a side is still missing, open it from the project and retry.
    ensureBinariesOpen();
}

void BifrostDiffView::ensureBinariesOpen()
{
    constexpr int kMaxResolveAttempts = 20;   // ~14s at 700ms

    // Gate on whether the PANE was built, not on whether the name resolved to a
    // BinaryView. A resolved view with no open FileContext yields a placeholder
    // with no frame; treating that as "done" is what left one side stuck
    // forever, with the auto-open never attempted and no retry ever armed.
    bool leftMissing  = !m_leftFrame  && !m_leftBvName.isEmpty();
    bool rightMissing = !m_rightFrame && !m_rightBvName.isEmpty();
    if (!leftMissing && !rightMissing)
        return;   // both panes built — done

    UIContext* ctx = UIContext::activeContext();
    Ref<Project> project = ctx ? ctx->getProject() : nullptr;

    // Kick the async open once PER SIDE. A single shared latch meant whichever
    // side was missing first consumed the only attempt, so the other side was
    // never opened even when it later turned out to be missing too.
    if (ctx && project && project->IsOpen())
    {
        auto openMatch = [&](const QString& name) {
            const QString target = normalizeName(name);
            for (auto& pfile : project->GetFiles())
                if (normalizeName(QString::fromStdString(pfile->GetName())) == target)
                {
                    ctx->openProjectFile(pfile);
                    return;
                }
            // Silently opening nothing here used to be indistinguishable from a
            // slow open, and the pane just sat on the placeholder forever.
            LogWarn("Bifrost: no project file matches \"%s\"; its pane cannot be opened",
                    qPrintable(name));
        };
        if (leftMissing  && !m_autoOpenTriedLeft)  { m_autoOpenTriedLeft  = true; openMatch(m_leftBvName);  }
        if (rightMissing && !m_autoOpenTriedRight) { m_autoOpenTriedRight = true; openMatch(m_rightBvName); }
    }

    // Retry building the still-missing panes once the async open completes.
    // m_retryPending keeps concurrent chains from forking (showEvent can also
    // start one), which would burn the attempt budget several times over.
    if (!m_retryPending && m_resolveAttempts < kMaxResolveAttempts)
    {
        ++m_resolveAttempts;
        m_retryPending = true;
        QTimer::singleShot(700, this, [this]() { m_retryPending = false; initPanes(); });
    }
}

// ── Navigation (called by sidebar) ───────────────────────────────────────────

void BifrostDiffView::navigateToEntry(uint64_t leftAddr, uint64_t rightAddr,
                                       const QString& status)
{
    if (!m_leftBv  && !m_leftBvName.isEmpty())  m_leftBv  = findBvByName(m_leftBvName);
    if (!m_rightBv && !m_rightBvName.isEmpty()) m_rightBv = findBvByName(m_rightBvName);

    Ref<Function> lf = funcAtAddr(m_leftBv,  leftAddr);
    Ref<Function> rf = funcAtAddr(m_rightBv, rightAddr);

    // Apply highlighting first so the view re-render it triggers can't undo the
    // scroll — navigation is the last thing we do below.
    clearHighlights();

    // Only mutate user highlights on a side whose view is materialized; setting
    // a highlight on a binary whose pane isn't live (e.g. still auto-opening)
    // makes BN deref a null current-view widget and crash (see sideHighlightSafe).
    const bool leftSafe  = sideHighlightSafe(m_leftFrame,  m_leftBv);
    const bool rightSafe = sideHighlightSafe(m_rightFrame, m_rightBv);

    // A matched pair (identical or changed) → per-block match highlighting.
    if ((status == "identical" || status == "changed" || status == "matched")
        && lf && rf && leftSafe && rightSafe)
    {
        applyBlockHighlights(lf, rf);
        m_prevLeftFunc = lf; m_prevRightFunc = rf;
    }
    else if (status == "removed" && lf && leftSafe)
    {
        for (auto& bb : lf->GetBasicBlocks())
            bb->SetUserBasicBlockHighlight(RedHighlightColor);
        m_prevLeftFunc = lf;
    }
    else if (status == "added" && rf && rightSafe)
    {
        for (auto& bb : rf->GetBasicBlocks())
            bb->SetUserBasicBlockHighlight(GreenHighlightColor);
        m_prevRightFunc = rf;
    }

    // Navigate both panes last. Prefer navigateToFunction (jumps to the function
    // and centers it) and fall back to a raw address navigate.
    auto go = [](ViewFrame* frame, Ref<BinaryView> bv, Ref<Function> fn, uint64_t addr) {
        if (!frame || !addr) return;
        if (fn) frame->navigateToFunction(fn, addr);
        else    frame->navigate(bv, addr);
    };
    go(m_leftFrame,  m_leftBv,  lf, leftAddr);
    go(m_rightFrame, m_rightBv, rf, rightAddr);
}

// ── Block highlighting ────────────────────────────────────────────────────────

void BifrostDiffView::clearBlockHighlights(Ref<Function> func)
{
    if (!func) return;
    BNHighlightColor none{}; none.style = StandardHighlightColor;
    none.color = NoHighlightColor; none.alpha = 0;
    for (auto& bb : func->GetBasicBlocks()) bb->SetUserBasicBlockHighlight(none);
}

void BifrostDiffView::clearHighlights()
{
    BNHighlightColor none{}; none.style = StandardHighlightColor;
    none.color = NoHighlightColor; none.alpha = 0;

    // Clearing sets NoHighlight, which goes through the same BN refresh path as
    // setting one, so it is only safe while the side's view is materialized.
    const bool leftSafe  = sideHighlightSafe(m_leftFrame,  m_leftBv);
    const bool rightSafe = sideHighlightSafe(m_rightFrame, m_rightBv);

    if (leftSafe)  clearBlockHighlights(m_prevLeftFunc);
    if (rightSafe) clearBlockHighlights(m_prevRightFunc);

    if (m_prevLeftFunc && leftSafe)
        for (uint64_t a : m_leftInstrHi)
            m_prevLeftFunc->SetUserInstructionHighlight(m_prevLeftFunc->GetArchitecture(), a, none);
    if (m_prevRightFunc && rightSafe)
        for (uint64_t a : m_rightInstrHi)
            m_prevRightFunc->SetUserInstructionHighlight(m_prevRightFunc->GetArchitecture(), a, none);

    m_leftInstrHi.clear();
    m_rightInstrHi.clear();
    m_prevLeftFunc = m_prevRightFunc = nullptr;
}

void BifrostDiffView::applyBlockHighlights(Ref<Function> lf, Ref<Function> rf)
{
    if (!lf || !rf || !m_leftBv || !m_rightBv) return;

    // Structural basic-block match via the engine (replaces positional zip).
    bifrost::FeatureExtractor le(m_leftBv), re(m_rightBv);
    bifrost::FuncFeatures lFeat = le.ExtractFunction(lf);
    bifrost::FuncFeatures rFeat = re.ExtractFunction(rf);
    bifrost::FuncMatch fm;
    bifrost::Matcher::MatchBlocks(lFeat, rFeat, fm);

    std::map<uint64_t, Ref<BasicBlock>> lBlocks, rBlocks;
    for (auto& b : lf->GetBasicBlocks()) lBlocks[b->GetStart()] = b;
    for (auto& b : rf->GetBasicBlocks()) rBlocks[b->GetStart()] = b;

    auto colorFor = [](bifrost::MatchStatus s) -> BNHighlightStandardColor {
        switch (s)
        {
        case bifrost::MatchStatus::Identical: return NoHighlightColor;
        case bifrost::MatchStatus::Changed:   return OrangeHighlightColor;
        case bifrost::MatchStatus::Added:     return GreenHighlightColor;
        case bifrost::MatchStatus::Removed:   return RedHighlightColor;
        }
        return NoHighlightColor;
    };

    for (auto& bm : fm.blocks)
    {
        BNHighlightStandardColor col = colorFor(bm.status);
        if (bm.leftAddr)
            if (auto it = lBlocks.find(bm.leftAddr); it != lBlocks.end())
                it->second->SetUserBasicBlockHighlight(col);
        if (bm.rightAddr)
            if (auto it = rBlocks.find(bm.rightAddr); it != rBlocks.end())
                it->second->SetUserBasicBlockHighlight(col);

        if (bm.status == bifrost::MatchStatus::Changed && bm.leftAddr && bm.rightAddr)
        {
            auto li = lBlocks.find(bm.leftAddr), ri = rBlocks.find(bm.rightAddr);
            if (li != lBlocks.end() && ri != rBlocks.end())
                highlightInstructionDiffs(lf, rf, li->second, ri->second);
        }
    }
}

// Concatenate a disassembly line's token text, dropping address tokens (which
// differ under relocation without a semantic change) so immediates still count.
static std::string normLine(const BinaryNinja::DisassemblyTextLine& line)
{
    std::string s;
    for (auto& tok : line.tokens)
    {
        if (tok.type == PossibleAddressToken || tok.type == CodeRelativeAddressToken)
            continue;
        s += tok.text;
    }
    return s;
}

void BifrostDiffView::highlightInstructionDiffs(Ref<Function> lf, Ref<Function> rf,
                                                Ref<BasicBlock> lb, Ref<BasicBlock> rb)
{
    Ref<DisassemblySettings> settings = new DisassemblySettings();
    auto ll = lb->GetDisassemblyText(settings);
    auto rl = rb->GetDisassemblyText(settings);

    Ref<Architecture> larch = lf->GetArchitecture();
    Ref<Architecture> rarch = rf->GetArchitecture();

    auto hlLeft = [&](uint64_t addr) {
        lf->SetUserInstructionHighlight(larch, addr, OrangeHighlightColor);
        m_leftInstrHi.push_back(addr);
    };
    auto hlRight = [&](uint64_t addr) {
        rf->SetUserInstructionHighlight(rarch, addr, OrangeHighlightColor);
        m_rightInstrHi.push_back(addr);
    };

    size_t shared = std::min(ll.size(), rl.size());
    for (size_t i = 0; i < shared; ++i)
        if (normLine(ll[i]) != normLine(rl[i])) { hlLeft(ll[i].addr); hlRight(rl[i].addr); }
    for (size_t i = shared; i < ll.size(); ++i) hlLeft(ll[i].addr);
    for (size_t i = shared; i < rl.size(); ++i) hlRight(rl[i].addr);
}

// ── BifrostDiffViewType ───────────────────────────────────────────────────────

BifrostDiffViewType* BifrostDiffViewType::m_instance = nullptr;

BifrostDiffViewType::BifrostDiffViewType()
    : ViewType("BifrostDiff", "Bifrost Diff")
{
}

int BifrostDiffViewType::getPriority(Ref<BinaryView> data, const QString& /* filename */)
{
    // Claim only databases that actually carry a diff; every ordinary binary
    // scores 0 and is left to the built-in view types.
    return bifrostDiffFromBinaryView(data) ? 100 : 0;
}

QWidget* BifrostDiffViewType::create(Ref<BinaryView> data, ViewFrame* /* viewFrame */)
{
    return new BifrostDiffView(nullptr, data);
}

void BifrostDiffViewType::init()
{
    m_instance = new BifrostDiffViewType();
    ViewType::registerViewType(m_instance);
}
