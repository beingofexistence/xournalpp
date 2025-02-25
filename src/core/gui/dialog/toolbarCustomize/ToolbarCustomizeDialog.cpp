#include "ToolbarCustomizeDialog.h"

#include <cstddef>  // for size_t
#include <limits>   // for numeric_l...
#include <string>   // for allocator
#include <vector>   // for vector

#include <gdk-pixbuf/gdk-pixbuf.h>  // for GdkPixbuf
#include <glib-object.h>            // for G_CALLBACK

#include "control/Control.h"            // for Control
#include "control/settings/Settings.h"  // for Settings
#include "gui/Builder.h"
#include "gui/MainWindow.h"                                 // for MainWindow
#include "gui/ToolitemDragDrop.h"                           // for ToolItemD...
#include "gui/toolbarMenubar/AbstractToolItem.h"            // for AbstractT...
#include "gui/toolbarMenubar/ToolMenuHandler.h"             // for ToolMenuH...
#include "gui/toolbarMenubar/icon/ColorSelectImage.h"       // for ColorSele...
#include "gui/toolbarMenubar/icon/ToolbarSeparatorImage.h"  // for getNewToo...
#include "gui/toolbarMenubar/model/ColorPalette.h"          // for Palette
#include "util/Assert.h"                                    // for xoj_assert
#include "util/Color.h"                                     // for Color
#include "util/GListView.h"                                 // for GListView
#include "util/NamedColor.h"                                // for NamedColor
#include "util/gtk4_helper.h"
#include "util/i18n.h"  // for _
#include "util/raii/GObjectSPtr.h"

#include "ToolItemDragCurrentData.h"  // for ToolItemD...
#include "ToolbarDragDropHandler.h"   // for ToolbarDr...
#include "ToolbarDragDropHelper.h"    // for dragSourc...

class GladeSearchpath;

/*
 * struct used for data necessary for dragging
 * during toolbar customization
 */
struct ToolbarCustomizeDialog::ToolItemDragData {
    ToolbarCustomizeDialog* dlg;
    GtkWidget* icon;  ///< Currently must be an GtkImage
    AbstractToolItem* item;
    xoj::util::WidgetSPtr ebox;
};

struct ToolbarCustomizeDialog::ColorToolItemDragData {
    ToolbarCustomizeDialog* dlg;
    const NamedColor* namedColor;
    xoj::util::WidgetSPtr ebox;
};

// Separator and spacer
struct ToolbarCustomizeDialog::SeparatorData {
    ToolItemType type;
    int pos;
    SeparatorType separator;
    const char* label;
};

std::array<ToolbarCustomizeDialog::SeparatorData, 2> ToolbarCustomizeDialog::separators = {
        ToolbarCustomizeDialog::SeparatorData{TOOL_ITEM_SEPARATOR, 0, SeparatorType::SEPARATOR, _("Separator")},
        ToolbarCustomizeDialog::SeparatorData{TOOL_ITEM_SPACER, 1, SeparatorType::SPACER, _("Spacer")}};


constexpr auto UI_FILE = "toolbarCustomizeDialog.glade";
constexpr auto UI_DIALOG_NAME = "DialogCustomizeToolbar";

