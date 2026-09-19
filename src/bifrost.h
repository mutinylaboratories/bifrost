#pragma once

#include "bifroststate.h"
#include "linearview.h"
#include "uicontext.h"
#include "viewframe.h"
#include "viewtype.h"

#include <QtWidgets/QPushButton>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QWidget>

// BifrostContainer holds two LinearViews side by side in a horizontal splitter.
// Each pane has a binary picker button so the user can independently point each
// side at any currently-open binary. Pane assignments are persisted to the active
// Binary Ninja project so they are restored automatically on next open.
class BifrostContainer : public QWidget, public ViewContainer
{
    Q_OBJECT

    QSplitter* m_splitter;
    LinearView* m_leftView;
    LinearView* m_rightView;
    View* m_activeView;
    ViewFrame* m_frame;

    QPushButton* m_leftPickerBtn;
    QPushButton* m_rightPickerBtn;

    void showPicker(bool isLeft);
    void loadLeft(BinaryNinja::Ref<BinaryNinja::BinaryView> data);
    void loadRight(BinaryNinja::Ref<BinaryNinja::BinaryView> data);

    void saveProjectState() const;
    void restoreProjectState();

    // Pushes current left/right views (data + navigate callbacks) into
    // BifrostPaneState so the sidebar and diff view can drive them.
    void pushPaneState();

    // Returns the binary view from the currently available views whose backing
    // file path matches `path`, or nullptr if not found.
    BinaryNinja::Ref<BinaryNinja::BinaryView> findViewByPath(
        const std::string& path,
        const std::vector<std::pair<BinaryNinja::Ref<BinaryNinja::BinaryView>, QString>>& views) const;

    QString displayName(BinaryNinja::Ref<BinaryNinja::BinaryView> data) const;

public:
    explicit BifrostContainer(QWidget* parent, BinaryNinja::Ref<BinaryNinja::BinaryView> data, ViewFrame* frame);
    virtual ~BifrostContainer() override;

    virtual View* getView() override { return m_activeView; }

private Q_SLOTS:
    void onFocusChanged(QWidget* old, QWidget* now);
};

// BifrostViewType registers the "Bifrost" view in the view selector.
class BifrostViewType : public ViewType
{
    static BifrostViewType* m_instance;

public:
    BifrostViewType();

    virtual int getPriority(BinaryNinja::Ref<BinaryNinja::BinaryView> data, const QString& filename) override;
    virtual QWidget* create(BinaryNinja::Ref<BinaryNinja::BinaryView> data, ViewFrame* viewFrame) override;

    static void init();
};
