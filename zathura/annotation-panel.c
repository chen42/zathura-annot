/* SPDX-License-Identifier: Zlib */
/* Part of zathura-annot (modified fork of pwmt/zathura) */

#include "annotation-panel.h"

#include <girara-gtk/session.h>
#include <girara-gtk/settings.h>
#include <girara/utils.h>
#include <glib/gi18n.h>
#include <math.h>
#include <string.h>

#include "annotations.h"
#include "document.h"
#include "utils.h"
#include "zathura.h"

typedef struct zathura_annotation_panel_state_s {
  zathura_t* zathura;
  GtkWidget* root;
  GtkWidget* count_label;
  GtkWidget* collapse_all_btn;
  GtkWidget* filter_entry;
  GtkWidget* filter_clear_btn;
  GtkWidget* export_btn;
  GtkWidget* scrolled_window;
  GtkWidget* list_box;
  GtkWidget* active_editor;
  GHashTable* collapsed_ids;
  char* filter_text;
  char* pending_scroll_id;
  double pending_scroll_target_y;
  guint pending_scroll_source_id;
  unsigned int pending_scroll_retries;
  unsigned int card_count;
  unsigned int collapsed_count;
  bool updating_filter;
} zathura_annotation_panel_state_t;

static void cancel_pending_scroll(zathura_annotation_panel_state_t* state) {
  if (state == NULL) {
    return;
  }
  if (state->pending_scroll_source_id > 0) {
    g_source_remove(state->pending_scroll_source_id);
    state->pending_scroll_source_id = 0;
  }
  g_clear_pointer(&state->pending_scroll_id, g_free);
  state->pending_scroll_target_y = -1.0;
  state->pending_scroll_retries  = 0;
}

static void panel_state_free(gpointer data) {
  zathura_annotation_panel_state_t* state = data;
  if (state != NULL) {
    cancel_pending_scroll(state);
    if (state->collapsed_ids != NULL) {
      g_hash_table_destroy(state->collapsed_ids);
      state->collapsed_ids = NULL;
    }
    g_free(state->filter_text);
    state->filter_text = NULL;
  }
  g_free(state);
}

static zathura_annotation_panel_state_t* get_panel_state(GtkWidget* panel) {
  if (panel == NULL) {
    return NULL;
  }
  return g_object_get_data(G_OBJECT(panel), "zathura-annotation-panel-state");
}

static char* get_text_view_string(GtkTextView* text_view) {
  if (text_view == NULL) {
    return g_strdup("");
  }
  GtkTextBuffer* buffer = gtk_text_view_get_buffer(text_view);
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void sync_active_editor_draft(zathura_annotation_panel_state_t* state) {
  if (state == NULL || state->zathura == NULL || state->active_editor == NULL ||
      state->zathura->annotations.editing_id == NULL) {
    return;
  }

  char* current_text = get_text_view_string(GTK_TEXT_VIEW(state->active_editor));
  g_free(state->zathura->annotations.draft_comment);
  state->zathura->annotations.draft_comment = current_text;
}

static void cb_editor_buffer_changed(GtkTextBuffer* buffer, gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  if (state == NULL || state->zathura == NULL || state->zathura->annotations.editing_id == NULL) {
    return;
  }

  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  char* text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
  g_free(state->zathura->annotations.draft_comment);
  state->zathura->annotations.draft_comment = text;
}

static void commit_current_editor(zathura_annotation_panel_state_t* state) {
  if (state == NULL || state->zathura == NULL || state->zathura->annotations.editing_id == NULL) {
    return;
  }

  g_autofree char* text = NULL;
  if (state->active_editor != NULL) {
    text = get_text_view_string(GTK_TEXT_VIEW(state->active_editor));
  } else if (state->zathura->annotations.draft_comment != NULL) {
    text = g_strdup(state->zathura->annotations.draft_comment);
  } else {
    text = g_strdup("");
  }

  g_autofree char* target_id = g_strdup(state->zathura->annotations.editing_id);
  zathura_annotations_commit_edit(state->zathura, target_id, text);
  zathura_annotations_mark_dirty(state->zathura);
  state->active_editor = NULL;
  zathura_annotation_panel_refresh(state->root);
  zathura_annotations_queue_draw_all(state->zathura);
}

static void cancel_current_editor(zathura_annotation_panel_state_t* state) {
  if (state == NULL || state->zathura == NULL || state->zathura->annotations.editing_id == NULL) {
    return;
  }

  bool was_new = state->zathura->annotations.draft_is_new;
  zathura_annotations_cancel_edit(state->zathura);
  if (was_new) {
    zathura_annotations_mark_dirty(state->zathura);
  }
  state->active_editor = NULL;
  zathura_annotation_panel_refresh(state->root);
  zathura_annotations_queue_draw_all(state->zathura);
}

bool zathura_annotation_panel_handle_editor_key(GtkWidget* panel, guint keyval, GdkModifierType state_mask) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL || state->zathura == NULL || state->zathura->annotations.editing_id == NULL) {
    return false;
  }

  const GdkModifierType mods = state_mask & gtk_accelerator_get_default_mod_mask();
  if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) && (mods & GDK_CONTROL_MASK)) {
    commit_current_editor(state);
    return true;
  }
  if (keyval == GDK_KEY_Escape) {
    cancel_current_editor(state);
    return true;
  }
  return false;
}

static gboolean cb_editor_key_pressed(GtkEventControllerKey* UNUSED(controller), guint keyval, guint UNUSED(keycode),
                                      GdkModifierType state_mask, gpointer user_data) {
  GtkWidget* panel = GTK_WIDGET(user_data);
  return zathura_annotation_panel_handle_editor_key(panel, keyval, state_mask) ? TRUE : FALSE;
}

static void cb_editor_save_clicked(GtkButton* UNUSED(button), gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  commit_current_editor(state);
}

static void cb_editor_cancel_clicked(GtkButton* UNUSED(button), gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  cancel_current_editor(state);
}

static bool is_annotation_collapsed(zathura_annotation_panel_state_t* state, const char* annotation_id) {
  if (state == NULL || state->collapsed_ids == NULL || annotation_id == NULL) {
    return false;
  }
  return g_hash_table_contains(state->collapsed_ids, annotation_id);
}