ToolbarCustomizeDialog::ToolbarCustomizeDialog(GladeSearchpath* gladeSearchPath, MainWindow* win,
                                               ToolbarDragDropHandler* handler):
        itemData(buildToolDataVector(*win->getToolMenuHandler()->getToolItems())),
        colorItemData(buildColorDataVector(handler->getControl()->getSettings()->getColorPalette())) {
    Builder builder(gladeSearchPath, UI_FILE);
    window.reset(GTK_WINDOW(builder.get(UI_DIALOG_NAME)));
    toolTable = GTK_GRID(builder.get("tbDefaultTools"));
    colorTable = GTK_GRID(builder.get("tbColor"));

    rebuildIconview();

    int i = 0;
    for (auto& data: colorItemData) {
        // In the dialog 5 colors are shown per row
        const int x = i % 5;
        const int y = i / 5;

        gtk_grid_attach(colorTable, data.ebox.get(), x, y, 1, 1);
        i++;
    }

    // init separator and spacer
    GtkWidget* tbSeparators = builder.get("tbSeparator");

    for (SeparatorData& data: separators) {
        GtkBox* box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 2));
        gtk_box_append(box, ToolbarSeparatorImage::newImage(data.separator));
        gtk_box_append(box, gtk_label_new(data.label));

        GtkWidget* ebox = gtk_event_box_new();
        gtk_container_add(GTK_CONTAINER(ebox), GTK_WIDGET(box));
        gtk_widget_show_all(ebox);

        // make ebox a drag source
        gtk_drag_source_set(ebox, GDK_BUTTON1_MASK, &ToolbarDragDropHelper::dropTargetEntry, 1, GDK_ACTION_MOVE);
        ToolbarDragDropHelper::dragSourceAddToolbar(ebox);

        g_signal_connect(ebox, "drag-begin", G_CALLBACK(toolitemDragBeginSeparator), &data);
        g_signal_connect(ebox, "drag-end", G_CALLBACK(toolitemDragEndSeparator), &data);
        g_signal_connect(ebox, "drag-data-get", G_CALLBACK(toolitemDragDataGetSeparator), &data);
        gtk_grid_attach(GTK_GRID(tbSeparators), ebox, data.pos, 0, 1, 1);
    }

    GtkWidget* target = builder.get("viewport1");
    // prepare drag & drop
    gtk_drag_dest_set(target, GTK_DEST_DEFAULT_ALL, nullptr, 0, GDK_ACTION_MOVE);
    ToolbarDragDropHelper::dragDestAddToolbar(target);

    g_signal_connect(target, "drag-data-received", G_CALLBACK(dragDataReceived), this);

    g_signal_connect_swapped(builder.get("btClose"), "clicked", G_CALLBACK(gtk_window_close), window.get());
    g_signal_connect_swapped(window.get(), "delete-event",
                             G_CALLBACK(+[](ToolbarDragDropHandler* h) { h->toolbarConfigDialogClosed(); }), handler);
}

ToolbarCustomizeDialog::~ToolbarCustomizeDialog() = default;

void ToolbarCustomizeDialog::toolitemDragBeginSeparator(GtkWidget* widget, GdkDragContext* context, void* data) {
    SeparatorData* sepData = static_cast<SeparatorData*>(data);
    ToolItemDragCurrentData::setData(sepData->type, -1, nullptr);
    GdkPixbuf* pixbuf = ToolbarSeparatorImage::getNewToolPixbuf(sepData->separator);
    gtk_drag_set_icon_pixbuf(context, pixbuf, -2, -2);
    g_object_unref(pixbuf);
}

void ToolbarCustomizeDialog::toolitemDragEndSeparator(GtkWidget* widget, GdkDragContext* context, void* unused) {
    ToolItemDragCurrentData::clearData();
}

void ToolbarCustomizeDialog::toolitemDragDataGetSeparator(GtkWidget* widget, GdkDragContext* context,
                                                          GtkSelectionData* selection_data, guint info, guint time,
                                                          void* data) {
    SeparatorData* sepData = static_cast<SeparatorData*>(data);
    ToolItemDragDropData* it = ToolitemDragDrop::ToolItemDragDropData_new(nullptr);
    it->type = sepData->type;

    gtk_selection_data_set(selection_data, ToolbarDragDropHelper::atomToolItem, 0, reinterpret_cast<const guchar*>(it),
                           sizeof(ToolItemDragDropData));

    g_free(it);
}

/**
 * Drag a Toolitem from dialog
 */
void ToolbarCustomizeDialog::toolitemDragBegin(GtkWidget* widget, GdkDragContext* context, ToolItemDragData* data) {
    ToolItemDragCurrentData::setData(TOOL_ITEM_ITEM, -1, data->item);
    if (data->icon) {
        ToolbarDragDropHelper::gdk_context_set_icon_from_image(context, data->icon);
    }
    gtk_widget_hide(data->ebox.get());
}

/**
 * Drag a Toolitem from dialog STOPPED
 */
void ToolbarCustomizeDialog::toolitemDragEnd(GtkWidget* widget, GdkDragContext* context, ToolItemDragData* data) {
    ToolItemDragCurrentData::clearData();
    gtk_widget_show(data->ebox.get());
}

