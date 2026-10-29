/* SPDX-License-Identifier: Zlib */

#include <cairo.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "annotations.h"
#include "page-widget.h"
#include "page.h"
#include "zathura.h"

static void append_rect(girara_list_t* list, double x1, double y1, double x2, double y2) {
  zathura_rectangle_t* r = g_malloc(sizeof(zathura_rectangle_t));
  r->x1                  = x1;
  r->y1                  = y1;
  r->x2                  = x2;
  r->y2                  = y2;
  girara_list_append(list, r);
}

static void get_pixel_rgb(cairo_surface_t* surface, int x, int y, guint8* r, guint8* g, guint8* b) {
  cairo_surface_flush(surface);
  const int stride     = cairo_image_surface_get_stride(surface);
  const guchar* data   = cairo_image_surface_get_data(surface);
  const guint32* pixel = (const guint32*)(data + y * stride + x * 4);
  guint32 val          = *pixel;
  *r                   = (val >> 16) & 0xFF;
  *g                   = (val >> 8) & 0xFF;
  *b                   = val & 0xFF;
}

static void paint_surface_white(cairo_t* cr, int width, int height) {
  cairo_save(cr);
  cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);
  cairo_restore(cr);
}

static zathura_page_t* create_dummy_page(unsigned int index) {
  return zathura_page_new((zathura_document_t*)0x1, index, NULL);
}

static void free_dummy_page(zathura_page_t* page) {
  g_free(page);
}

static void test_page_selection_snapshot_and_create_actions(void) {
  zathura_t zathura = {0};
  zathura_annotations_init(&zathura);

  zathura_page_t* page   = create_dummy_page(0);
  GtkWidget* page_widget = zathura_page_widget_new(&zathura, page);
  g_object_ref_sink(page_widget);

  girara_list_t* rects = girara_list_new_with_free(g_free);
  append_rect(rects, 40.0, 50.0, 200.0, 64.0);
  zathura_page_widget_set_selection_snapshot(ZATHURA_PAGE_WIDGET(page_widget), rects, "Selected passage");
  girara_list_free(rects);

  g_assert_true(zathura_page_widget_has_selection_snapshot(ZATHURA_PAGE_WIDGET(page_widget)));

  /* Activate "yellow" highlight action */
  g_assert_true(gtk_widget_activate_action(page_widget, "annotation.yellow", NULL));

  g_assert_false(zathura_page_widget_has_selection_snapshot(ZATHURA_PAGE_WIDGET(page_widget)));
  g_assert_cmpuint(girara_list_size(zathura.annotations.list), ==, 1);

  zathura_annotation_t* ann = girara_list_nth(zathura.annotations.list, 0);
  g_assert_nonnull(ann);
  g_assert_cmpint(ann->color, ==, ZATHURA_ANNOTATION_COLOR_YELLOW);
  g_assert_cmpstr(ann->quote, ==, "Selected passage");
  g_assert_cmpstr(ann->comment, ==, "");
  g_assert_null(zathura.annotations.editing_id);

  /* Set another selection snapshot and activate "comment" action */
  rects = girara_list_new_with_free(g_free);
  append_rect(rects, 40.0, 80.0, 200.0, 94.0);
  zathura_page_widget_set_selection_snapshot(ZATHURA_PAGE_WIDGET(page_widget), rects, "Commented passage");
  girara_list_free(rects);

  g_assert_true(gtk_widget_activate_action(page_widget, "annotation.comment", NULL));
  g_assert_cmpuint(girara_list_size(zathura.annotations.list), ==, 2);

  zathura_annotation_t* ann2 = girara_list_nth(zathura.annotations.list, 1);
  g_assert_nonnull(ann2);
  g_assert_cmpstr(zathura.annotations.editing_id, ==, ann2->id);
  g_assert_true(zathura.annotations.draft_is_new);

  g_object_unref(page_widget);
  free_dummy_page(page);
  zathura_annotations_free(&zathura);
}