static void cb_card_clicked(GtkGestureClick* UNUSED(gesture), gint n_press, gdouble x, gdouble y,
                            gpointer user_data) {
  GtkWidget* card                         = GTK_WIDGET(user_data);
  zathura_annotation_panel_state_t* state = g_object_get_data(G_OBJECT(card), "zathura-panel-state");
  const char* id                          = g_object_get_data(G_OBJECT(card), "zathura-annotation-id");
  if (state == NULL || state->zathura == NULL || id == NULL) {
    return;
  }

  g_autofree char* id_copy = g_strdup(id);

  if (g_strcmp0(state->zathura->annotations.active_id, id_copy) != 0) {
    zathura_annotations_set_active(state->zathura, id_copy);
    for (GtkWidget* child = gtk_widget_get_first_child(state->list_box); child != NULL;
         child            = gtk_widget_get_next_sibling(child)) {
      const char* child_id = g_object_get_data(G_OBJECT(child), "zathura-annotation-id");
      if (g_strcmp0(child_id, id_copy) == 0) {
        gtk_widget_add_css_class(child, "active");
      } else {
        gtk_widget_remove_css_class(child, "active");
      }
    }
  }

  if (n_press == 2 && g_strcmp0(state->zathura->annotations.editing_id, id_copy) != 0) {
    GtkWidget* picked = gtk_widget_pick(card, x, y, GTK_PICK_DEFAULT);
    if (picked != NULL &&
        (GTK_IS_BUTTON(picked) || gtk_widget_get_ancestor(picked, GTK_TYPE_BUTTON) != NULL ||
         GTK_IS_TEXT_VIEW(picked) || gtk_widget_get_ancestor(picked, GTK_TYPE_TEXT_VIEW) != NULL)) {
      return;
    }

    const bool collapsed = is_annotation_collapsed(state, id_copy);
    zathura_annotation_panel_set_annotation_collapsed(state->root, id_copy, !collapsed);
  }
}

static void cb_card_edit_clicked(GtkButton* button, gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  const char* id                          = g_object_get_data(G_OBJECT(button), "zathura-annotation-id");
  if (state == NULL || state->zathura == NULL || id == NULL) {
    return;
  }

  sync_active_editor_draft(state);
  zathura_annotations_set_active(state->zathura, id);
  zathura_annotation_panel_start_edit(state->root, id, false);
}

static void cb_card_delete_clicked(GtkButton* button, gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  const char* id                          = g_object_get_data(G_OBJECT(button), "zathura-annotation-id");
  if (state == NULL || state->zathura == NULL || id == NULL) {
    return;
  }

  sync_active_editor_draft(state);
  g_autofree char* id_copy = g_strdup(id);
  zathura_annotations_remove(state->zathura, id_copy);
  zathura_annotations_mark_dirty(state->zathura);
  zathura_annotation_panel_refresh(state->root);
  zathura_annotations_queue_draw_all(state->zathura);
}

static void cb_card_jump_clicked(GtkButton* button, gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  const char* id                          = g_object_get_data(G_OBJECT(button), "zathura-annotation-id");
  if (state == NULL || state->zathura == NULL || id == NULL) {
    return;
  }

  sync_active_editor_draft(state);
  zathura_annotation_t* ann = zathura_annotations_find(state->zathura, id);
  if (ann == NULL) {
    return;
  }

  zathura_annotations_set_active(state->zathura, ann->id);
  zathura_annotations_reveal(state->zathura, ann);
  zathura_annotation_panel_refresh(state->root);
}

static void cb_close_panel_clicked(GtkButton* UNUSED(button), gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  if (state == NULL || state->root == NULL) {
    return;
  }

  gtk_widget_set_visible(state->root, FALSE);
  if (state->zathura != NULL) {
    zathura_annotations_queue_draw_all(state->zathura);
    if (state->zathura->ui.session != NULL && state->zathura->ui.session->gtk.view != NULL) {
      gtk_widget_grab_focus(state->zathura->ui.session->gtk.view);
    }
  }
}

static void cb_panel_visible_notify(GObject* UNUSED(object), GParamSpec* UNUSED(pspec), gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  if (state != NULL && state->zathura != NULL && state->zathura->ui.document_widget != NULL) {
    gtk_widget_queue_allocate(GTK_WIDGET(state->zathura->ui.document_widget));
  }
}

static char* extract_first_line(const char* text) {
  if (text == NULL) {
    return g_strdup("");
  }
  while (*text == '\r' || *text == '\n') {
    ++text;
  }
  const char* end = text;
  while (*end != '\0' && *end != '\r' && *end != '\n') {
    ++end;
  }
  return g_strndup(text, (gsize)(end - text));
}

static void cb_card_collapse_clicked(GtkButton* button, gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  const char* id                          = g_object_get_data(G_OBJECT(button), "zathura-annotation-id");
  if (state == NULL || id == NULL) {
    return;
  }

  const bool collapsed = is_annotation_collapsed(state, id);
  zathura_annotation_panel_set_annotation_collapsed(state->root, id, !collapsed);
}

static void cb_collapse_all_clicked(GtkButton* UNUSED(button), gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  if (state == NULL || state->root == NULL) {
    return;
  }

  const bool collapse_all = !zathura_annotation_panel_are_all_collapsed(state->root);
  zathura_annotation_panel_set_all_collapsed(state->root, collapse_all);
}

static gboolean cb_panel_scroll(GtkEventControllerScroll* controller, gdouble dx, gdouble dy, gpointer user_data) {
  GtkWidget* panel = GTK_WIDGET(user_data);
  double wheel_dy  = dy;
  if (controller != NULL && gtk_event_controller_scroll_get_unit(controller) == GDK_SCROLL_UNIT_SURFACE) {
    wheel_dy = dy / 56.0;
  }
  return zathura_annotation_panel_scroll(panel, dx, wheel_dy) ? TRUE : FALSE;
}

static bool annotation_matches_filter(const zathura_annotation_t* ann, const char* filter_text) {
  if (filter_text == NULL || filter_text[0] == '\0') {
    return true;
  }
  if (ann == NULL || ann->comment == NULL || ann->comment[0] == '\0') {
    return false;
  }

  g_autofree char* needle   = g_utf8_casefold(filter_text, -1);
  g_autofree char* haystack = g_utf8_casefold(ann->comment, -1);
  if (needle == NULL || haystack == NULL) {
    return false;
  }
  return strstr(haystack, needle) != NULL;
}

static void cb_filter_entry_changed(GtkEditable* editable, gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  if (state == NULL || state->updating_filter) {
    return;
  }

  const char* text = gtk_editable_get_text(editable);
  g_free(state->filter_text);
  state->filter_text = g_strdup(text != NULL ? text : "");
  zathura_annotation_panel_refresh(state->root);
}

static void cb_filter_clear_clicked(GtkButton* UNUSED(button), gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  if (state == NULL || state->root == NULL) {
    return;
  }
  zathura_annotation_panel_set_filter_text(state->root, "");
}

static void cb_export_clicked(GtkButton* UNUSED(button), gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  if (state == NULL || state->root == NULL) {
    return;
  }
  zathura_annotation_panel_export(state->root, NULL);
}

