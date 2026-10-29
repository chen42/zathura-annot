/* SPDX-License-Identifier: Zlib */
/* Part of zathura-annot (modified fork of pwmt/zathura) */

#include "annotation-panel.h"

#include <girara-gtk/session.h>
#include <girara-gtk/settings.h>
#include <glib/gi18n.h>
#include <string.h>

#include "annotations.h"
#include "zathura.h"

typedef struct zathura_annotation_panel_state_s {
  zathura_t* zathura;
  GtkWidget* root;
  GtkWidget* count_label;
  GtkWidget* show_resolved_check;
  GtkWidget* scrolled_window;
  GtkWidget* list_box;
  GtkWidget* active_editor;
  bool show_resolved;
  unsigned int card_count;
  bool updating_toggle;
} zathura_annotation_panel_state_t;

static void panel_state_free(gpointer data) {
  g_free(data);
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

static void cb_card_clicked(GtkGestureClick* UNUSED(gesture), gint UNUSED(n_press), gdouble UNUSED(x),
                            gdouble UNUSED(y), gpointer user_data) {
  GtkWidget* card                         = GTK_WIDGET(user_data);
  zathura_annotation_panel_state_t* state = g_object_get_data(G_OBJECT(card), "zathura-panel-state");
  const char* id                          = g_object_get_data(G_OBJECT(card), "zathura-annotation-id");
  if (state == NULL || state->zathura == NULL || id == NULL) {
    return;
  }

  if (g_strcmp0(state->zathura->annotations.active_id, id) == 0) {
    return;
  }

  zathura_annotations_set_active(state->zathura, id);
  for (GtkWidget* child = gtk_widget_get_first_child(state->list_box); child != NULL;
       child            = gtk_widget_get_next_sibling(child)) {
    const char* child_id = g_object_get_data(G_OBJECT(child), "zathura-annotation-id");
    if (g_strcmp0(child_id, id) == 0) {
      gtk_widget_add_css_class(child, "active");
    } else {
      gtk_widget_remove_css_class(child, "active");
    }
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

static void cb_card_resolve_clicked(GtkButton* button, gpointer user_data) {
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
  ann->resolved   = !ann->resolved;
  ann->updated_at = g_get_real_time();
  zathura_annotations_mark_dirty(state->zathura);
  zathura_annotation_panel_refresh(state->root);
  zathura_annotations_queue_draw_page(state->zathura, ann->page);
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

static void cb_show_resolved_toggled(GtkCheckButton* check, gpointer user_data) {
  zathura_annotation_panel_state_t* state = user_data;
  if (state == NULL || state->updating_toggle) {
    return;
  }

  sync_active_editor_draft(state);
  state->show_resolved = gtk_check_button_get_active(check);
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

static GtkWidget* create_card_widget(zathura_annotation_panel_state_t* state, zathura_annotation_t* ann) {
  GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_add_css_class(card, "annotation-card");
  gtk_widget_add_css_class(card, ann->color == ZATHURA_ANNOTATION_COLOR_RED ? "red" : "yellow");
  if (g_strcmp0(ann->id, state->zathura->annotations.active_id) == 0) {
    gtk_widget_add_css_class(card, "active");
  }
  if (ann->resolved) {
    gtk_widget_add_css_class(card, "resolved");
  }
  g_object_set_data(G_OBJECT(card), "zathura-panel-state", state);
  g_object_set_data_full(G_OBJECT(card), "zathura-annotation-id", g_strdup(ann->id), g_free);

  GtkGesture* card_click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(card_click), GDK_BUTTON_PRIMARY);
  g_signal_connect(card_click, "pressed", G_CALLBACK(cb_card_clicked), card);
  gtk_widget_add_controller(card, GTK_EVENT_CONTROLLER(card_click));

  /* 1. Top row: quoted snippet on the left, delete (×) button on the top right */
  GtkWidget* top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
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
  const bool is_editing = (g_strcmp0(ann->id, state->zathura->annotations.editing_id) == 0);
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

  GtkWidget* resolve_btn = gtk_button_new_with_label(ann->resolved ? _("Unresolve") : _("Resolve"));
  gtk_widget_add_css_class(resolve_btn, "annotation-card-btn");
  gtk_widget_add_css_class(resolve_btn, "flat");
  g_object_set_data_full(G_OBJECT(resolve_btn), "zathura-annotation-id", g_strdup(ann->id), g_free);
  g_signal_connect(resolve_btn, "clicked", G_CALLBACK(cb_card_resolve_clicked), state);
  gtk_box_append(GTK_BOX(bottom_row), resolve_btn);

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
  state->show_resolved                    = false;
  g_object_set_data_full(G_OBJECT(panel), "zathura-annotation-panel-state", state, panel_state_free);

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

  state->show_resolved_check = gtk_check_button_new_with_label(_("Show resolved"));
  gtk_widget_add_css_class(state->show_resolved_check, "annotation-header-check");
  g_signal_connect(state->show_resolved_check, "toggled", G_CALLBACK(cb_show_resolved_toggled), state);
  gtk_box_append(GTK_BOX(header), state->show_resolved_check);

  GtkWidget* close_btn = gtk_button_new_with_label("×");
  gtk_widget_add_css_class(close_btn, "annotation-card-btn");
  gtk_widget_add_css_class(close_btn, "annotation-close-btn");
  gtk_widget_add_css_class(close_btn, "flat");
  g_signal_connect(close_btn, "clicked", G_CALLBACK(cb_close_panel_clicked), state);
  gtk_box_append(GTK_BOX(header), close_btn);

  gtk_box_append(GTK_BOX(panel), header);

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
  state->active_editor = NULL;
  state->card_count    = 0;

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
      if (!is_editing && (!has_comm || (!state->show_resolved && ann->resolved))) {
        continue;
      }

      GtkWidget* card = create_card_widget(state, ann);
      gtk_box_append(GTK_BOX(state->list_box), card);
      ++state->card_count;
    }
  }

  g_autofree char* count_str = g_strdup_printf("%u", state->card_count);
  gtk_label_set_text(GTK_LABEL(state->count_label), count_str);

  if (state->card_count == 0) {
    GtkWidget* empty_label = gtk_label_new(_("No comments yet. Select text and right-click to add a comment."));
    gtk_widget_add_css_class(empty_label, "annotation-empty-state");
    gtk_label_set_wrap(GTK_LABEL(empty_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(empty_label), 0.0f);
    gtk_box_append(GTK_BOX(state->list_box), empty_label);
  }
}

void zathura_annotation_panel_focus_annotation(GtkWidget* panel, const char* annotation_id) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL || annotation_id == NULL) {
    return;
  }

  zathura_annotation_t* ann = zathura_annotations_find(state->zathura, annotation_id);
  if (ann != NULL && ann->resolved && !state->show_resolved) {
    zathura_annotation_panel_set_show_resolved(panel, true);
  } else {
    zathura_annotation_panel_refresh(panel);
  }

  for (GtkWidget* child = gtk_widget_get_first_child(state->list_box); child != NULL;
       child            = gtk_widget_get_next_sibling(child)) {
    const char* card_id = g_object_get_data(G_OBJECT(child), "zathura-annotation-id");
    if (g_strcmp0(card_id, annotation_id) == 0) {
      gtk_widget_grab_focus(child);
      break;
    }
  }
}

void zathura_annotation_panel_start_edit(GtkWidget* panel, const char* annotation_id, bool is_new) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL || state->zathura == NULL || annotation_id == NULL) {
    return;
  }

  sync_active_editor_draft(state);
  zathura_annotations_set_active(state->zathura, annotation_id);
  zathura_annotations_start_edit(state->zathura, annotation_id, is_new);
  zathura_annotation_panel_refresh(panel);

  if (state->active_editor != NULL) {
    gtk_widget_grab_focus(state->active_editor);
  }
}

void zathura_annotation_panel_set_show_resolved(GtkWidget* panel, bool show_resolved) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL) {
    return;
  }

  state->show_resolved   = show_resolved;
  state->updating_toggle = true;
  gtk_check_button_set_active(GTK_CHECK_BUTTON(state->show_resolved_check), show_resolved);
  state->updating_toggle = false;

  zathura_annotation_panel_refresh(panel);
}

bool zathura_annotation_panel_get_show_resolved(GtkWidget* panel) {
  zathura_annotation_panel_state_t* state = get_panel_state(panel);
  if (state == NULL) {
    return false;
  }
  return state->show_resolved;
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