static void test_page_draw_highlight_and_comment_badge_visibility(void) {
  zathura_t zathura = {0};
  zathura_annotations_init(&zathura);

  zathura_page_t* page   = create_dummy_page(0);
  GtkWidget* page_widget = zathura_page_widget_new(&zathura, page);
  g_object_ref_sink(page_widget);

  girara_list_t* rects = girara_list_new_with_free(g_free);
  append_rect(rects, 20.0, 20.0, 120.0, 40.0);
  zathura_annotation_t* ann = zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_YELLOW, rects, "Quote", "Note");
  girara_list_free(rects);
  ann->baked_in_surface = false;
  zathura_annotations_add(&zathura, ann);

  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 300, 200);
  cairo_t* cr           = cairo_create(surf);

  /* 1. Comment panel hidden (comment_panel_visible == false), annotation unselected */
  paint_surface_white(cr, 300, 200);
  zathura_page_widget_draw_annotations(ZATHURA_PAGE_WIDGET(page_widget), cr, 1.0, false);

  guint8 r_center = 0, g_center = 0, b_center = 0;
  guint8 r_outside_hidden = 0, g_outside_hidden = 0, b_outside_hidden = 0;
  get_pixel_rgb(surf, 70, 30, &r_center, &g_center, &b_center);
  get_pixel_rgb(surf, 124, 20, &r_outside_hidden, &g_outside_hidden, &b_outside_hidden);

  /* Highlight background is visible (warm muted yellow: R > 230, B < 215) */
  g_assert_cmpuint(r_center, >, 230);
  g_assert_cmpuint(b_center, <, 215);
  /* When panel is hidden, outside badge area (x=124, y=20) remains pure white */
  g_assert_cmpuint(r_outside_hidden, ==, 255);
  g_assert_cmpuint(g_outside_hidden, ==, 255);
  g_assert_cmpuint(b_outside_hidden, ==, 255);

  /* 2. Comment panel visible (comment_panel_visible == true): badge drawn outside at [122..127, 18..23] */
  paint_surface_white(cr, 300, 200);
  zathura_page_widget_draw_annotations(ZATHURA_PAGE_WIDGET(page_widget), cr, 1.0, true);

  guint8 r_inside_corner = 0, g_inside_corner = 0, b_inside_corner = 0;
  guint8 r_outside_vis = 0, g_outside_vis = 0, b_outside_vis = 0;
  get_pixel_rgb(surf, 116, 24, &r_inside_corner, &g_inside_corner, &b_inside_corner);
  get_pixel_rgb(surf, 124, 20, &r_outside_vis, &g_outside_vis, &b_outside_vis);

  /* Inside top-right corner of highlight box is unobstructed by badge (same as center) */
  g_assert_cmpuint(b_inside_corner, ==, b_center);
  /* Outside badge area now has the indicator box (much lower blue channel than white) */
  g_assert_cmpuint(b_outside_vis, <, 100);

  /* 3. Selecting the annotation makes both highlight fill and indicator box brighter */
  zathura_annotations_set_active(&zathura, ann->id);
  paint_surface_white(cr, 300, 200);
  zathura_page_widget_draw_annotations(ZATHURA_PAGE_WIDGET(page_widget), cr, 1.0, true);

  guint8 r_active = 0, g_active = 0, b_active = 0;
  guint8 r_badge_active = 0, g_badge_active = 0, b_badge_active = 0;
  get_pixel_rgb(surf, 70, 30, &r_active, &g_active, &b_active);
  get_pixel_rgb(surf, 124, 20, &r_badge_active, &g_badge_active, &b_badge_active);

  g_assert_cmpuint(r_active, >=, r_center);
  g_assert_cmpuint(g_active, >, g_center);
  g_assert_cmpuint(b_active, <, b_center);
  g_assert_cmpuint(r_badge_active, >, r_outside_vis);
  g_assert_cmpuint(g_badge_active, >, g_outside_vis);

  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  g_object_unref(page_widget);
  free_dummy_page(page);
  zathura_annotations_free(&zathura);
}