static gboolean cb_filter_key_pressed(GtkEventControllerKey* UNUSED(controller), guint keyval, guint UNUSED(keycode),
                                      GdkModifierType UNUSED(mods), gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  if (state == NULL || state->root == NULL) {
    return FALSE;
  }

  if (keyval == GDK_KEY_Escape) {
    if (state->filter_text != NULL && state->filter_text[0] != '\0') {
      zathura_annotation_panel_set_filter_text(state->root, "");
    }
    if (state->zathura != NULL && state->zathura->ui.session != NULL && state->zathura->ui.session->gtk.view != NULL) {
      gtk_widget_grab_focus(state->zathura->ui.session->gtk.view);
    }
    return TRUE;
  }

  if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
    if (state->zathura != NULL && state->zathura->ui.session != NULL && state->zathura->ui.session->gtk.view != NULL) {
      gtk_widget_grab_focus(state->zathura->ui.session->gtk.view);
    }
    return TRUE;
  }

  return FALSE;
}

static GtkWidget* create_card_widget(zathura_annotation_panel_state_t* state, zathura_annotation_t* ann) {
  const bool is_editing   = (g_strcmp0(ann->id, state->zathura->annotations.editing_id) == 0);
  const bool is_collapsed = !is_editing && is_annotation_collapsed(state, ann->id);

  GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_add_css_class(card, "annotation-card");
  gtk_widget_add_css_class(card, ann->color == ZATHURA_ANNOTATION_COLOR_RED ? "red" : "yellow");
  if (g_strcmp0(ann->id, state->zathura->annotations.active_id) == 0) {
    gtk_widget_add_css_class(card, "active");
  }
  if (is_collapsed) {
    gtk_widget_add_css_class(card, "collapsed");
  }
  g_object_set_data(G_OBJECT(card), "zathura-panel-state", state);
  g_object_set_data_full(G_OBJECT(card), "zathura-annotation-id", g_strdup(ann->id), g_free);

  GtkGesture* card_click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(card_click), GDK_BUTTON_PRIMARY);
  g_signal_connect(card_click, "pressed", G_CALLBACK(cb_card_clicked), card);
  gtk_widget_add_controller(card, GTK_EVENT_CONTROLLER(card_click));

  if (is_collapsed) {
    GtkWidget* top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    GtkWidget* collapse_btn = gtk_button_new_with_label("▸");
    gtk_widget_add_css_class(collapse_btn, "annotation-card-btn");
    gtk_widget_add_css_class(collapse_btn, "annotation-collapse-btn");
    gtk_widget_add_css_class(collapse_btn, "flat");
    gtk_widget_set_valign(collapse_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(collapse_btn, _("Expand comment"));
    g_object_set_data_full(G_OBJECT(collapse_btn), "zathura-annotation-id", g_strdup(ann->id), g_free);
    g_signal_connect(collapse_btn, "clicked", G_CALLBACK(cb_card_collapse_clicked), state);
    gtk_box_append(GTK_BOX(top_row), collapse_btn);

    g_autofree char* first_line = extract_first_line(ann->comment);
    GtkWidget* comment_label    = gtk_label_new(first_line);
    gtk_widget_add_css_class(comment_label, "annotation-comment-body");
    gtk_widget_add_css_class(comment_label, "collapsed");
    gtk_label_set_wrap(GTK_LABEL(comment_label), FALSE);
    gtk_label_set_single_line_mode(GTK_LABEL(comment_label), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(comment_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(comment_label), 0.0f);
    gtk_widget_set_hexpand(comment_label, TRUE);
    gtk_widget_set_valign(comment_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(top_row), comment_label);

    gtk_box_append(GTK_BOX(card), top_row);
    return card;
  }

  /* 1. Top row: collapse button, quoted snippet, delete (×) button on the top right */
  GtkWidget* top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

  GtkWidget* collapse_btn = gtk_button_new_with_label("▾");
  gtk_widget_add_css_class(collapse_btn, "annotation-card-btn");
  gtk_widget_add_css_class(collapse_btn, "annotation-collapse-btn");
  gtk_widget_add_css_class(collapse_btn, "flat");
  gtk_widget_set_valign(collapse_btn, GTK_ALIGN_START);
  gtk_widget_set_tooltip_text(collapse_btn, _("Roll up comment"));
  g_object_set_data_full(G_OBJECT(collapse_btn), "zathura-annotation-id", g_strdup(ann->id), g_free);
  g_signal_connect(collapse_btn, "clicked", G_CALLBACK(cb_card_collapse_clicked), state);
  gtk_box_append(GTK_BOX(top_row), collapse_btn);

  if (ann->quote != NULL && ann->quote[0] != '\0') {
    GtkWidget* quote_label = gtk_label_new(ann->quote);
    gtk_widget_add_css_class(quote_label, "annotation-quote");
    gtk_label_set_wrap(GTK_LABEL(quote_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(quote_label), 0.0f);
    gtk_label_set_lines(GTK_LABEL(quote_label), 3);
    gtk_label_set_ellipsize(GTK_LABEL(quote_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(quote_label, TRUE);
    gtk_box_append(GTK_BOX(top_row), quote_label);
  } else {
    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(top_row), spacer);
  }

  GtkWidget* delete_btn = gtk_button_new_with_label("×");
  gtk_widget_add_css_class(delete_btn, "annotation-card-btn");
  gtk_widget_add_css_class(delete_btn, "annotation-close-btn");
  gtk_widget_add_css_class(delete_btn, "flat");
  gtk_widget_set_valign(delete_btn, GTK_ALIGN_START);
  g_object_set_data_full(G_OBJECT(delete_btn), "zathura-annotation-id", g_strdup(ann->id), g_free);
  g_signal_connect(delete_btn, "clicked", G_CALLBACK(cb_card_delete_clicked), state);
  gtk_box_append(GTK_BOX(top_row), delete_btn);

  gtk_box_append(GTK_BOX(card), top_row);

  /* 2. Comment body or inline editor */
  if (is_editing) {
    GtkWidget* editor = gtk_text_view_new();
    gtk_widget_add_css_class(editor, "annotation-editor");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(editor), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_size_request(editor, -1, 64);

    const char* initial_text = (state->zathura->annotations.draft_comment != NULL)
                                   ? state->zathura->annotations.draft_comment
                                   : (ann->comment != NULL ? ann->comment : "");
    GtkTextBuffer* buffer    = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
    gtk_text_buffer_set_text(buffer, initial_text, -1);
    g_signal_connect(buffer, "changed", G_CALLBACK(cb_editor_buffer_changed), state);

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(cb_editor_key_pressed), state->root);
    gtk_widget_add_controller(editor, key_ctrl);

    state->active_editor = editor;
    gtk_box_append(GTK_BOX(card), editor);

    GtkWidget* action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(action_row, GTK_ALIGN_END);

    GtkWidget* cancel_btn = gtk_button_new_with_label(_("Cancel"));
    gtk_widget_add_css_class(cancel_btn, "annotation-card-btn");
    gtk_widget_add_css_class(cancel_btn, "flat");
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(cb_editor_cancel_clicked), state);
    gtk_box_append(GTK_BOX(action_row), cancel_btn);

    GtkWidget* save_btn = gtk_button_new_with_label(_("Save"));
    gtk_widget_add_css_class(save_btn, "annotation-card-btn");
    gtk_widget_add_css_class(save_btn, "flat");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(cb_editor_save_clicked), state);
    gtk_box_append(GTK_BOX(action_row), save_btn);

    gtk_box_append(GTK_BOX(card), action_row);
  } else {
    GtkWidget* comment_label = gtk_label_new(ann->comment != NULL ? ann->comment : "");
    gtk_widget_add_css_class(comment_label, "annotation-comment-body");
    gtk_label_set_wrap(GTK_LABEL(comment_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(comment_label), 0.0f);
    gtk_box_append(GTK_BOX(card), comment_label);
  }

  /* 3. Bottom action row: color dot, page button, compact low-contrast action buttons */
  GtkWidget* bottom_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
  gtk_widget_add_css_class(bottom_row, "annotation-card-actions");

  GtkWidget* dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_size_request(dot, 6, 6);
  gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class(dot, "annotation-color-dot");
  gtk_widget_add_css_class(dot, ann->color == ZATHURA_ANNOTATION_COLOR_RED ? "red" : "yellow");
  gtk_box_append(GTK_BOX(bottom_row), dot);

  g_autofree char* page_text = g_strdup_printf(_("Page %u"), ann->page + 1);
  GtkWidget* page_btn        = gtk_button_new_with_label(page_text);
  gtk_widget_add_css_class(page_btn, "annotation-page-badge");
  gtk_widget_add_css_class(page_btn, "annotation-card-btn");
  gtk_widget_add_css_class(page_btn, "flat");
  gtk_widget_set_hexpand(page_btn, TRUE);
  gtk_widget_set_halign(page_btn, GTK_ALIGN_START);
  g_object_set_data_full(G_OBJECT(page_btn), "zathura-annotation-id", g_strdup(ann->id), g_free);
  g_signal_connect(page_btn, "clicked", G_CALLBACK(cb_card_jump_clicked), state);
  gtk_box_append(GTK_BOX(bottom_row), page_btn);

  GtkWidget* edit_btn = gtk_button_new_with_label(_("Edit"));
  gtk_widget_add_css_class(edit_btn, "annotation-card-btn");
  gtk_widget_add_css_class(edit_btn, "flat");
  g_object_set_data_full(G_OBJECT(edit_btn), "zathura-annotation-id", g_strdup(ann->id), g_free);
  g_signal_connect(edit_btn, "clicked", G_CALLBACK(cb_card_edit_clicked), state);
  gtk_box_append(GTK_BOX(bottom_row), edit_btn);

  gtk_box_append(GTK_BOX(card), bottom_row);

  return card;
}

GtkWidget* zathura_annotation_panel_new(zathura_t* zathura) {
  GtkWidget* panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(panel, "annotation-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_END);
  gtk_widget_set_valign(panel, GTK_ALIGN_FILL);

  int panel_width = 340;
  if (zathura != NULL && zathura->ui.session != NULL) {
    girara_setting_get(zathura->ui.session, "annotation-panel-width", &panel_width);
    if (panel_width < 220) {
      panel_width = 340;
    }
  }
  gtk_widget_set_size_request(panel, panel_width, -1);

  zathura_annotation_panel_state_t* state = g_malloc0(sizeof(zathura_annotation_panel_state_t));
  state->zathura                          = zathura;
  state->root                             = panel;
  state->collapsed_ids                    = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  g_object_set_data_full(G_OBJECT(panel), "zathura-annotation-panel-state", state, panel_state_free);
  g_signal_connect(panel, "notify::visible", G_CALLBACK(cb_panel_visible_notify), state);

  GtkEventController* scroll_ctrl = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  gtk_event_controller_set_propagation_phase(scroll_ctrl, GTK_PHASE_CAPTURE);
  g_signal_connect(scroll_ctrl, "scroll", G_CALLBACK(cb_panel_scroll), panel);
  gtk_widget_add_controller(panel, scroll_ctrl);

  /* Header bar */
  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(header, "annotation-panel-header");

  GtkWidget* title = gtk_label_new(_("Comments"));
  gtk_widget_add_css_class(title, "annotation-panel-title");
  gtk_widget_set_hexpand(title, TRUE);
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_box_append(GTK_BOX(header), title);

  state->count_label = gtk_label_new("0");
  gtk_widget_add_css_class(state->count_label, "annotation-count-badge");
  gtk_box_append(GTK_BOX(header), state->count_label);

  state->collapse_all_btn = gtk_button_new_with_label(_("Roll up"));
  gtk_widget_add_css_class(state->collapse_all_btn, "annotation-card-btn");
  gtk_widget_add_css_class(state->collapse_all_btn, "annotation-collapse-all-btn");
  gtk_widget_add_css_class(state->collapse_all_btn, "flat");
  gtk_widget_set_tooltip_text(state->collapse_all_btn, _("Roll up all comments"));
  g_signal_connect(state->collapse_all_btn, "clicked", G_CALLBACK(cb_collapse_all_clicked), state);
  gtk_box_append(GTK_BOX(header), state->collapse_all_btn);

  GtkWidget* close_btn = gtk_button_new_with_label("×");
  gtk_widget_add_css_class(close_btn, "annotation-card-btn");
  gtk_widget_add_css_class(close_btn, "annotation-close-btn");
  gtk_widget_add_css_class(close_btn, "flat");
  g_signal_connect(close_btn, "clicked", G_CALLBACK(cb_close_panel_clicked), state);
  gtk_box_append(GTK_BOX(header), close_btn);

  gtk_box_append(GTK_BOX(panel), header);

  /* Filter bar */
  GtkWidget* filter_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(filter_bar, "annotation-filter-bar");

  state->filter_entry = gtk_entry_new();
  gtk_widget_add_css_class(state->filter_entry, "annotation-filter-entry");
  gtk_widget_set_hexpand(state->filter_entry, TRUE);
  gtk_entry_set_placeholder_text(GTK_ENTRY(state->filter_entry), _("Filter comments..."));
  g_signal_connect(state->filter_entry, "changed", G_CALLBACK(cb_filter_entry_changed), state);

  GtkEventController* filter_key_ctrl = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(filter_key_ctrl, GTK_PHASE_CAPTURE);
  g_signal_connect(filter_key_ctrl, "key-pressed", G_CALLBACK(cb_filter_key_pressed), state);
  gtk_widget_add_controller(state->filter_entry, filter_key_ctrl);
  gtk_box_append(GTK_BOX(filter_bar), state->filter_entry);

  state->filter_clear_btn = gtk_button_new_with_label(_("Clear"));
  gtk_widget_add_css_class(state->filter_clear_btn, "annotation-card-btn");
  gtk_widget_add_css_class(state->filter_clear_btn, "annotation-filter-clear-btn");
  gtk_widget_add_css_class(state->filter_clear_btn, "flat");
  gtk_widget_set_tooltip_text(state->filter_clear_btn, _("Clear comment filter"));
  gtk_widget_set_sensitive(state->filter_clear_btn, FALSE);
  g_signal_connect(state->filter_clear_btn, "clicked", G_CALLBACK(cb_filter_clear_clicked), state);
  gtk_box_append(GTK_BOX(filter_bar), state->filter_clear_btn);

  state->export_btn = gtk_button_new_with_label(_("Export"));
  gtk_widget_add_css_class(state->export_btn, "annotation-card-btn");
  gtk_widget_add_css_class(state->export_btn, "annotation-export-btn");
  gtk_widget_add_css_class(state->export_btn, "flat");
  gtk_widget_set_tooltip_text(state->export_btn, _("Export visible comments to TSV file and clipboard"));
  gtk_widget_set_sensitive(state->export_btn, FALSE);
  g_signal_connect(state->export_btn, "clicked", G_CALLBACK(cb_export_clicked), state);
  gtk_box_append(GTK_BOX(filter_bar), state->export_btn);

  gtk_box_append(GTK_BOX(panel), filter_bar);

  /* Scrolled card list */
  state->scrolled_window = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(state->scrolled_window, TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(state->scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  state->list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(state->list_box, "annotation-panel-list");
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(state->scrolled_window), state->list_box);

  gtk_box_append(GTK_BOX(panel), state->scrolled_window);

  zathura_annotation_panel_refresh(panel);
  return panel;
}

void zathura_annotation_panel_refresh(GtkWidget* panel) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL) {
    return;
  }

  sync_active_editor_draft(state);
  state->active_editor   = NULL;
  state->card_count      = 0;
  state->collapsed_count = 0;

  /* Clear existing children in list_box */
  GtkWidget* child = gtk_widget_get_first_child(state->list_box);
  while (child != NULL) {
    GtkWidget* next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(state->list_box), child);
    child = next;
  }

  if (state->zathura != NULL && state->zathura->annotations.list != NULL) {
    for (size_t idx = 0; idx != girara_list_size(state->zathura->annotations.list); ++idx) {
      zathura_annotation_t* ann = girara_list_nth(state->zathura->annotations.list, idx);
      if (ann == NULL) {
        continue;
      }

      const bool is_editing = (g_strcmp0(ann->id, state->zathura->annotations.editing_id) == 0);
      const bool has_comm   = zathura_annotation_has_comment(ann);
      if (!is_editing && (!has_comm || !annotation_matches_filter(ann, state->filter_text))) {
        continue;
      }

      GtkWidget* card = create_card_widget(state, ann);
      gtk_box_append(GTK_BOX(state->list_box), card);
      ++state->card_count;
      if (!is_editing && is_annotation_collapsed(state, ann->id)) {
        ++state->collapsed_count;
      }
    }
  }

  g_autofree char* count_str = g_strdup_printf("%u", state->card_count);
  gtk_label_set_text(GTK_LABEL(state->count_label), count_str);

  if (state->collapse_all_btn != NULL) {
    const bool all_collapsed = (state->card_count > 0 && state->collapsed_count == state->card_count);
    gtk_button_set_label(GTK_BUTTON(state->collapse_all_btn), all_collapsed ? _("Expand all") : _("Roll up"));
    gtk_widget_set_tooltip_text(state->collapse_all_btn,
                                all_collapsed ? _("Expand all comments") : _("Roll up all comments"));
    gtk_widget_set_sensitive(state->collapse_all_btn, state->card_count > 0);
  }

  const bool has_filter = (state->filter_text != NULL && state->filter_text[0] != '\0');
  if (state->filter_clear_btn != NULL) {
    gtk_widget_set_sensitive(state->filter_clear_btn, has_filter);
  }
  if (state->export_btn != NULL) {
    gtk_widget_set_sensitive(state->export_btn, state->card_count > 0);
  }

  if (state->card_count == 0) {
    GtkWidget* empty_label = gtk_label_new(
        has_filter ? _("No matching comments.") : _("No comments yet. Select text and right-click to add a comment."));
    gtk_widget_add_css_class(empty_label, "annotation-empty-state");
    gtk_label_set_wrap(GTK_LABEL(empty_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(empty_label), 0.0f);
    gtk_box_append(GTK_BOX(state->list_box), empty_label);
  }
}

static GtkWidget* find_card_by_annotation_id(zathura_annotation_panel_state_t* state, const char* annotation_id) {
  if (state == NULL || state->list_box == NULL || annotation_id == NULL) {
    return NULL;
  }
  for (GtkWidget* child = gtk_widget_get_first_child(state->list_box); child != NULL;
       child            = gtk_widget_get_next_sibling(child)) {
    const char* card_id = g_object_get_data(G_OBJECT(child), "zathura-annotation-id");
    if (g_strcmp0(card_id, annotation_id) == 0) {
      return child;
    }
  }
  return NULL;
}

static void update_active_card_classes(zathura_annotation_panel_state_t* state, const char* active_id) {
  if (state == NULL || state->list_box == NULL) {
    return;
  }
  for (GtkWidget* child = gtk_widget_get_first_child(state->list_box); child != NULL;
       child            = gtk_widget_get_next_sibling(child)) {
    const char* card_id = g_object_get_data(G_OBJECT(child), "zathura-annotation-id");
    if (card_id == NULL) {
      continue;
    }
    if (active_id != NULL && g_strcmp0(card_id, active_id) == 0) {
      gtk_widget_add_css_class(child, "active");
    } else {
      gtk_widget_remove_css_class(child, "active");
    }
  }
}

static bool measure_card_in_list_box(zathura_annotation_panel_state_t* state, GtkWidget* target_card,
                                     double* out_card_y, double* out_card_h, double* out_list_h) {
  if (state == NULL || state->list_box == NULL || target_card == NULL) {
    return false;
  }

  graphene_rect_t card_bounds;
  if (gtk_widget_get_height(target_card) > 0 &&
      gtk_widget_compute_bounds(target_card, state->list_box, &card_bounds)) {
    *out_card_y          = (double)card_bounds.origin.y;
    *out_card_h          = (double)card_bounds.size.height;
    GtkWidget* last_card = gtk_widget_get_last_child(state->list_box);
    graphene_rect_t last_bounds;
    if (last_card != NULL && gtk_widget_get_height(last_card) > 0 &&
        gtk_widget_compute_bounds(last_card, state->list_box, &last_bounds)) {
      *out_list_h = (double)(last_bounds.origin.y + last_bounds.size.height) + 5.0;
    } else {
      *out_list_h = (double)gtk_widget_get_height(state->list_box);
    }
    return true;
  }

  int for_width = gtk_widget_get_width(state->list_box);
  if (for_width <= 0) {
    int req_w = -1, req_h = -1;
    gtk_widget_get_size_request(state->root, &req_w, &req_h);
    for_width = (req_w > 32) ? (req_w - 24) : 316;
  }

  const double spacing = (double)gtk_box_get_spacing(GTK_BOX(state->list_box));
  double y_cursor      = 0.0;
  bool found           = false;

  for (GtkWidget* child = gtk_widget_get_first_child(state->list_box); child != NULL;
       child            = gtk_widget_get_next_sibling(child)) {
    if (!gtk_widget_get_visible(child)) {
      continue;
    }
    int min_h = 0, nat_h = 0;
    gtk_widget_measure(child, GTK_ORIENTATION_VERTICAL, for_width, &min_h, &nat_h, NULL, NULL);
    const double ch = (double)MAX(min_h, nat_h);
    if (child == target_card) {
      *out_card_y = y_cursor;
      *out_card_h = ch;
      found       = true;
    }
    y_cursor += ch;
    if (gtk_widget_get_next_sibling(child) != NULL) {
      y_cursor += spacing;
    }
  }

  *out_list_h = y_cursor;
  return found;
}

static double compute_annotation_viewport_y(zathura_annotation_panel_state_t* state,
                                            const zathura_annotation_t* ann) {
  if (state == NULL || ann == NULL || state->zathura == NULL || state->zathura->document == NULL ||
      state->scrolled_window == NULL) {
    return -1.0;
  }

  zathura_page_t* page = zathura_document_get_page(state->zathura->document, ann->page);
  if (page == NULL) {
    return -1.0;
  }

  GtkWidget* page_widget = zathura_page_get_widget(state->zathura, page);
  if (page_widget == NULL) {
    return -1.0;
  }

  zathura_rectangle_t sr  = recalc_rectangle(page, ann->bounds);
  graphene_point_t src_pt = GRAPHENE_POINT_INIT(0.0f, (float)sr.y1);
  graphene_point_t dst_pt = GRAPHENE_POINT_INIT(0.0f, 0.0f);

  if (gtk_widget_get_height(state->scrolled_window) > 0 &&
      gtk_widget_compute_point(page_widget, state->scrolled_window, &src_pt, &dst_pt)) {
    return (double)dst_pt.y;
  }

  GtkWidget* ref_widget = state->zathura->ui.content_overlay != NULL ? state->zathura->ui.content_overlay
                                                                     : state->zathura->ui.view;
  if (ref_widget != NULL && gtk_widget_compute_point(page_widget, ref_widget, &src_pt, &dst_pt)) {
    int top_bars_h = 0;
    for (GtkWidget* child = gtk_widget_get_first_child(state->root);
         child != NULL && child != state->scrolled_window;
         child = gtk_widget_get_next_sibling(child)) {
      int child_h = gtk_widget_get_height(child);
      if (child_h <= 0) {
        int min_h = 0, nat_h = 0;
        gtk_widget_measure(child, GTK_ORIENTATION_VERTICAL, -1, &min_h, &nat_h, NULL, NULL);
        child_h = nat_h;
      }
      top_bars_h += child_h;
    }
    return (double)dst_pt.y - (double)top_bars_h;
  }

  return -1.0;
}

static bool apply_scroll_to_annotation(zathura_annotation_panel_state_t* state, const char* annotation_id,
                                       double explicit_target_y, double* out_desired_scroll) {
  if (state == NULL || state->scrolled_window == NULL || state->list_box == NULL || annotation_id == NULL) {
    return false;
  }

  GtkWidget* target_card = find_card_by_annotation_id(state, annotation_id);
  if (target_card == NULL) {
    return false;
  }

  GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->scrolled_window));
  if (vadj == NULL) {
    return false;
  }

  double card_y = 0.0;
  double card_h = 0.0;
  double list_h = 0.0;
  if (!measure_card_in_list_box(state, target_card, &card_y, &card_h, &list_h)) {
    return false;
  }

  double page_size = gtk_adjustment_get_page_size(vadj);
  if (page_size <= 1.0) {
    int sw_h = gtk_widget_get_height(state->scrolled_window);
    if (sw_h > 0) {
      page_size = (double)sw_h;
    }
  }

  double target_y = explicit_target_y;
  if (target_y < 0.0 && state->zathura != NULL) {
    zathura_annotation_t* ann = zathura_annotations_find(state->zathura, annotation_id);
    target_y                  = compute_annotation_viewport_y(state, ann);
  }
  if (target_y < 0.0) {
    target_y = 8.0;
  }

  double desired_viewport_y = target_y;
  if (page_size > 1.0) {
    desired_viewport_y = CLAMP(target_y, 8.0, MAX(8.0, page_size - card_h - 8.0));
  } else {
    desired_viewport_y = MAX(0.0, target_y);
  }

  const double desired_scroll = MAX(0.0, card_y - desired_viewport_y);
  if (out_desired_scroll != NULL) {
    *out_desired_scroll = desired_scroll;
  }

  if (page_size > 1.0) {
    const double needed_total_h = desired_scroll + page_size;
    const int extra_bottom      = (needed_total_h > list_h) ? (int)ceil(needed_total_h - list_h) : 0;
    if (gtk_widget_get_margin_bottom(state->list_box) != extra_bottom) {
      gtk_widget_set_margin_bottom(state->list_box, extra_bottom);
    }
    const double min_upper = MAX(list_h + (double)extra_bottom, gtk_adjustment_get_upper(vadj));
    if (gtk_adjustment_get_upper(vadj) < min_upper) {
      gtk_adjustment_set_upper(vadj, min_upper);
    }
  } else {
    const double min_upper = MAX(list_h, desired_scroll + page_size);
    if (gtk_adjustment_get_upper(vadj) < min_upper) {
      gtk_adjustment_set_upper(vadj, min_upper);
    }
  }

  const double lower      = gtk_adjustment_get_lower(vadj);
  const double upper      = gtk_adjustment_get_upper(vadj);
  const double max_scroll = MAX(lower, upper - page_size);
  gtk_adjustment_set_value(vadj, CLAMP(desired_scroll, lower, max_scroll));
  return true;
}

