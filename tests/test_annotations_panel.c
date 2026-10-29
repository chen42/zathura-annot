/* SPDX-License-Identifier: Zlib */

#include <girara-gtk/callbacks.h>
#include <girara-gtk/internal.h>
#include <girara-gtk/session.h>
#include <girara/datastructures.h>
#include <girara/log.h>
#include <gtk/gtk.h>

#include "annotation-panel.h"
#include "annotations.h"
#include "commands.h"
#include "shortcuts.h"
#include "tests.h"
#include "zathura.h"

static zathura_t* create_test_zathura(void) {
  girara_set_log_level(GIRARA_ERROR);
  zathura_t* zathura = zathura_create();
  g_assert_nonnull(zathura);
  if (g_getenv("G_TEST_SRCDIR") != NULL) {
    zathura_set_config_dir(zathura, g_getenv("G_TEST_SRCDIR"));
  }
  g_assert_true(zathura_init(zathura));
  return zathura;
}

static GtkWidget* find_widget_by_css_class(GtkWidget* root, const char* css_class) {
  if (root == NULL) {
    return NULL;
  }
  if (gtk_widget_has_css_class(root, css_class)) {
    return root;
  }
  for (GtkWidget* child = gtk_widget_get_first_child(root); child != NULL;
       child            = gtk_widget_get_next_sibling(child)) {
    GtkWidget* found = find_widget_by_css_class(child, css_class);
    if (found != NULL) {
      return found;
    }
  }
  return NULL;
}

static void append_rect(girara_list_t* list, double x1, double y1, double x2, double y2) {
  zathura_rectangle_t* r = g_malloc0(sizeof(zathura_rectangle_t));
  *r                     = (zathura_rectangle_t){x1, y1, x2, y2};
  girara_list_append(list, r);
}

static void test_panel_card_rendering_and_resolved_filter(void) {
  zathura_t* zathura = create_test_zathura();
  g_assert_nonnull(zathura->ui.annotation_panel);

  girara_list_t* r1 = girara_list_new_with_free(g_free);
  append_rect(r1, 10.0, 20.0, 100.0, 35.0);
  girara_list_t* r2 = girara_list_new_with_free(g_free);
  append_rect(r2, 10.0, 50.0, 100.0, 65.0);
  girara_list_t* r3 = girara_list_new_with_free(g_free);
  append_rect(r3, 10.0, 80.0, 100.0, 95.0);

  zathura_annotation_t* a1 =
      zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_YELLOW, r1, "First quote", "Unresolved 1");
  zathura_annotation_t* a2 =
      zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_RED, r2, "Second quote", "Unresolved 2");
  zathura_annotation_t* a3 =
      zathura_annotation_new(1, ZATHURA_ANNOTATION_COLOR_YELLOW, r3, "Third quote", "Resolved comment");
  a3->resolved = true;

  girara_list_free(r1);
  girara_list_free(r2);
  girara_list_free(r3);

  zathura_annotations_add(zathura, a1);
  zathura_annotations_add(zathura, a2);
  zathura_annotations_add(zathura, a3);

  gtk_widget_set_visible(zathura->ui.annotation_panel, TRUE);
  zathura_annotation_panel_refresh(zathura->ui.annotation_panel);

  GtkWidget* badge = find_widget_by_css_class(zathura->ui.annotation_panel, "annotation-count-badge");
  g_assert_nonnull(badge);
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(badge)), ==, "2");
  g_assert_cmpuint(zathura_annotation_panel_get_card_count(zathura->ui.annotation_panel), ==, 2);

  /* Toggle Show resolved on */
  zathura_annotation_panel_set_show_resolved(zathura->ui.annotation_panel, true);
  g_assert_true(zathura_annotation_panel_get_show_resolved(zathura->ui.annotation_panel));
  g_assert_cmpuint(zathura_annotation_panel_get_card_count(zathura->ui.annotation_panel), ==, 3);

  GtkWidget* resolved_card = find_widget_by_css_class(zathura->ui.annotation_panel, "resolved");
  g_assert_nonnull(resolved_card);

  /* Verify action buttons are placed at the bottom of the card and use the compact low-contrast class */
  GtkWidget* last_child = gtk_widget_get_last_child(resolved_card);
  g_assert_nonnull(last_child);
  g_assert_true(gtk_widget_has_css_class(last_child, "annotation-card-actions"));
  GtkWidget* compact_btn = find_widget_by_css_class(last_child, "annotation-card-btn");
  g_assert_nonnull(compact_btn);

  zathura_free(zathura);
}