static void test_page_draw_skips_baked_in_fill_but_draws_active_border(void) {
  zathura_t zathura = {0};
  zathura_annotations_init(&zathura);

  zathura_page_t* page   = create_dummy_page(0);
  GtkWidget* page_widget = zathura_page_widget_new(&zathura, page);
  g_object_ref_sink(page_widget);

  girara_list_t* rects = girara_list_new_with_free(g_free);
  append_rect(rects, 20.0, 20.0, 120.0, 40.0);
  zathura_annotation_t* ann = zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_YELLOW, rects, "Quote", "");
  girara_list_free(rects);
  ann->baked_in_surface = true;
  zathura_annotations_add(&zathura, ann);
  zathura_annotations_set_active(&zathura, ann->id);

  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 300, 200);
  cairo_t* cr           = cairo_create(surf);
  paint_surface_white(cr, 300, 200);

  zathura_page_widget_draw_annotations(ZATHURA_PAGE_WIDGET(page_widget), cr, 1.0, false);

  guint8 r_in = 0, g_in = 0, b_in = 0;
  guint8 r_border = 0, g_border = 0, b_border = 0;
  get_pixel_rgb(surf, 70, 30, &r_in, &g_in, &b_in);
  get_pixel_rgb(surf, 18, 30, &r_border, &g_border, &b_border);

  /* Interior remains pure white (no double-darkening) */
  g_assert_cmpuint(r_in, ==, 255);
  g_assert_cmpuint(g_in, ==, 255);
  g_assert_cmpuint(b_in, ==, 255);

  /* Outside border at x=18 (sr.x1 - 1.5), y=30 is stroked with bright active outline */
  g_assert_cmpuint(r_border, >, 240);
  g_assert_cmpuint(b_border, <, 100);

  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  g_object_unref(page_widget);
  free_dummy_page(page);
  zathura_annotations_free(&zathura);
}

static void test_page_context_annotation_actions(void) {
  zathura_t zathura = {0};
  zathura_annotations_init(&zathura);

  zathura_page_t* page   = create_dummy_page(0);
  GtkWidget* page_widget = zathura_page_widget_new(&zathura, page);
  g_object_ref_sink(page_widget);

  girara_list_t* rects = girara_list_new_with_free(g_free);
  append_rect(rects, 20.0, 20.0, 120.0, 40.0);
  zathura_annotation_t* ann = zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_YELLOW, rects, "Quote", "Comment");
  girara_list_free(rects);
  zathura_annotations_add(&zathura, ann);
  zathura_annotations_set_active(&zathura, ann->id);

  g_assert_true(gtk_widget_activate_action(page_widget, "annotation.recolor-red", NULL));
  g_assert_cmpint(ann->color, ==, ZATHURA_ANNOTATION_COLOR_RED);

  g_assert_true(gtk_widget_activate_action(page_widget, "annotation.toggle-resolved", NULL));
  g_assert_true(ann->resolved);

  g_assert_true(gtk_widget_activate_action(page_widget, "annotation.delete", NULL));
  g_assert_cmpuint(girara_list_size(zathura.annotations.list), ==, 0);

  /* Also test removing a color-only highlight via text selection overlap ("annotation.remove-selected") */
  rects = girara_list_new_with_free(g_free);
  append_rect(rects, 30.0, 50.0, 150.0, 68.0);
  zathura_annotation_t* hl_only = zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_YELLOW, rects, "Highlighted text", "");
  girara_list_free(rects);
  zathura_annotations_add(&zathura, hl_only);
  g_assert_cmpuint(girara_list_size(zathura.annotations.list), ==, 1);

  girara_list_t* sel_rects = girara_list_new_with_free(g_free);
  append_rect(sel_rects, 40.0, 52.0, 120.0, 66.0);
  zathura_page_widget_set_selection_snapshot(ZATHURA_PAGE_WIDGET(page_widget), sel_rects, "Highlighted");
  girara_list_free(sel_rects);

  g_assert_true(gtk_widget_activate_action(page_widget, "annotation.remove-selected", NULL));
  g_assert_cmpuint(girara_list_size(zathura.annotations.list), ==, 0);

  g_object_unref(page_widget);
  free_dummy_page(page);
  zathura_annotations_free(&zathura);
}

int main(int argc, char* argv[]) {
  gtk_init();
  (void)gtk_settings_get_default();
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/annotations/page/selection_snapshot_and_create_actions",
                  test_page_selection_snapshot_and_create_actions);
  g_test_add_func("/annotations/page/draw_highlight_and_comment_badge_visibility",
                  test_page_draw_highlight_and_comment_badge_visibility);
  g_test_add_func("/annotations/page/draw_skips_baked_in_fill_but_draws_active_border",
                  test_page_draw_skips_baked_in_fill_but_draws_active_border);
  g_test_add_func("/annotations/page/context_annotation_actions", test_page_context_annotation_actions);
  return g_test_run();
}