static gboolean cb_deferred_scroll_to_annotation(gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  if (state == NULL || state->pending_scroll_id == NULL) {
    if (state != NULL) {
      state->pending_scroll_source_id = 0;
    }
    return G_SOURCE_REMOVE;
  }

  double desired_scroll = 0.0;
  apply_scroll_to_annotation(state, state->pending_scroll_id, state->pending_scroll_target_y, &desired_scroll);

  GtkWidget* target_card = find_card_by_annotation_id(state, state->pending_scroll_id);
  GtkAdjustment* vadj    = state->scrolled_window != NULL
                               ? gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->scrolled_window))
                               : NULL;
  const bool allocated   = (target_card != NULL && gtk_widget_get_height(target_card) > 0);
  const bool settled     = (vadj != NULL && fabs(gtk_adjustment_get_value(vadj) - desired_scroll) < 1.0);

  if ((!allocated || !settled) && gtk_widget_get_mapped(state->root) && state->pending_scroll_retries > 0) {
    --state->pending_scroll_retries;
    return G_SOURCE_CONTINUE;
  }

  state->pending_scroll_source_id = 0;
  g_clear_pointer(&state->pending_scroll_id, g_free);
  state->pending_scroll_target_y = -1.0;
  state->pending_scroll_retries  = 0;
  return G_SOURCE_REMOVE;
}