void ToolbarCustomizeDialog::toolitemDragDataGet(GtkWidget* widget, GdkDragContext* context,
                                                 GtkSelectionData* selection_data, guint info, guint time,
                                                 ToolItemDragData* data) {
    g_return_if_fail(data != nullptr);
    g_return_if_fail(data->item != nullptr);

    data->item->setUsed(true);

    ToolItemDragDropData* it = ToolitemDragDrop::ToolItemDragDropData_new(data->item);

    gtk_selection_data_set(selection_data, ToolbarDragDropHelper::atomToolItem, 0, reinterpret_cast<const guchar*>(it),
                           sizeof(ToolItemDragDropData));

    g_free(it);

    data->dlg->rebuildIconview();
}

/**
 * Drag a ColorToolitem from dialog
 */
void ToolbarCustomizeDialog::toolitemColorDragBegin(GtkWidget* widget, GdkDragContext* context,
                                                    ColorToolItemDragData* data) {
    ToolItemDragCurrentData::setDataColor(-1, data->namedColor);

    GdkPixbuf* image = ColorSelectImage::newColorIconPixbuf(data->namedColor->getColor(), 16, true);
    gtk_drag_set_icon_pixbuf(context, image, -2, -2);
    g_object_unref(image);
}

/**
 * Drag a ColorToolitem from dialog STOPPED
 */
void ToolbarCustomizeDialog::toolitemColorDragEnd(GtkWidget* widget, GdkDragContext* context,
                                                  ColorToolItemDragData* data) {
    ToolItemDragCurrentData::clearData();
}

void ToolbarCustomizeDialog::toolitemColorDragDataGet(GtkWidget* widget, GdkDragContext* context,
                                                      GtkSelectionData* selection_data, guint info, guint time,
                                                      ColorToolItemDragData* data) {
    ToolItemDragCurrentData::setDataColor(-1, data->namedColor);

    ToolItemDragDropData* it = ToolitemDragDrop::ToolItemDragDropData_new(nullptr);
    it->type = TOOL_ITEM_COLOR;
    it->namedColor = data->namedColor;

    gtk_selection_data_set(selection_data, ToolbarDragDropHelper::atomToolItem, 0, reinterpret_cast<const guchar*>(it),
                           sizeof(ToolItemDragDropData));

    g_free(it);
}

/**
 * A tool item was dragged to the dialog
 */
void ToolbarCustomizeDialog::dragDataReceived(GtkWidget* widget, GdkDragContext* dragContext, gint x, gint y,
                                              GtkSelectionData* data, guint info, guint time,
                                              ToolbarCustomizeDialog* dlg) {
    if (gtk_selection_data_get_data_type(data) != ToolbarDragDropHelper::atomToolItem) {
        gtk_drag_finish(dragContext, false, false, time);
        return;
    }

    auto* d = reinterpret_cast<ToolItemDragDropData const*>(gtk_selection_data_get_data(data));
    g_return_if_fail(ToolitemDragDrop::checkToolItemDragDropData(d));

    if (d->type == TOOL_ITEM_ITEM) {
        d->item->setUsed(false);
        dlg->rebuildIconview();
    } else if (d->type == TOOL_ITEM_SEPARATOR) {
        /*
         * There is always a seperator shown in the dialog.
         * Hence dragging a separator into the dialog does not
         * require any action.
         */
    } else if (d->type == TOOL_ITEM_SPACER) {
        /*
         * There is always a spacer shown in the dialog.
         * Hence dragging a spacer into the dialog does not
         * require any action.
         */
    } else if (d->type == TOOL_ITEM_COLOR) {
        /*
         * The dialog always contains the full palette of colors.
         * Hence dragging a color toolitem into the dialog does note
         * require any action.
         */
    } else {
        g_warning("ToolbarCustomizeDialog::dragDataReceived unhandled type: %i", d->type);
    }

    gtk_drag_finish(dragContext, true, false, time);
}

/**
 * clear the icon list
 */
void ToolbarCustomizeDialog::freeIconview() {
    GList* children = gtk_container_get_children(GTK_CONTAINER(toolTable));
    for (auto& w: GListView<GtkWidget>(children)) {
        gtk_container_remove(GTK_CONTAINER(toolTable), &w);
    }
    g_list_free(children);
}

