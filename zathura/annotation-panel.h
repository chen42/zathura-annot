/* SPDX-License-Identifier: Zlib */
/* Part of zathura-annot (modified fork of pwmt/zathura) */

#ifndef ANNOTATION_PANEL_H
#define ANNOTATION_PANEL_H

#include <stdbool.h>
#include <gtk/gtk.h>

#include "types.h"

GtkWidget* zathura_annotation_panel_new(zathura_t* zathura);
void zathura_annotation_panel_refresh(GtkWidget* panel);
void zathura_annotation_panel_focus_annotation(GtkWidget* panel, const char* annotation_id);
void zathura_annotation_panel_start_edit(GtkWidget* panel, const char* annotation_id, bool is_new);
void zathura_annotation_panel_set_show_resolved(GtkWidget* panel, bool show_resolved);
bool zathura_annotation_panel_get_show_resolved(GtkWidget* panel);
unsigned int zathura_annotation_panel_get_card_count(GtkWidget* panel);
GtkWidget* zathura_annotation_panel_get_editor_text_view(GtkWidget* panel);
bool zathura_annotation_panel_handle_editor_key(GtkWidget* panel, guint keyval, GdkModifierType state);

#endif // ANNOTATION_PANEL_H
