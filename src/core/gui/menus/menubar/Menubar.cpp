#include "Menubar.h"

#include "control/Control.h"
#include "gui/MainWindow.h"

#include "PageTypeSubmenu.h"
#include "PluginsSubmenu.h"
#include "RecentDocumentsSubmenu.h"
#include "ToolbarSelectionSubmenu.h"
#include "config-features.h"  // for ENABLE_PLUGINS

Menubar::Menubar() = default;
Menubar::~Menubar() noexcept = default;

void Menubar::populate(MainWindow* win) {
    Control* ctrl = win->getControl();

    recentDocumentsSubmenu = std::make_unique<RecentDocumentsSubmenu>(ctrl, GTK_APPLICATION_WINDOW(win->getWindow()));
    toolbarSelectionSubmenu =
            std::make_unique<ToolbarSelectionSubmenu>(win, ctrl->getSettings(), win->getToolMenuHandler());
    pageTypeSubmenu = std::make_unique<PageTypeSubmenu>(ctrl->getPageTypes(), ctrl->getPageBackgroundChangeController(),
                                                        ctrl->getSettings(), GTK_APPLICATION_WINDOW(win->getWindow()));
#ifdef ENABLE_PLUGINS
    pluginsSubmenu =
            std::make_unique<PluginsSubmenu>(ctrl->getPluginController(), GTK_APPLICATION_WINDOW(win->getWindow()));
#else
    // If plugins are disabled - hide the entire menu
    gtk_widget_hide(win->get("menuitemPlugin"));
#endif

    forEachSubmenu([&](auto& subm) { subm.addToMenubar(win); });
}

void Menubar::setDisabled(bool disabled) {
    forEachSubmenu([&](auto& subm) { subm.setDisabled(disabled); });
}
