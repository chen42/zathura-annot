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
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(badge)), ==, "3");
  g_assert_cmpuint(zathura_annotation_panel_get_card_count(zathura->ui.annotation_panel), ==, 3);

  GtkWidget* resolved_card = find_widget_by_css_class(zathura->ui.annotation_panel, "resolved");
  g_assert_null(resolved_card);

  GtkWidget* card = find_widget_by_css_class(zathura->ui.annotation_panel, "annotation-card");
  g_assert_nonnull(card);

  /* Verify action buttons are placed at the bottom of the card and use the compact low-contrast class */
  GtkWidget* last_child = gtk_widget_get_last_child(card);
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

static void test_panel_roll_up_all_and_expand_one_by_one(void) {
  zathura_t* zathura = create_test_zathura();
  GtkWidget* panel   = zathura->ui.annotation_panel;
  g_assert_nonnull(panel);

  girara_list_t* r1 = girara_list_new_with_free(g_free);
  append_rect(r1, 10.0, 20.0, 100.0, 35.0);

  zathura_annotation_t* a1 = zathura_annotation_new(
      0, ZATHURA_ANNOTATION_COLOR_RED, r1, "First quote", "First line of comment 1\nSecond line of comment 1");
  zathura_annotation_t* a2 = zathura_annotation_new(
      0, ZATHURA_ANNOTATION_COLOR_RED, r1, "Second quote", "Only first line of comment 2\nDetails of comment 2");
  g_autofree char* id1 = g_strdup(a1->id);
  g_autofree char* id2 = g_strdup(a2->id);
  girara_list_free(r1);

  zathura_annotations_add(zathura, a1);
  zathura_annotations_add(zathura, a2);

  gtk_widget_set_visible(panel, TRUE);
  zathura_annotation_panel_refresh(panel);

  g_assert_false(zathura_annotation_panel_are_all_collapsed(panel));
  g_assert_false(zathura_annotation_panel_is_annotation_collapsed(panel, id1));
  g_assert_false(zathura_annotation_panel_is_annotation_collapsed(panel, id2));

  /* Global roll up: all comments collapse and show only the first line */
  GtkWidget* collapse_all_btn = find_widget_by_css_class(panel, "annotation-collapse-all-btn");
  g_assert_nonnull(collapse_all_btn);
  g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(collapse_all_btn)), ==, "Roll up");

  zathura_annotation_panel_set_all_collapsed(panel, true);
  g_assert_true(zathura_annotation_panel_are_all_collapsed(panel));
  g_assert_true(zathura_annotation_panel_is_annotation_collapsed(panel, id1));
  g_assert_true(zathura_annotation_panel_is_annotation_collapsed(panel, id2));
  g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(collapse_all_btn)), ==, "Expand all");

  GtkWidget* first_collapsed_card = find_widget_by_css_class(panel, "collapsed");
  g_assert_nonnull(first_collapsed_card);
  GtkWidget* comment_label = find_widget_by_css_class(first_collapsed_card, "annotation-comment-body");
  g_assert_nonnull(comment_label);
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(comment_label)), ==, "First line of comment 1");
  g_assert_null(find_widget_by_css_class(first_collapsed_card, "annotation-quote"));

  /* Expand comment 1 individually while comment 2 stays rolled up */
  zathura_annotation_panel_set_annotation_collapsed(panel, id1, false);
  g_assert_false(zathura_annotation_panel_is_annotation_collapsed(panel, id1));
  g_assert_true(zathura_annotation_panel_is_annotation_collapsed(panel, id2));
  g_assert_false(zathura_annotation_panel_are_all_collapsed(panel));
  g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(collapse_all_btn)), ==, "Roll up");

  /* Double-clicking a comment card toggles roll up / expand */
  GtkWidget* card1      = find_widget_by_css_class(panel, "annotation-card");
  g_assert_nonnull(card1);
  GListModel* ctrls     = gtk_widget_observe_controllers(card1);
  GtkGestureClick* click = NULL;
  for (guint i = 0; i < g_list_model_get_n_items(ctrls); ++i) {
    GObject* obj = g_list_model_get_item(ctrls, i);
    if (GTK_IS_GESTURE_CLICK(obj)) {
      click = GTK_GESTURE_CLICK(obj);
      g_object_unref(obj);
      break;
    }
    g_object_unref(obj);
  }
  g_object_unref(ctrls);
  g_assert_nonnull(click);

  /* Double-click rolls up expanded card1 */
  g_signal_emit_by_name(click, "pressed", 2, 10.0, 10.0);
  g_assert_true(zathura_annotation_panel_is_annotation_collapsed(panel, id1));

  /* Double-click again expands collapsed card1 */
  card1 = find_widget_by_css_class(panel, "annotation-card");
  g_assert_nonnull(card1);
  ctrls = gtk_widget_observe_controllers(card1);
  click = NULL;
  for (guint i = 0; i < g_list_model_get_n_items(ctrls); ++i) {
    GObject* obj = g_list_model_get_item(ctrls, i);
    if (GTK_IS_GESTURE_CLICK(obj)) {
      click = GTK_GESTURE_CLICK(obj);
      g_object_unref(obj);
      break;
    }
    g_object_unref(obj);
  }
  g_object_unref(ctrls);
  g_assert_nonnull(click);

  g_signal_emit_by_name(click, "pressed", 2, 10.0, 10.0);
  g_assert_false(zathura_annotation_panel_is_annotation_collapsed(panel, id1));

  zathura_free(zathura);
}

