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
bool zathura_annotation_panel_scroll_to_annotation(GtkWidget* panel, const char* annotation_id, double target_viewport_y);
void zathura_annotation_panel_start_edit(GtkWidget* panel, const char* annotation_id, bool is_new);
unsigned int zathura_annotation_panel_get_card_count(GtkWidget* panel);
GtkWidget* zathura_annotation_panel_get_editor_text_view(GtkWidget* panel);
bool zathura_annotation_panel_handle_editor_key(GtkWidget* panel, guint keyval, GdkModifierType state);
void zathura_annotation_panel_set_all_collapsed(GtkWidget* panel, bool collapsed);
bool zathura_annotation_panel_are_all_collapsed(GtkWidget* panel);
void zathura_annotation_panel_set_annotation_collapsed(GtkWidget* panel, const char* annotation_id, bool collapsed);
bool zathura_annotation_panel_is_annotation_collapsed(GtkWidget* panel, const char* annotation_id);
bool zathura_annotation_panel_scroll(GtkWidget* panel, double dx, double dy);
GtkAdjustment* zathura_annotation_panel_get_vadjustment(GtkWidget* panel);
void zathura_annotation_panel_set_filter_text(GtkWidget* panel, const char* filter_text);
const char* zathura_annotation_panel_get_filter_text(GtkWidget* panel);
GtkWidget* zathura_annotation_panel_get_filter_entry(GtkWidget* panel);
char* zathura_annotation_panel_format_export(GtkWidget* panel, bool csv_format, unsigned int* exported_count);
char* zathura_annotation_panel_get_default_export_path(zathura_t* zathura);
bool zathura_annotation_panel_export(GtkWidget* panel, const char* target_path);

#endif // ANNOTATION_PANEL_H