bool zathura_annotation_panel_scroll_to_annotation(GtkWidget* panel, const char* annotation_id,
                                                   double target_viewport_y) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL || annotation_id == NULL) {
    return false;
  }

  cancel_pending_scroll(state);
  if (!apply_scroll_to_annotation(state, annotation_id, target_viewport_y, NULL)) {
    return false;
  }

  state->pending_scroll_id        = g_strdup(annotation_id);
  state->pending_scroll_target_y  = target_viewport_y;
  state->pending_scroll_retries   = 6;
  state->pending_scroll_source_id = g_timeout_add(16, cb_deferred_scroll_to_annotation, state);
  return true;
}

void zathura_annotation_panel_focus_annotation(GtkWidget* panel, const char* annotation_id) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL) {
    return;
  }
  if (annotation_id == NULL) {
    update_active_card_classes(state, NULL);
    return;
  }

  bool needs_refresh        = false;
  zathura_annotation_t* ann = zathura_annotations_find(state->zathura, annotation_id);
  if (ann != NULL && !annotation_matches_filter(ann, state->filter_text)) {
    g_free(state->filter_text);
    state->filter_text = g_strdup("");
    if (state->filter_entry != NULL) {
      state->updating_filter = true;
      gtk_editable_set_text(GTK_EDITABLE(state->filter_entry), "");
      state->updating_filter = false;
    }
    needs_refresh = true;
  }
  if (needs_refresh || find_card_by_annotation_id(state, annotation_id) == NULL) {
    zathura_annotation_panel_refresh(panel);
  } else {
    update_active_card_classes(state, annotation_id);
  }

  zathura_annotation_panel_scroll_to_annotation(panel, annotation_id, -1.0);
}