static void test_panel_mouse_wheel_scroll(void) {
  zathura_t* zathura = create_test_zathura();
  GtkWidget* panel   = zathura->ui.annotation_panel;
  g_assert_nonnull(panel);

  gtk_widget_set_visible(panel, TRUE);
  GtkAdjustment* vadj = zathura_annotation_panel_get_vadjustment(panel);
  g_assert_nonnull(vadj);

  gtk_adjustment_configure(vadj, 0.0, 0.0, 600.0, 40.0, 200.0, 200.0);
  g_assert_cmpfloat_with_epsilon(gtk_adjustment_get_value(vadj), 0.0, 1e-6);

  /* Scroll down two wheel steps */
  g_assert_true(zathura_annotation_panel_scroll(panel, 0.0, 2.0));
  g_assert_cmpfloat_with_epsilon(gtk_adjustment_get_value(vadj), 80.0, 1e-6);

  /* Scroll up one wheel step */
  g_assert_true(zathura_annotation_panel_scroll(panel, 0.0, -1.0));
  g_assert_cmpfloat_with_epsilon(gtk_adjustment_get_value(vadj), 40.0, 1e-6);

  zathura_free(zathura);
}

static void test_panel_filter_comments_by_word_and_phrase(void) {
  zathura_t* zathura        = create_test_zathura();
  girara_session_t* session = zathura->ui.session;
  GtkWidget* panel          = zathura->ui.annotation_panel;
  g_assert_nonnull(panel);

  girara_list_t* r1 = girara_list_new_with_free(g_free);
  append_rect(r1, 10.0, 20.0, 100.0, 35.0);

  zathura_annotation_t* a1 = zathura_annotation_new(
      0, ZATHURA_ANNOTATION_COLOR_RED, r1, "Quote 1", "Verify statistical power for cohort");
  zathura_annotation_t* a2 = zathura_annotation_new(
      0, ZATHURA_ANNOTATION_COLOR_RED, r1, "Quote 2", "Check dopamine receptor expression");
  zathura_annotation_t* a3 = zathura_annotation_new(
      0, ZATHURA_ANNOTATION_COLOR_RED, r1, "Quote 3", "Statistical power calculation in supplement");
  girara_list_free(r1);

  zathura_annotations_add(zathura, a1);
  zathura_annotations_add(zathura, a2);
  zathura_annotations_add(zathura, a3);

  gtk_widget_set_visible(panel, TRUE);
  zathura_annotation_panel_refresh(panel);
  g_assert_cmpuint(zathura_annotation_panel_get_card_count(panel), ==, 3);

  GtkWidget* filter_entry = zathura_annotation_panel_get_filter_entry(panel);
  g_assert_nonnull(filter_entry);

  /* Typing into the filter entry filters comments case-insensitively */
  gtk_editable_set_text(GTK_EDITABLE(filter_entry), "statistical power");
  g_assert_cmpstr(zathura_annotation_panel_get_filter_text(panel), ==, "statistical power");
  g_assert_cmpuint(zathura_annotation_panel_get_card_count(panel), ==, 2);

  /* Filtering by a single word */
  zathura_annotation_panel_set_filter_text(panel, "DOPAMINE");
  g_assert_cmpuint(zathura_annotation_panel_get_card_count(panel), ==, 1);

  /* Filtering via :comments filter <phrase> */
  girara_list_t* args_filter = girara_list_new();
  girara_list_append(args_filter, (void*)"filter");
  girara_list_append(args_filter, (void*)"in");
  girara_list_append(args_filter, (void*)"supplement");
  g_assert_true(cmd_comments(session, args_filter));
  girara_list_free(args_filter);
  g_assert_cmpstr(zathura_annotation_panel_get_filter_text(panel), ==, "in supplement");
  g_assert_cmpuint(zathura_annotation_panel_get_card_count(panel), ==, 1);

  /* Non-matching filter shows 0 cards and empty state */
  zathura_annotation_panel_set_filter_text(panel, "nonexistent phrase");
  g_assert_cmpuint(zathura_annotation_panel_get_card_count(panel), ==, 0);
  GtkWidget* empty_label = find_widget_by_css_class(panel, "annotation-empty-state");
  g_assert_nonnull(empty_label);
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(empty_label)), ==, "No matching comments.");

  /* Clearing the filter restores all 3 comments */
  zathura_annotation_panel_set_filter_text(panel, "");
  g_assert_cmpuint(zathura_annotation_panel_get_card_count(panel), ==, 3);

  zathura_free(zathura);
}