static void test_panel_inline_editor_ctrl_enter_and_escape(void) {
  zathura_t* zathura = create_test_zathura();

  girara_list_t* r1 = girara_list_new_with_free(g_free);
  append_rect(r1, 10.0, 20.0, 100.0, 35.0);

  zathura_annotation_t* ann =
      zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_YELLOW, r1, "Editable quote", "");
  zathura_annotations_add(zathura, ann);
  g_autofree char* id_copy = g_strdup(ann->id);

  gtk_widget_set_visible(zathura->ui.annotation_panel, TRUE);
  zathura_annotation_panel_start_edit(zathura->ui.annotation_panel, id_copy, false);

  GtkWidget* editor = zathura_annotation_panel_get_editor_text_view(zathura->ui.annotation_panel);
  g_assert_nonnull(editor);
  g_assert_true(GTK_IS_TEXT_VIEW(editor));

  GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
  gtk_text_buffer_set_text(buffer, "Reviewer comment", -1);

  /* Simulate Ctrl+Enter key press on the inline editor */
  g_assert_true(
      zathura_annotation_panel_handle_editor_key(zathura->ui.annotation_panel, GDK_KEY_Return, GDK_CONTROL_MASK));

  zathura_annotation_t* committed = zathura_annotations_find(zathura, id_copy);
  g_assert_nonnull(committed);
  g_assert_cmpstr(committed->comment, ==, "Reviewer comment");
  g_assert_null(zathura->annotations.editing_id);

  /* Now create a new pending-comment annotation (is_new == true) and cancel with Escape */
  zathura_annotation_t* pending =
      zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_RED, r1, "Pending quote", "");
  zathura_annotations_add(zathura, pending);
  g_autofree char* pending_id = g_strdup(pending->id);

  zathura_annotation_panel_start_edit(zathura->ui.annotation_panel, pending_id, true);
  g_assert_true(zathura_annotation_panel_handle_editor_key(zathura->ui.annotation_panel, GDK_KEY_Escape, 0));

  /* Pending comment with no existing text must be removed on Escape */
  g_assert_null(zathura_annotations_find(zathura, pending_id));

  girara_list_free(r1);
  zathura_free(zathura);
}

static void test_panel_editor_draft_survival_across_refresh(void) {
  zathura_t* zathura = create_test_zathura();

  girara_list_t* r1 = girara_list_new_with_free(g_free);
  append_rect(r1, 10.0, 20.0, 100.0, 35.0);

  zathura_annotation_t* ann =
      zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_YELLOW, r1, "Quote", "Initial comment");
  girara_list_free(r1);
  zathura_annotations_add(zathura, ann);

  gtk_widget_set_visible(zathura->ui.annotation_panel, TRUE);
  zathura_annotation_panel_start_edit(zathura->ui.annotation_panel, ann->id, false);

  GtkWidget* editor = zathura_annotation_panel_get_editor_text_view(zathura->ui.annotation_panel);
  g_assert_nonnull(editor);
  GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
  gtk_text_buffer_set_text(buffer, "Draft in progress", -1);

  /* Refresh the panel twice while editing */
  zathura_annotation_panel_refresh(zathura->ui.annotation_panel);
  zathura_annotation_panel_refresh(zathura->ui.annotation_panel);

  g_assert_cmpstr(zathura->annotations.draft_comment, ==, "Draft in progress");

  GtkWidget* new_editor = zathura_annotation_panel_get_editor_text_view(zathura->ui.annotation_panel);
  g_assert_nonnull(new_editor);
  GtkTextBuffer* new_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(new_editor));
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(new_buffer, &start, &end);
  g_autofree char* text = gtk_text_buffer_get_text(new_buffer, &start, &end, FALSE);
  g_assert_cmpstr(text, ==, "Draft in progress");

  zathura_free(zathura);
}

