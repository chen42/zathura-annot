/* SPDX-License-Identifier: Zlib */

#include <glib.h>
#include <math.h>

#include "annotations.h"
#include "zathura.h"

static void append_rect(girara_list_t* list, double x1, double y1, double x2, double y2) {
  zathura_rectangle_t* r = g_malloc(sizeof(zathura_rectangle_t));
  r->x1                  = x1;
  r->y1                  = y1;
  r->x2                  = x2;
  r->y2                  = y2;
  girara_list_append(list, r);
}

static void test_annotation_lifecycle_and_bounds(void) {
  girara_list_t* rects = girara_list_new_with_free(g_free);
  append_rect(rects, 120.0, 100.0, 400.0, 112.0);
  /* Unnormalized coordinates (x1 > x2, y1 > y2) */
  append_rect(rects, 350.0, 128.0, 72.0, 115.0);
  append_rect(rects, 72.0, 130.0, 210.0, 142.0);

  zathura_rectangle_t bounds = zathura_annotation_get_bounds(rects);
  g_assert_cmpfloat_with_epsilon(bounds.x1, 72.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(bounds.y1, 100.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(bounds.x2, 400.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(bounds.y2, 142.0, 1e-6);

  zathura_annotation_t* ann =
      zathura_annotation_new(1, ZATHURA_ANNOTATION_COLOR_YELLOW, rects, "quoted text", "initial comment");
  zathura_annotation_t* ann_no_comment =
      zathura_annotation_new(1, ZATHURA_ANNOTATION_COLOR_RED, rects, "plain highlight", "");
  girara_list_free(rects);

  g_assert_nonnull(ann);
  g_assert_nonnull(ann->id);
  g_assert_cmpuint(ann->page, ==, 1);
  g_assert_cmpint(ann->color, ==, ZATHURA_ANNOTATION_COLOR_RED);
  g_assert_cmpint(ann_no_comment->color, ==, ZATHURA_ANNOTATION_COLOR_YELLOW);
  zathura_annotation_free(ann_no_comment);
  g_assert_cmpstr(ann->quote, ==, "quoted text");
  g_assert_cmpstr(ann->comment, ==, "initial comment");
  g_assert_true(zathura_annotation_has_comment(ann));
  g_assert_cmpuint(girara_list_size(ann->rectangles), ==, 3);
  g_assert_cmpfloat_with_epsilon(ann->bounds.x1, 72.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(ann->bounds.y1, 100.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(ann->bounds.x2, 400.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(ann->bounds.y2, 142.0, 1e-6);

  zathura_annotation_t* copy = zathura_annotation_copy(ann);
  g_assert_nonnull(copy);
  g_assert_cmpstr(copy->id, ==, ann->id);
  g_assert_cmpstr(copy->quote, ==, "quoted text");
  g_assert_cmpstr(copy->comment, ==, "initial comment");
  g_assert_cmpuint(girara_list_size(copy->rectangles), ==, 3);

  /* Free original and verify copy remains valid */
  zathura_annotation_free(ann);
  g_assert_cmpstr(copy->quote, ==, "quoted text");
  g_assert_cmpstr(copy->comment, ==, "initial comment");
  g_assert_cmpfloat_with_epsilon(copy->bounds.x2, 400.0, 1e-6);
  zathura_annotation_free(copy);
}

static void test_annotation_pdf_quad_conversion(void) {
  const double page_height = 792.0;
  zathura_rectangle_t rect = {.x1 = 72.0, .y1 = 100.0, .x2 = 300.0, .y2 = 114.0};

  zathura_annotation_quad_t quad = zathura_annotation_rect_to_pdf_quad(rect, page_height);
  g_assert_cmpfloat_with_epsilon(quad.p1_x, 72.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(quad.p1_y, 692.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(quad.p2_x, 300.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(quad.p2_y, 692.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(quad.p3_x, 72.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(quad.p3_y, 678.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(quad.p4_x, 300.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(quad.p4_y, 678.0, 1e-6);

  zathura_rectangle_t roundtrip = zathura_annotation_pdf_quad_to_rect(quad, page_height);
  g_assert_cmpfloat_with_epsilon(roundtrip.x1, 72.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(roundtrip.y1, 100.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(roundtrip.x2, 300.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(roundtrip.y2, 114.0, 1e-6);

  /* Also verify counterclockwise quad ordering (p3 and p4 swapped) */
  zathura_annotation_quad_t ccw_quad = {
      .p1_x = 72.0,
      .p1_y = 692.0,
      .p2_x = 300.0,
      .p2_y = 692.0,
      .p3_x = 300.0,
      .p3_y = 678.0,
      .p4_x = 72.0,
      .p4_y = 678.0,
  };
  zathura_rectangle_t ccw_rect = zathura_annotation_pdf_quad_to_rect(ccw_quad, page_height);
  g_assert_cmpfloat_with_epsilon(ccw_rect.x1, 72.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(ccw_rect.y1, 100.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(ccw_rect.x2, 300.0, 1e-6);
  g_assert_cmpfloat_with_epsilon(ccw_rect.y2, 114.0, 1e-6);
}

static zathura_annotation_t* make_single_rect_annotation(unsigned int page, double x1, double y1, double x2, double y2,
                                                         const char* quote, const char* comment) {
  girara_list_t* rects = girara_list_new_with_free(g_free);
  append_rect(rects, x1, y1, x2, y2);
  zathura_annotation_t* ann = zathura_annotation_new(page, ZATHURA_ANNOTATION_COLOR_YELLOW, rects, quote, comment);
  girara_list_free(rects);
  return ann;
}

static void test_annotation_sorting_and_page_index(void) {
  zathura_t zathura = {0};
  zathura_annotations_init(&zathura);

  /* Insert 5 annotations out of order across pages 0, 1, and 2 */
  zathura_annotation_t* a_p2 = make_single_rect_annotation(2, 50.0, 80.0, 150.0, 92.0, "p2", "");
  zathura_annotation_t* a_p0_bottom = make_single_rect_annotation(0, 72.0, 300.0, 200.0, 312.0, "p0-bottom", "");
  zathura_annotation_t* a_p1_right  = make_single_rect_annotation(1, 220.0, 100.0, 320.0, 112.0, "p1-right", "");
  zathura_annotation_t* a_p0_top    = make_single_rect_annotation(0, 72.0, 90.0, 200.0, 102.0, "p0-top", "");
  zathura_annotation_t* a_p1_left   = make_single_rect_annotation(1, 80.0, 100.0, 180.0, 112.0, "p1-left", "");

  zathura_annotations_add(&zathura, a_p2);
  zathura_annotations_add(&zathura, a_p0_bottom);
  zathura_annotations_add(&zathura, a_p1_right);
  zathura_annotations_add(&zathura, a_p0_top);
  zathura_annotations_add(&zathura, a_p1_left);

  zathura_annotations_rebuild_index(&zathura, 3);

  g_assert_cmpuint(girara_list_size(zathura.annotations.list), ==, 5);
  g_assert_true(girara_list_nth(zathura.annotations.list, 0) == a_p0_top);
  g_assert_true(girara_list_nth(zathura.annotations.list, 1) == a_p0_bottom);
  g_assert_true(girara_list_nth(zathura.annotations.list, 2) == a_p1_left);
  g_assert_true(girara_list_nth(zathura.annotations.list, 3) == a_p1_right);
  g_assert_true(girara_list_nth(zathura.annotations.list, 4) == a_p2);

  GPtrArray* p0 = zathura_annotations_get_for_page(&zathura, 0);
  GPtrArray* p1 = zathura_annotations_get_for_page(&zathura, 1);
  GPtrArray* p2 = zathura_annotations_get_for_page(&zathura, 2);
  g_assert_nonnull(p0);
  g_assert_nonnull(p1);
  g_assert_nonnull(p2);
  g_assert_cmpuint(p0->len, ==, 2);
  g_assert_cmpuint(p1->len, ==, 2);
  g_assert_cmpuint(p2->len, ==, 1);
  g_assert_true(g_ptr_array_index(p0, 0) == a_p0_top);
  g_assert_true(g_ptr_array_index(p0, 1) == a_p0_bottom);
  g_assert_true(g_ptr_array_index(p1, 0) == a_p1_left);
  g_assert_true(g_ptr_array_index(p1, 1) == a_p1_right);
  g_assert_true(g_ptr_array_index(p2, 0) == a_p2);

  zathura_annotations_free(&zathura);
}

static void test_annotation_hit_testing(void) {
  zathura_t zathura = {0};
  zathura_annotations_init(&zathura);

  girara_list_t* rects = girara_list_new_with_free(g_free);
  append_rect(rects, 100.0, 100.0, 400.0, 112.0);
  append_rect(rects, 72.0, 116.0, 220.0, 128.0);
  zathura_annotation_t* ann = zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_RED, rects, "two lines", "");
  girara_list_free(rects);

  zathura_annotations_add(&zathura, ann);
  zathura_annotations_rebuild_index(&zathura, 2);

  /* Inside line 1 */
  g_assert_true(zathura_annotations_hit_test(&zathura, 0, 250.0, 106.0) == ann);
  /* Inside line 2 */
  g_assert_true(zathura_annotations_hit_test(&zathura, 0, 150.0, 122.0) == ann);
  /* Inside union bounds (72..400, 100..128) but in the empty gap to the right of line 2 */
  g_assert_null(zathura_annotations_hit_test(&zathura, 0, 300.0, 122.0));
  /* On page 1 */
  g_assert_null(zathura_annotations_hit_test(&zathura, 1, 250.0, 106.0));

  zathura_annotations_free(&zathura);
}

static void test_annotation_draft_edit_commit_and_cancel(void) {
  zathura_t zathura = {0};
  zathura_annotations_init(&zathura);

  /* 1. New draft committed with non-empty text keeps annotation and updates comment */
  zathura_annotation_t* ann1 = make_single_rect_annotation(0, 72.0, 100.0, 200.0, 112.0, "quote 1", "");
  zathura_annotations_add(&zathura, ann1);
  g_autofree char* id1 = g_strdup(ann1->id);

  g_assert_true(zathura_annotations_start_edit(&zathura, id1, true));
  g_assert_cmpstr(zathura.annotations.editing_id, ==, id1);
  g_assert_true(zathura.annotations.draft_is_new);
  g_assert_cmpint(ann1->color, ==, ZATHURA_ANNOTATION_COLOR_RED);

  g_assert_true(zathura_annotations_commit_edit(&zathura, id1, "Saved comment"));
  g_assert_null(zathura.annotations.editing_id);
  g_assert_null(zathura.annotations.draft_comment);
  g_assert_false(zathura.annotations.draft_is_new);
  g_assert_nonnull(zathura_annotations_find(&zathura, id1));
  g_assert_cmpstr(ann1->comment, ==, "Saved comment");
  g_assert_cmpint(ann1->color, ==, ZATHURA_ANNOTATION_COLOR_RED);

  /* 2. Existing annotation (draft_is_new == false): cancel_edit preserves annotation and comment */
  g_assert_true(zathura_annotations_start_edit(&zathura, id1, false));
  zathura_annotations_cancel_edit(&zathura);
  g_assert_null(zathura.annotations.editing_id);
  g_assert_nonnull(zathura_annotations_find(&zathura, id1));
  g_assert_cmpstr(ann1->comment, ==, "Saved comment");

  /* 3. New draft (draft_is_new == true): commit_edit with whitespace-only text removes annotation */
  zathura_annotation_t* ann2 = make_single_rect_annotation(0, 72.0, 140.0, 200.0, 152.0, "quote 2", "");
  zathura_annotations_add(&zathura, ann2);
  g_autofree char* id2 = g_strdup(ann2->id);

  g_assert_true(zathura_annotations_start_edit(&zathura, id2, true));
  g_assert_false(zathura_annotations_commit_edit(&zathura, id2, "   \t\n  "));
  g_assert_null(zathura_annotations_find(&zathura, id2));
  g_assert_null(zathura.annotations.editing_id);

  /* 4. New draft (draft_is_new == true): cancel_edit removes annotation */
  zathura_annotation_t* ann3 = make_single_rect_annotation(0, 72.0, 180.0, 200.0, 192.0, "quote 3", "");
  zathura_annotations_add(&zathura, ann3);
  g_autofree char* id3 = g_strdup(ann3->id);

  g_assert_true(zathura_annotations_start_edit(&zathura, id3, true));
  zathura_annotations_cancel_edit(&zathura);
  g_assert_null(zathura_annotations_find(&zathura, id3));
  g_assert_null(zathura.annotations.editing_id);

  zathura_annotations_free(&zathura);
}

int main(int argc, char* argv[]) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/annotations/model/lifecycle_and_bounds", test_annotation_lifecycle_and_bounds);
  g_test_add_func("/annotations/model/pdf_quad_conversion", test_annotation_pdf_quad_conversion);
  g_test_add_func("/annotations/model/sorting_and_page_index", test_annotation_sorting_and_page_index);
  g_test_add_func("/annotations/model/hit_testing", test_annotation_hit_testing);
  g_test_add_func("/annotations/model/draft_edit_commit_and_cancel", test_annotation_draft_edit_commit_and_cancel);
  return g_test_run();
}
