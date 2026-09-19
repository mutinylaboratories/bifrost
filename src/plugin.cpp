#include "bifrost.h"
#include "bifrostdiffdialog.h"
#include "bifrostdiffstore.h"
#include "bifrostdiffview.h"
#include "bifrostmanagedialog.h"
#include "bifrostsidebar.h"
#include "action.h"
#include "uicontext.h"

#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

using namespace BinaryNinja;

extern "C"
{
    BN_DECLARE_UI_ABI_VERSION

    BINARYNINJAPLUGIN bool UIPluginInit()
    {
        BifrostViewType::init();
        BifrostDiffViewType::init();
        BifrostSidebarWidgetType::init();

        // ── Plugins → Bifrost → Run Diff… ──────────────────────────────────
        UIAction::registerAction("Bifrost\\Run Diff...");
        UIActionHandler::globalActions()->bindAction(
            "Bifrost\\Run Diff...",
            UIAction(
                [](const UIActionContext& ctx) {
                    QWidget* parent = ctx.context ? ctx.context->mainWindow() : nullptr;
                    BifrostDiffDialog dlg(parent);
                    dlg.exec();
                },
                [](const UIActionContext& ctx) -> bool {
                    auto* uiCtx = ctx.context;
                    if (!uiCtx) return false;
                    auto project = uiCtx->getProject();
                    return project && project->IsOpen();
                }));
        Menu::mainMenu("Plugins")->addAction("Bifrost\\Run Diff...", "Bifrost");

        // ── Plugins → Bifrost → Manage Diffs… ──────────────────────────────
        UIAction::registerAction("Bifrost\\Manage Diffs...");
        UIActionHandler::globalActions()->bindAction(
            "Bifrost\\Manage Diffs...",
            UIAction(
                [](const UIActionContext& ctx) {
                    QWidget* parent = ctx.context ? ctx.context->mainWindow() : nullptr;
                    BifrostManageDiffsDialog dlg(parent);
                    dlg.exec();
                },
                [](const UIActionContext& ctx) -> bool {
                    auto* uiCtx = ctx.context;
                    if (!uiCtx) return false;
                    auto project = uiCtx->getProject();
                    if (!project || !project->IsOpen()) return false;
                    return !bifrostListDiffs(project).empty();
                }));
        Menu::mainMenu("Plugins")->addAction("Bifrost\\Manage Diffs...", "Bifrost");

        return true;
    }
}