static void test_panel_toggle_command_and_shortcut(void) {
  zathura_t* zathura        = create_test_zathura();
  girara_session_t* session = zathura->ui.session;

  g_assert_false(gtk_widget_get_visible(zathura->ui.annotation_panel));

  /* Toggle via shortcut */
  g_assert_true(sc_toggle_annotations(session, NULL, NULL, 0));
  g_assert_true(gtk_widget_get_visible(zathura->ui.annotation_panel));

  g_assert_true(sc_toggle_annotations(session, NULL, NULL, 0));
  g_assert_false(gtk_widget_get_visible(zathura->ui.annotation_panel));

  /* Open via :comments open */
  girara_list_t* args_open = girara_list_new();
  girara_list_append(args_open, (void*)"open");
  g_assert_true(cmd_comments(session, args_open));
  g_assert_true(gtk_widget_get_visible(zathura->ui.annotation_panel));
  girara_list_free(args_open);

  /* Close via :comments close */
  girara_list_t* args_close = girara_list_new();
  girara_list_append(args_close, (void*)"close");
  g_assert_true(cmd_comments(session, args_close));
  g_assert_false(gtk_widget_get_visible(zathura->ui.annotation_panel));
  girara_list_free(args_close);

  /* Toggle via :comments with no args */
  girara_list_t* args_empty = girara_list_new();
  g_assert_true(cmd_comments(session, args_empty));
  g_assert_true(gtk_widget_get_visible(zathura->ui.annotation_panel));
  girara_list_free(args_empty);

  /* Verify content_overlay is the parent of annotation_panel and view */
  g_assert_nonnull(zathura->ui.content_overlay);
  g_assert_true(gtk_widget_get_parent(zathura->ui.annotation_panel) == zathura->ui.content_overlay);
  g_assert_true(gtk_overlay_get_child(GTK_OVERLAY(zathura->ui.content_overlay)) == zathura->ui.view);

  zathura_free(zathura);
}

static void test_panel_girara_key_guard_when_focused(void) {
  zathura_t* zathura        = create_test_zathura();
  girara_session_t* session = zathura->ui.session;

  girara_list_t* r1 = girara_list_new_with_free(g_free);
  append_rect(r1, 10.0, 20.0, 100.0, 35.0);

  zathura_annotation_t* ann =
      zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_YELLOW, r1, "Quote", "Comment");
  girara_list_free(r1);
  zathura_annotations_add(zathura, ann);

  gtk_widget_set_visible(zathura->ui.annotation_panel, TRUE);
  zathura_annotation_panel_start_edit(zathura->ui.annotation_panel, ann->id, false);

  GtkWidget* editor = zathura_annotation_panel_get_editor_text_view(zathura->ui.annotation_panel);
  g_assert_nonnull(editor);

  gtk_window_set_focus(GTK_WINDOW(session->gtk.window), editor);

  /* When editor inside .annotation-panel is focused, girara_callback_view_key_press_event must return FALSE */
  gboolean handled_q = girara_callback_view_key_press_event(NULL, GDK_KEY_q, 0, 0, session);
  gboolean handled_j = girara_callback_view_key_press_event(NULL, GDK_KEY_j, 0, 0, session);
  gboolean handled_k = girara_callback_view_key_press_event(NULL, GDK_KEY_k, 0, 0, session);
  g_assert_false(handled_q);
  g_assert_false(handled_j);
  g_assert_false(handled_k);

  zathura_free(zathura);
}

int main(int argc, char* argv[]) {
  setup_logger();
  gtk_init();
  (void)gtk_settings_get_default();
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/annotations/panel/card_rendering_and_resolved_filter",
                  test_panel_card_rendering_and_resolved_filter);
  g_test_add_func("/annotations/panel/inline_editor_ctrl_enter_and_escape",
                  test_panel_inline_editor_ctrl_enter_and_escape);
  g_test_add_func("/annotations/panel/editor_draft_survival_across_refresh",
                  test_panel_editor_draft_survival_across_refresh);
  g_test_add_func("/annotations/panel/toggle_command_and_shortcut", test_panel_toggle_command_and_shortcut);
  g_test_add_func("/annotations/panel/girara_key_guard_when_focused", test_panel_girara_key_guard_when_focused);

  return g_test_run();
}