void zathura_annotation_panel_start_edit(GtkWidget* panel, const char* annotation_id, bool is_new) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL || state->zathura == NULL || annotation_id == NULL) {
    return;
  }

  if (state->collapsed_ids != NULL) {
    g_hash_table_remove(state->collapsed_ids, annotation_id);
  }
  sync_active_editor_draft(state);
  zathura_annotations_set_active(state->zathura, annotation_id);
  zathura_annotations_start_edit(state->zathura, annotation_id, is_new);
  zathura_annotation_panel_refresh(panel);
  zathura_annotation_panel_scroll_to_annotation(panel, annotation_id, -1.0);

  if (state->active_editor != NULL) {
    gtk_widget_grab_focus(state->active_editor);
  }
}

void zathura_annotation_panel_set_all_collapsed(GtkWidget* panel, bool collapsed) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL || state->collapsed_ids == NULL) {
    return;
  }

  sync_active_editor_draft(state);
  g_hash_table_remove_all(state->collapsed_ids);
  if (collapsed && state->zathura != NULL && state->zathura->annotations.list != NULL) {
    for (size_t idx = 0; idx != girara_list_size(state->zathura->annotations.list); ++idx) {
      zathura_annotation_t* ann = girara_list_nth(state->zathura->annotations.list, idx);
      if (ann != NULL && ann->id != NULL) {
        g_hash_table_add(state->collapsed_ids, g_strdup(ann->id));
      }
    }
  }
  zathura_annotation_panel_refresh(panel);
}