/**
 * builds up the icon list
 */
auto ToolbarCustomizeDialog::buildToolDataVector(const std::vector<AbstractToolItem*>& tools)
        -> std::vector<ToolItemDragData> {
    // By reserving, we ensure no reallocation is done, so the pointer `&data` used below is not invalidated
    std::vector<ToolItemDragData> database;
    database.reserve(tools.size());
    for (AbstractToolItem* item: tools) {
        std::string name = item->getToolDisplayName();
        GtkWidget* icon = item->getNewToolIcon(); /* floating */
        xoj_assert(icon);

        GtkBox* box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 3));
        gtk_box_append(box, icon);
        gtk_box_append(box, gtk_label_new(name.c_str()));

        GtkWidget* ebox = gtk_event_box_new();
        gtk_container_add(GTK_CONTAINER(ebox), GTK_WIDGET(box));
        gtk_widget_show_all(GTK_WIDGET(ebox));

        auto& data = database.emplace_back();
        data.dlg = this;
        data.icon = icon;
        data.item = item;
        data.ebox.reset(ebox, xoj::util::adopt);

        // make ebox a drag source
        gtk_drag_source_set(ebox, GDK_BUTTON1_MASK, &ToolbarDragDropHelper::dropTargetEntry, 1, GDK_ACTION_MOVE);
        ToolbarDragDropHelper::dragSourceAddToolbar(ebox);

        g_signal_connect(ebox, "drag-begin", G_CALLBACK(toolitemDragBegin), &data);
        g_signal_connect(ebox, "drag-end", G_CALLBACK(toolitemDragEnd), &data);
        g_signal_connect(ebox, "drag-data-get", G_CALLBACK(toolitemDragDataGet), &data);
    }
    return database;
}

void ToolbarCustomizeDialog::rebuildIconview() {
    freeIconview();
    int i = 0;
    for (auto& data: itemData) {
        if (!data.item->isUsed()) {
            const int x = i % 3;
            const int y = i / 3;
            gtk_grid_attach(toolTable, data.ebox.get(), x, y, 1, 1);

            i++;
        }
    }
}

auto ToolbarCustomizeDialog::buildColorDataVector(const Palette& palette) -> std::vector<ColorToolItemDragData> {
    // By reserving, we ensure no reallocation is done, so the pointer `&data` used below is not invalidated
    std::vector<ColorToolItemDragData> database;
    database.reserve(palette.size());
    for (size_t i = 0; i < palette.size(); i++) {
        // namedColor needs to be a pointer to pass it into a ColorToolItemDragData
        const NamedColor* namedColor = &(palette.getColorAt(i));

        GtkBox* box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 3));
        gtk_box_append(box, ColorSelectImage::newColorIcon(namedColor->getColor(), 16, true));
        gtk_box_append(box, gtk_label_new(namedColor->getName().c_str()));

        GtkWidget* ebox = gtk_event_box_new();
        gtk_container_add(GTK_CONTAINER(ebox), GTK_WIDGET(box));
        gtk_widget_show_all(GTK_WIDGET(ebox));

        // make ebox a drag source
        gtk_drag_source_set(ebox, GDK_BUTTON1_MASK, &ToolbarDragDropHelper::dropTargetEntry, 1, GDK_ACTION_MOVE);
        ToolbarDragDropHelper::dragSourceAddToolbar(ebox);

        auto& data = database.emplace_back();
        data.dlg = this;
        data.namedColor = namedColor;
        data.ebox.reset(ebox, xoj::util::ref);

        g_signal_connect(ebox, "drag-begin", G_CALLBACK(toolitemColorDragBegin), &data);
        g_signal_connect(ebox, "drag-end", G_CALLBACK(toolitemColorDragEnd), &data);
        g_signal_connect(ebox, "drag-data-get", G_CALLBACK(toolitemColorDragDataGet), &data);
    }
    return database;
}

void ToolbarCustomizeDialog::show(GtkWindow* parent) {
    gtk_window_set_transient_for(this->window.get(), parent);

    gtk_window_set_position(this->window.get(), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_widget_show_all(GTK_WIDGET(this->window.get()));
}
