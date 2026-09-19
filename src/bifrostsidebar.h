#pragma once

#include "bifroststate.h"
#include "sidebarwidget.h"
#include "viewframe.h"

#include <QtCore/QPointer>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTreeWidget>

namespace bifrost { struct DiffResult; }

class BifrostSidebarWidget : public SidebarWidget
{
    Q_OBJECT

    // --- Functions tab ---
    QStackedWidget* m_stack;
    QLabel*         m_emptyLabel;
    QLineEdit*      m_filter;
    QTreeWidget*    m_tree;
    QPushButton*    m_refreshBtn;

    // --- Diff tab ---
    QLabel*         m_diffHeaderLabel;
    QLabel*         m_diffStatusLabel;
    QComboBox*      m_diffFilter;
    QTreeWidget*    m_diffTree;
    QPushButton*    m_runDiffBtn;
    QPushButton*    m_prevChangeBtn;
    QPushButton*    m_nextChangeBtn;

    void populate();
    void applyFilter(const QString& text);
    void addFunctions(BinaryNinja::Ref<BinaryNinja::BinaryView> data,
                      const QString& label,
                      const QString& projectFileId,
                      const QString& filter);
    void runDiff();
    // Populate the diff tree + header from a completed result and save it to the
    // project. Runs on the main thread (from the async diff completion).
    void applyDiffResult(const bifrost::DiffResult& diff,
                         const QString& leftName, const QString& rightName);
    void loadDiffFromState();

    // Add one row to the diff tree (shared by live diff and saved-diff load).
    void addDiffRow(const QString& status, const QString& name,
                    double similarity, double confidence, const QString& algorithm,
                    uint64_t leftAddr, uint64_t rightAddr);
    // Hide/show diff rows according to the m_diffFilter selection.
    void applyDiffFilter();
    // Step selection to the next (dir>0) / previous (dir<0) non-identical,
    // non-hidden diff row and navigate/highlight it.
    void stepChange(int dir);

public:
    BifrostSidebarWidget(ViewFrame* frame, BinaryNinja::Ref<BinaryNinja::BinaryView> data);
    ~BifrostSidebarWidget() override;

    void notifyViewChanged(ViewFrame* frame) override;
    void notifyThemeChanged() override;

private Q_SLOTS:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onDiffItemClicked(QTreeWidgetItem* item, int column);
};

class BifrostSidebarWidgetType : public SidebarWidgetType
{
    static BifrostSidebarWidgetType* m_instance;

public:
    BifrostSidebarWidgetType();

    SidebarWidgetLocation defaultLocation() const override
    {
        return SidebarWidgetLocation::RightBottom;
    }

    SidebarContextSensitivity contextSensitivity() const override
    {
        return GlobalSidebarContext;
    }

    SidebarWidget* createWidget(ViewFrame* frame, BinaryNinja::Ref<BinaryNinja::BinaryView> data) override;

    static void init();
};