bool zathura_annotation_panel_are_all_collapsed(GtkWidget* panel) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL || state->card_count == 0) {
    return false;
  }
  return state->collapsed_count == state->card_count;
}

void zathura_annotation_panel_set_annotation_collapsed(GtkWidget* panel, const char* annotation_id, bool collapsed) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL || state->collapsed_ids == NULL || annotation_id == NULL) {
    return;
  }

  sync_active_editor_draft(state);
  if (collapsed) {
    g_hash_table_add(state->collapsed_ids, g_strdup(annotation_id));
  } else {
    g_hash_table_remove(state->collapsed_ids, annotation_id);
  }
  zathura_annotation_panel_refresh(panel);
}

bool zathura_annotation_panel_is_annotation_collapsed(GtkWidget* panel, const char* annotation_id) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  return is_annotation_collapsed(state, annotation_id);
}

bool zathura_annotation_panel_scroll(GtkWidget* panel, double dx, double dy) {
  (void)dx;
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL || state->scrolled_window == NULL) {
    return false;
  }

  cancel_pending_scroll(state);

  GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->scrolled_window));
  if (vadj == NULL) {
    return false;
  }

  double step = gtk_adjustment_get_step_increment(vadj);
  if (step <= 1.0) {
    const double page_size = gtk_adjustment_get_page_size(vadj);
    step                   = page_size > 0.0 ? MAX(48.0, page_size * 0.15) : 56.0;
  }

  const double lower     = gtk_adjustment_get_lower(vadj);
  const double upper     = gtk_adjustment_get_upper(vadj);
  const double page_size = gtk_adjustment_get_page_size(vadj);
  const double max_val   = MAX(lower, upper - page_size);
  const double cur_val   = gtk_adjustment_get_value(vadj);
  const double next_val  = CLAMP(cur_val + dy * step, lower, max_val);

  gtk_adjustment_set_value(vadj, next_val);
  return true;
}

GtkAdjustment* zathura_annotation_panel_get_vadjustment(GtkWidget* panel) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL || state->scrolled_window == NULL) {
    return NULL;
  }
  return gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->scrolled_window));
}

unsigned int zathura_annotation_panel_get_card_count(GtkWidget* panel) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL) {
    return 0;
  }
  return state->card_count;
}

