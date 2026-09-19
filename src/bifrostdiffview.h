#pragma once

#include "bifroststate.h"
#include "binaryninjaapi.h"
#include "pane.h"
#include "viewframe.h"
#include "viewtype.h"
#include "filecontext.h"

#include <QtWidgets/QSplitter>
#include <QtWidgets/QWidget>

// BifrostDiffView shows two full ViewFrames side by side (each with BN's
// native file-type / view-type / IL-type combo toolbar).  The diff function
// list is displayed in the BifrostSidebar (RightBottom) rather than inline.
// Navigation and block/instruction highlighting are driven entirely by clicks
// on that sidebar list — this view has no toolbar of its own. Use the ViewFrame
// headers' own view-type control to switch a pane to the CFG graph.
//
// It implements View so it can be produced by BifrostDiffViewType when a diff
// .bndb is opened from the project browser (BN resolves a View* from the widget
// via View::getViewFromWidget, and a null there is not survivable). When opened
// as a plain tab instead, m_diffBv is null and the View methods are inert.
class BifrostDiffView : public QWidget, public View
{
    Q_OBJECT

    QSplitter* m_frameSplit = nullptr;

    // The diff database this view was opened from, when it came from a .bndb
    // project file. Null when the view was opened as a plain tab.
    BinaryNinja::Ref<BinaryNinja::BinaryView> m_diffBv;

    ViewFrame*       m_leftFrame   = nullptr;
    ViewFrame*       m_rightFrame  = nullptr;
    SplitPaneWidget* m_leftWidget  = nullptr;
    SplitPaneWidget* m_rightWidget = nullptr;

    // The FileContext each pane's frame was created against. Kept so the frame
    // can be detached from it before Qt destroys our widget tree — see
    // detachFrame for why that is not optional.
    FileContext* m_leftFc  = nullptr;
    FileContext* m_rightFc = nullptr;

    // Detach a pane's ViewFrame from its FileContext and forget both.
    //
    // We construct these ViewFrames ourselves, outside BN's tab machinery, so
    // nothing informs the FileContext when Qt destroys our widget tree. It
    // holds a raw m_currentViewFrame pointer, which is then left dangling, and
    // the next user-highlight makes BN dereference the freed frame — observed
    // as a QWidget* whose bytes were reused JSON text, crashing in
    // ViewFrame::getTypeForView.
    static void detachFrame(ViewFrame*& frame, FileContext*& fc);

    BinaryNinja::Ref<BinaryNinja::BinaryView> m_leftBv;
    BinaryNinja::Ref<BinaryNinja::BinaryView> m_rightBv;
    QString m_leftBvName;
    QString m_rightBvName;

    // The diff this view shows, kept so the view can re-register itself as the
    // active diff whenever its tab is shown (so multiple diff views coexist).
    BinaryNinja::Ref<BinaryNinja::Metadata> m_diffData;
    QString m_diffName;

    // Bind BN's File▸Save to "write this diff into the project as a .bndb".
    // Only done for a diff opened as a plain tab — one opened FROM a .bndb is
    // already a project file, and BN's own save should keep handling it.
    void bindSaveAction();
    void saveDiffToProject();

    // Register this view as the active diff driver (nav callbacks + diff data).
    void registerActive();

    // Shared construction: both constructors funnel here once they have the
    // diff metadata (from the caller, or read out of a diff database).
    void init(BinaryNinja::Ref<BinaryNinja::Metadata> diffData);

    BinaryNinja::Ref<BinaryNinja::Function> m_prevLeftFunc;
    BinaryNinja::Ref<BinaryNinja::Function> m_prevRightFunc;

    // Auto-open-from-project state: openProjectFile is async, so we open each
    // missing binary once and retry building the panes on a bounded timer.
    // The latch is per side — a shared one let whichever side was missing first
    // consume the only attempt, stranding the other.
    bool m_autoOpenTriedLeft  = false;
    bool m_autoOpenTriedRight = false;
    int  m_resolveAttempts = 0;
    bool m_retryPending = false;

    // Instruction addresses currently highlighted, so they can be cleared on
    // the next navigation without scanning the whole function.
    std::vector<uint64_t> m_leftInstrHi;
    std::vector<uint64_t> m_rightInstrHi;