static void test_panel_export_tsv_respects_filter_and_writes_file(void) {
  zathura_t* zathura        = create_test_zathura();
  girara_session_t* session = zathura->ui.session;
  GtkWidget* panel          = zathura->ui.annotation_panel;
  g_assert_nonnull(panel);

  girara_list_t* r1 = girara_list_new_with_free(g_free);
  append_rect(r1, 10.0, 20.0, 100.0, 35.0);

  zathura_annotation_t* a1 = zathura_annotation_new(
      0, ZATHURA_ANNOTATION_COLOR_RED, r1, "First\nquote\twith tab", "Line 1 of comment\n\nLine 2\r\nLine 3\twith tab");
  zathura_annotation_t* a2 = zathura_annotation_new(
      1, ZATHURA_ANNOTATION_COLOR_RED, r1, "Second quote", "Dopamine D2 receptor assay");
  zathura_annotation_t* a3 = zathura_annotation_new(
      2, ZATHURA_ANNOTATION_COLOR_RED, r1, "Resolved quote", "Resolved note about dopamine");
  a3->resolved = true;
  girara_list_free(r1);

  zathura_annotations_add(zathura, a1);
  zathura_annotations_add(zathura, a2);
  zathura_annotations_add(zathura, a3);

  gtk_widget_set_visible(panel, TRUE);
  zathura_annotation_panel_refresh(panel);

  GtkWidget* export_btn = find_widget_by_css_class(panel, "annotation-export-btn");
  g_assert_nonnull(export_btn);
  g_assert_true(gtk_widget_get_sensitive(export_btn));

  /* 1. Default view (all 3 comments): 3 rows exported, newlines/tabs replaced with spaces */
  unsigned int count   = 0;
  g_autofree char* tsv = zathura_annotation_panel_format_export(panel, false, &count);
  g_assert_cmpuint(count, ==, 3);
  g_assert_cmpstr(tsv, ==,
                  "page\tquote\tcomment\n"
                  "1\tFirst quote with tab\tLine 1 of comment Line 2 Line 3 with tab\n"
                  "2\tSecond quote\tDopamine D2 receptor assay\n"
                  "3\tResolved quote\tResolved note about dopamine\n");

  /* 2. Filter active ("dopamine"): exports matching comments */
  zathura_annotation_panel_set_filter_text(panel, "dopamine");
  g_autofree char* filtered_tsv = zathura_annotation_panel_format_export(panel, false, &count);
  g_assert_cmpuint(count, ==, 2);
  g_assert_cmpstr(filtered_tsv, ==,
                  "page\tquote\tcomment\n"
                  "2\tSecond quote\tDopamine D2 receptor assay\n"
                  "3\tResolved quote\tResolved note about dopamine\n");

  /* 3. Export via :comments export <path.tsv> writes the file */
  g_autofree char* tmp_tsv = g_build_filename(g_get_tmp_dir(), "zathura-test-comments-export.tsv", NULL);
  girara_list_t* args_exp  = girara_list_new();
  girara_list_append(args_exp, (void*)"export");
  girara_list_append(args_exp, tmp_tsv);
  g_assert_true(cmd_comments(session, args_exp));
  girara_list_free(args_exp);

  g_autofree char* file_contents = NULL;
  g_assert_true(g_file_get_contents(tmp_tsv, &file_contents, NULL, NULL));
  g_assert_cmpstr(file_contents, ==, filtered_tsv);
  remove(tmp_tsv);

  zathura_free(zathura);
}