GtkWidget* zathura_annotation_panel_get_editor_text_view(GtkWidget* panel) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL) {
    return NULL;
  }
  return state->active_editor;
}

void zathura_annotation_panel_set_filter_text(GtkWidget* panel, const char* filter_text) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL) {
    return;
  }

  g_free(state->filter_text);
  state->filter_text = g_strdup(filter_text != NULL ? filter_text : "");
  if (state->filter_entry != NULL) {
    state->updating_filter = true;
    gtk_editable_set_text(GTK_EDITABLE(state->filter_entry), state->filter_text);
    state->updating_filter = false;
  }
  zathura_annotation_panel_refresh(panel);
}

const char* zathura_annotation_panel_get_filter_text(GtkWidget* panel) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL || state->filter_text == NULL) {
    return "";
  }
  return state->filter_text;
}

GtkWidget* zathura_annotation_panel_get_filter_entry(GtkWidget* panel) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL) {
    return NULL;
  }
  return state->filter_entry;
}

static char* sanitize_single_line_field(const char* text) {
  if (text == NULL) {
    return g_strdup("");
  }

  GString* out = g_string_sized_new(strlen(text));
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p == '\r' || *p == '\n' || *p == '\t') {
      if (out->len > 0 && out->str[out->len - 1] != ' ') {
        g_string_append_c(out, ' ');
      }
    } else {
      g_string_append_c(out, *p);
    }
  }

  while (out->len > 0 && out->str[out->len - 1] == ' ') {
    g_string_truncate(out, out->len - 1);
  }
  return g_string_free(out, FALSE);
}

static void append_csv_field(GString* out, const char* field) {
  const char* safe        = field != NULL ? field : "";
  const bool needs_quotes = (strchr(safe, ',') != NULL || strchr(safe, '"') != NULL);
  if (!needs_quotes) {
    g_string_append(out, safe);
    return;
  }

  g_string_append_c(out, '"');
  for (const char* p = safe; *p != '\0'; ++p) {
    if (*p == '"') {
      g_string_append(out, "\"\"");
    } else {
      g_string_append_c(out, *p);
    }
  }
  g_string_append_c(out, '"');
}

char* zathura_annotation_panel_format_export(GtkWidget* panel, bool csv_format, unsigned int* exported_count) {
  if (exported_count != NULL) {
    *exported_count = 0;
  }

  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL) {
    return NULL;
  }

  sync_active_editor_draft(state);

  GString* out       = g_string_new(csv_format ? "page,quote,comment\n" : "page\tquote\tcomment\n");
  unsigned int count = 0;

  if (state->zathura != NULL && state->zathura->annotations.list != NULL) {
    for (size_t idx = 0; idx != girara_list_size(state->zathura->annotations.list); ++idx) {
      zathura_annotation_t* ann = girara_list_nth(state->zathura->annotations.list, idx);
      if (ann == NULL) {
        continue;
      }

      const bool has_comm = zathura_annotation_has_comment(ann);
      if (!has_comm || !annotation_matches_filter(ann, state->filter_text)) {
        continue;
      }

      g_autofree char* clean_quote   = sanitize_single_line_field(ann->quote);
      g_autofree char* clean_comment = sanitize_single_line_field(ann->comment);

      if (csv_format) {
        g_string_append_printf(out, "%u,", ann->page + 1);
        append_csv_field(out, clean_quote);
        g_string_append_c(out, ',');
        append_csv_field(out, clean_comment);
        g_string_append_c(out, '\n');
      } else {
        g_string_append_printf(out, "%u\t%s\t%s\n", ann->page + 1, clean_quote, clean_comment);
      }
      ++count;
    }
  }

  if (exported_count != NULL) {
    *exported_count = count;
  }
  return g_string_free(out, FALSE);
}

char* zathura_annotation_panel_get_default_export_path(zathura_t* zathura) {
  if (zathura == NULL || zathura->document == NULL) {
    return NULL;
  }

  const char* doc_path = zathura_document_get_path(zathura->document);
  if (doc_path == NULL || doc_path[0] == '\0') {
    return NULL;
  }

  const size_t len = strlen(doc_path);
  if (len > 4 && g_ascii_strcasecmp(doc_path + len - 4, ".pdf") == 0) {
    g_autofree char* stem = g_strndup(doc_path, len - 4);
    return g_strdup_printf("%s-comments.tsv", stem);
  }
  return g_strdup_printf("%s-comments.tsv", doc_path);
}

bool zathura_annotation_panel_export(GtkWidget* panel, const char* target_path) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL) {
    return false;
  }

  g_autofree char* resolved_path = NULL;
  if (target_path != NULL && target_path[0] != '\0') {
    resolved_path = girara_fix_path(target_path);
    if (resolved_path == NULL) {
      resolved_path = g_strdup(target_path);
    }
  } else {
    resolved_path = zathura_annotation_panel_get_default_export_path(state->zathura);
  }

  bool csv_format = false;
  if (resolved_path != NULL) {
    const size_t path_len = strlen(resolved_path);
    if (path_len > 4 && g_ascii_strcasecmp(resolved_path + path_len - 4, ".csv") == 0) {
      csv_format = true;
    }
  }

  unsigned int exported_count = 0;
  g_autofree char* content    = zathura_annotation_panel_format_export(panel, csv_format, &exported_count);
  if (content == NULL) {
    return false;
  }

  GdkDisplay* display = gtk_widget_get_display(panel);
  if (display != NULL) {
    GdkClipboard* clipboard = gdk_display_get_clipboard(display);
    if (clipboard != NULL) {
      gdk_clipboard_set_text(clipboard, content);
    }
  }
  if (state->zathura != NULL) {
    GdkClipboard* selection = get_selection(state->zathura);
    if (selection != NULL) {
      gdk_clipboard_set_text(selection, content);
    }
  }

  if (resolved_path != NULL) {
    g_autoptr(GError) error = NULL;
    if (!g_file_set_contents(resolved_path, content, -1, &error)) {
      if (state->zathura != NULL && state->zathura->ui.session != NULL) {
        girara_notify(state->zathura->ui.session, GIRARA_ERROR, _("Failed to export comments to %s: %s"),
                      resolved_path, error != NULL ? error->message : _("unknown error"));
      }
      return false;
    }

    if (state->zathura != NULL && state->zathura->ui.session != NULL) {
      girara_notify(state->zathura->ui.session, GIRARA_INFO, _("Exported %u comment(s) to %s and clipboard."),
                    exported_count, resolved_path);
    }
    return true;
  }

  if (state->zathura != NULL && state->zathura->ui.session != NULL) {
    girara_notify(state->zathura->ui.session, GIRARA_INFO, _("Copied %u comment(s) to clipboard."), exported_count);
  }
  return true;
}