    static std::string blockSignature(BinaryNinja::Ref<BinaryNinja::BasicBlock> block);
    static QString     bvDisplayName(BinaryNinja::Ref<BinaryNinja::BinaryView> bv);
    BinaryNinja::Ref<BinaryNinja::BinaryView> findBvByName(const QString& name) const;

    static FileContext* fileContextForBv(BinaryNinja::Ref<BinaryNinja::BinaryView> bv);

    // True when it is safe to set/clear user highlights on functions in `bv`:
    // its pane `frame` is built AND the BinaryView's current view frame has a
    // materialized widget. BN's highlight-changed refresh calls
    // FileContext::GetCurrentView() → ViewFrame::getTypeForView(getCurrentWidget())
    // and does NOT null-check the widget, so highlighting a binary whose view
    // isn't materialized yet (e.g. still auto-opening) crashes inside BN.
    bool sideHighlightSafe(ViewFrame* frame,
                           BinaryNinja::Ref<BinaryNinja::BinaryView> bv) const;

    SplitPaneWidget* makeSplitPane(BinaryNinja::Ref<BinaryNinja::BinaryView> bv,
                                   ViewFrame*& frameOut,
                                   FileContext*& fcOut);
    void initPanes();
    // Open any still-missing binaries from the project (async) and schedule a
    // bounded retry of initPanes so their panes appear once loaded.
    void ensureBinariesOpen();

public:
    // `diffName` is the diff's stored name (not the tab title) — it is the name
    // used if the diff is later saved into the project as "<diffName>.bndb".
    explicit BifrostDiffView(QWidget* parent,
                             BinaryNinja::Ref<BinaryNinja::Metadata> diffData,
                             const QString& diffName);
    // Opened from a diff .bndb in the project browser: the diff is read out of
    // the database's metadata (see bifrostDiffFromBinaryView).
    explicit BifrostDiffView(QWidget* parent,
                             BinaryNinja::Ref<BinaryNinja::BinaryView> diffBv);
    virtual ~BifrostDiffView() override;

    // Called by the sidebar when the user clicks a diff entry.
    void navigateToEntry(uint64_t leftAddr, uint64_t rightAddr, const QString& status);

    // ── View ────────────────────────────────────────────────────────────────
    // A diff view has no address space of its own; navigation happens through
    // the two embedded panes, driven from the sidebar.
    virtual BinaryViewRef getData() override { return m_diffBv; }
    virtual uint64_t getCurrentOffset() override { return 0; }
    virtual void setSelectionOffsets(BNAddressRange) override {}
    virtual bool navigate(uint64_t) override { return false; }
    virtual QFont getFont() override;

protected:
    void showEvent(QShowEvent* event) override;

public:

    // Strip trailing binary/database extensions ("foo.dylib.bndb" → "foo") so
    // names from different sources compare equal. Shared with the Run Diff
    // dialog so the two cannot drift apart.
    static QString normalizeName(QString n);

    static bool isSemanticToken(BNInstructionTextTokenType t);
    void clearBlockHighlights(BinaryNinja::Ref<BinaryNinja::Function> func);
    void applyBlockHighlights(BinaryNinja::Ref<BinaryNinja::Function> lf,
                               BinaryNinja::Ref<BinaryNinja::Function> rf);

    // Clear all block and instruction highlights recorded from the previous
    // navigation.
    void clearHighlights();
    // Highlight differing instructions within a matched-but-changed block pair.
    void highlightInstructionDiffs(BinaryNinja::Ref<BinaryNinja::Function> lf,
                                   BinaryNinja::Ref<BinaryNinja::Function> rf,
                                   BinaryNinja::Ref<BinaryNinja::BasicBlock> lb,
                                   BinaryNinja::Ref<BinaryNinja::BasicBlock> rb);
};

// Renders a diff .bndb (a database carrying the "bifrost.diff" metadata key) as
// a BifrostDiffView, so double-clicking a saved diff in the project browser
// opens the diff walkthrough instead of a hex dump. Ordinary binaries score 0
// here, so this never competes for them.
class BifrostDiffViewType : public ViewType
{
    static BifrostDiffViewType* m_instance;

public:
    BifrostDiffViewType();

    virtual int getPriority(BinaryNinja::Ref<BinaryNinja::BinaryView> data,
                            const QString& filename) override;
    virtual QWidget* create(BinaryNinja::Ref<BinaryNinja::BinaryView> data,
                            ViewFrame* viewFrame) override;

    static void init();
};