static void test_panel_focus_annotation_scrolls_card_to_text_level(void) {
  zathura_t* zathura = create_test_zathura();
  GtkWidget* panel   = zathura->ui.annotation_panel;
  g_assert_nonnull(panel);

  girara_list_t* r1 = girara_list_new_with_free(g_free);
  append_rect(r1, 10.0, 20.0, 100.0, 35.0);

  g_autofree char* id10 = NULL;
  g_autofree char* id12 = NULL;
  for (unsigned int i = 0; i < 12; ++i) {
    g_autofree char* quote = g_strdup_printf("Quote %u", i + 1);
    g_autofree char* comm  = g_strdup_printf("Comment body line 1 for item %u\nSecond line for item %u", i + 1, i + 1);
    zathura_annotation_t* ann =
        zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_RED, r1, quote, comm);
    ann->bounds.y1 = 20.0 + (double)i * 40.0;
    ann->bounds.y2 = 35.0 + (double)i * 40.0;
    zathura_annotations_add(zathura, ann);
    if (i == 9) {
      id10 = g_strdup(ann->id);
    } else if (i == 11) {
      id12 = g_strdup(ann->id);
    }
  }
  girara_list_free(r1);

  gtk_widget_set_visible(panel, TRUE);
  zathura_annotation_panel_refresh(panel);

  GtkAdjustment* vadj = zathura_annotation_panel_get_vadjustment(panel);
  g_assert_nonnull(vadj);
  gtk_adjustment_configure(vadj, 0.0, 0.0, 400.0, 40.0, 200.0, 240.0);
  g_assert_cmpfloat_with_epsilon(gtk_adjustment_get_value(vadj), 0.0, 1e-6);

  /* Focusing a comment below the pane brings it up into the viewport */
  zathura_annotations_set_active(zathura, id10);
  zathura_annotation_panel_focus_annotation(panel, id10);
  g_assert_cmpfloat(gtk_adjustment_get_value(vadj), >, 200.0);

  /* Aligning the last comment (id12) with text at y = 60 px in the viewport */
  const double scroll_at_8 = gtk_adjustment_get_value(vadj);
  g_assert_true(zathura_annotation_panel_scroll_to_annotation(panel, id10, 68.0));
  g_assert_cmpfloat_with_epsilon(gtk_adjustment_get_value(vadj), scroll_at_8 - 60.0, 1e-4);

  /* Even the very last comment (id12) can scroll up to y = 50 px in the viewport */
  g_assert_true(zathura_annotation_panel_scroll_to_annotation(panel, id12, 50.0));
  g_assert_cmpfloat(gtk_adjustment_get_value(vadj), >, scroll_at_8);

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
  g_test_add_func("/annotations/panel/roll_up_all_and_expand_one_by_one",
                  test_panel_roll_up_all_and_expand_one_by_one);
  g_test_add_func("/annotations/panel/mouse_wheel_scroll", test_panel_mouse_wheel_scroll);
  g_test_add_func("/annotations/panel/filter_comments_by_word_and_phrase",
                  test_panel_filter_comments_by_word_and_phrase);
  g_test_add_func("/annotations/panel/export_tsv_respects_filter_and_writes_file",
                  test_panel_export_tsv_respects_filter_and_writes_file);
  g_test_add_func("/annotations/panel/focus_annotation_scrolls_card_to_text_level",
                  test_panel_focus_annotation_scrolls_card_to_text_level);

  return g_test_run();
}
