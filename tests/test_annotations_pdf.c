/* SPDX-License-Identifier: Zlib */

#include <cairo-pdf.h>
#include <cairo.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <poppler.h>

#include "annotations-pdf.h"
#include "annotations.h"
#include "zathura.h"

static char* create_sample_two_page_pdf(const char* dir) {
  char* pdf_path         = g_build_filename(dir, "sample.pdf", NULL);
  cairo_surface_t* surf  = cairo_pdf_surface_create(pdf_path, 612.0, 792.0);
  cairo_t* cr            = cairo_create(surf);

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12.0);

  /* Page 0 */
  cairo_move_to(cr, 72.0, 110.0);
  cairo_show_text(cr, "First line of sample text on page one");
  cairo_move_to(cr, 72.0, 128.0);
  cairo_show_text(cr, "Second line of sample text");
  cairo_show_page(cr);

  /* Page 1 */
  cairo_move_to(cr, 72.0, 150.0);
  cairo_show_text(cr, "Important red highlight on page two");
  cairo_show_page(cr);

  cairo_destroy(cr);
  cairo_surface_finish(surf);
  cairo_surface_destroy(surf);

  return pdf_path;
}

static void append_rect(girara_list_t* list, double x1, double y1, double x2, double y2) {
  zathura_rectangle_t* r = g_malloc(sizeof(zathura_rectangle_t));
  r->x1                  = x1;
  r->y1                  = y1;
  r->x2                  = x2;
  r->y2                  = y2;
  girara_list_append(list, r);
}

static void test_pdf_save_and_reload_roundtrip(void) {
  g_autofree char* tmp_dir  = g_dir_make_tmp("zathura-test-pdf-XXXXXX", NULL);
  g_assert_nonnull(tmp_dir);
  g_autofree char* pdf_path = create_sample_two_page_pdf(tmp_dir);

  zathura_t zathura = {0};
  zathura_annotations_init(&zathura);

  girara_list_t* rects0 = girara_list_new_with_free(g_free);
  append_rect(rects0, 72.0, 98.0, 280.0, 114.0);
  append_rect(rects0, 72.0, 116.0, 230.0, 132.0);
  zathura_annotation_t* ann0 =
      zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_YELLOW, rects0, "First line", "Check methods section");
  girara_list_free(rects0);
  zathura_annotations_add(&zathura, ann0);

  girara_list_t* rects1 = girara_list_new_with_free(g_free);
  append_rect(rects1, 72.0, 138.0, 290.0, 154.0);
  zathura_annotation_t* ann1 = zathura_annotation_new(1, ZATHURA_ANNOTATION_COLOR_RED, rects1, "Important red", "");
  girara_list_free(rects1);
  zathura_annotations_add(&zathura, ann1);

  g_assert_true(zathura_annotations_save_pdf(&zathura, pdf_path));

  zathura_annotations_clear(&zathura);
  g_assert_cmpuint(girara_list_size(zathura.annotations.list), ==, 0);

  g_assert_true(zathura_annotations_load_pdf(&zathura, pdf_path, NULL));
  g_assert_cmpuint(girara_list_size(zathura.annotations.list), ==, 2);

  zathura_annotation_t* loaded0 = girara_list_nth(zathura.annotations.list, 0);
  zathura_annotation_t* loaded1 = girara_list_nth(zathura.annotations.list, 1);
  g_assert_nonnull(loaded0);
  g_assert_nonnull(loaded1);

  g_assert_cmpuint(loaded0->page, ==, 0);
  g_assert_cmpint(loaded0->color, ==, ZATHURA_ANNOTATION_COLOR_RED);
  g_assert_cmpuint(girara_list_size(loaded0->rectangles), ==, 2);
  g_assert_cmpstr(loaded0->comment, ==, "Check methods section");
  g_assert_nonnull(loaded0->quote);
  g_assert_true(g_str_has_prefix(loaded0->quote, "First line"));
  g_assert_true(loaded0->baked_in_surface);
  g_assert_false(loaded0->resolved);

  zathura_rectangle_t* r0_0 = girara_list_nth(loaded0->rectangles, 0);
  zathura_rectangle_t* r0_1 = girara_list_nth(loaded0->rectangles, 1);
  g_assert_cmpfloat_with_epsilon(r0_0->x1, 72.0, 1e-2);
  g_assert_cmpfloat(r0_0->y1, >, 98.0);
  g_assert_cmpfloat(r0_0->y2, <, 114.0);
  g_assert_cmpfloat_with_epsilon(r0_1->x1, 72.0, 1e-2);
  g_assert_cmpfloat(r0_1->y1, >, 116.0);
  g_assert_cmpfloat(r0_1->y2, <, 132.0);
  /* Space between lines is excluded from highlight rectangles */
  g_assert_cmpfloat(r0_1->y1 - r0_0->y2, >, 6.0);

  const zathura_rectangle_t tight0 = *r0_0;
  const zathura_rectangle_t tight1 = *r0_1;

  g_assert_cmpuint(loaded1->page, ==, 1);
  g_assert_cmpint(loaded1->color, ==, ZATHURA_ANNOTATION_COLOR_YELLOW);
  g_assert_cmpuint(girara_list_size(loaded1->rectangles), ==, 1);
  g_assert_cmpstr(loaded1->comment, ==, "");
  g_assert_true(loaded1->baked_in_surface);

  /* Second save/reload roundtrip preserves the tightened rectangles */
  g_assert_true(zathura_annotations_save_pdf(&zathura, pdf_path));
  zathura_annotations_clear(&zathura);
  g_assert_true(zathura_annotations_load_pdf(&zathura, pdf_path, NULL));
  loaded0 = girara_list_nth(zathura.annotations.list, 0);
  r0_0    = girara_list_nth(loaded0->rectangles, 0);
  r0_1    = girara_list_nth(loaded0->rectangles, 1);
  g_assert_cmpfloat_with_epsilon(r0_0->x1, tight0.x1, 1e-3);
  g_assert_cmpfloat_with_epsilon(r0_0->y1, tight0.y1, 1e-3);
  g_assert_cmpfloat_with_epsilon(r0_0->x2, tight0.x2, 1e-3);
  g_assert_cmpfloat_with_epsilon(r0_0->y2, tight0.y2, 1e-3);
  g_assert_cmpfloat_with_epsilon(r0_1->x1, tight1.x1, 1e-3);
  g_assert_cmpfloat_with_epsilon(r0_1->y1, tight1.y1, 1e-3);
  g_assert_cmpfloat_with_epsilon(r0_1->x2, tight1.x2, 1e-3);
  g_assert_cmpfloat_with_epsilon(r0_1->y2, tight1.y2, 1e-3);

  zathura_annotations_free(&zathura);
  g_unlink(pdf_path);
  g_rmdir(tmp_dir);
}

static void test_pdf_resolved_state_persistence(void) {
  g_autofree char* tmp_dir  = g_dir_make_tmp("zathura-test-pdf-resolved-XXXXXX", NULL);
  g_assert_nonnull(tmp_dir);
  g_autofree char* pdf_path = create_sample_two_page_pdf(tmp_dir);

  zathura_t zathura = {0};
  zathura_annotations_init(&zathura);

  girara_list_t* rects = girara_list_new_with_free(g_free);
  append_rect(rects, 72.0, 98.0, 280.0, 114.0);
  zathura_annotation_t* ann =
      zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_YELLOW, rects, "First line", "Resolved note");
  girara_list_free(rects);
  ann->resolved = true;
  zathura_annotations_add(&zathura, ann);

  g_assert_true(zathura_annotations_save_pdf(&zathura, pdf_path));

  zathura_annotations_clear(&zathura);
  g_assert_true(zathura_annotations_load_pdf(&zathura, pdf_path, NULL));
  g_assert_cmpuint(girara_list_size(zathura.annotations.list), ==, 1);

  zathura_annotation_t* loaded = girara_list_nth(zathura.annotations.list, 0);
  g_assert_true(loaded->resolved);

  /* Toggle resolved = false, save and reload */
  loaded->resolved = false;
  g_assert_true(zathura_annotations_save_pdf(&zathura, pdf_path));

  zathura_annotations_clear(&zathura);
  g_assert_true(zathura_annotations_load_pdf(&zathura, pdf_path, NULL));
  loaded = girara_list_nth(zathura.annotations.list, 0);
  g_assert_false(loaded->resolved);

  zathura_annotations_free(&zathura);
  g_unlink(pdf_path);
  g_rmdir(tmp_dir);
}

static void test_pdf_edit_recolor_and_delete(void) {
  g_autofree char* tmp_dir  = g_dir_make_tmp("zathura-test-pdf-edit-XXXXXX", NULL);
  g_assert_nonnull(tmp_dir);
  g_autofree char* pdf_path = create_sample_two_page_pdf(tmp_dir);

  zathura_t zathura = {0};
  zathura_annotations_init(&zathura);

  girara_list_t* rects0 = girara_list_new_with_free(g_free);
  append_rect(rects0, 72.0, 98.0, 250.0, 114.0);
  zathura_annotation_t* ann0 = zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_YELLOW, rects0, "Quote 1", "Initial");
  girara_list_free(rects0);
  zathura_annotations_add(&zathura, ann0);

  girara_list_t* rects1 = girara_list_new_with_free(g_free);
  append_rect(rects1, 72.0, 116.0, 250.0, 132.0);
  zathura_annotation_t* ann1 =
      zathura_annotation_new(0, ZATHURA_ANNOTATION_COLOR_YELLOW, rects1, "Quote 2", "To delete");
  girara_list_free(rects1);
  zathura_annotations_add(&zathura, ann1);

  g_assert_true(zathura_annotations_save_pdf(&zathura, pdf_path));

  /* Modify ann0 and remove ann1 */
  g_free(ann0->comment);
  ann0->comment = g_strdup("Updated note");
  ann0->color   = ZATHURA_ANNOTATION_COLOR_RED;
  g_assert_true(zathura_annotations_remove(&zathura, ann1->id));

  g_assert_true(zathura_annotations_save_pdf(&zathura, pdf_path));

  zathura_annotations_clear(&zathura);
  g_assert_true(zathura_annotations_load_pdf(&zathura, pdf_path, NULL));
  g_assert_cmpuint(girara_list_size(zathura.annotations.list), ==, 1);

  zathura_annotation_t* remaining = girara_list_nth(zathura.annotations.list, 0);
  g_assert_cmpint(remaining->color, ==, ZATHURA_ANNOTATION_COLOR_RED);
  g_assert_cmpstr(remaining->comment, ==, "Updated note");

  zathura_annotations_free(&zathura);
  g_unlink(pdf_path);
  g_rmdir(tmp_dir);
}

static void test_pdf_external_highlight_import(void) {
  g_autofree char* tmp_dir  = g_dir_make_tmp("zathura-test-pdf-ext-XXXXXX", NULL);
  g_assert_nonnull(tmp_dir);
  g_autofree char* pdf_path = create_sample_two_page_pdf(tmp_dir);
  g_autofree char* saved_pdf = g_build_filename(tmp_dir, "external.pdf", NULL);

  g_autofree gchar* uri  = g_filename_to_uri(pdf_path, NULL, NULL);
  PopplerDocument* doc   = poppler_document_new_from_file(uri, NULL, NULL);
  g_assert_nonnull(doc);
  PopplerPage* page = poppler_document_get_page(doc, 0);
  g_assert_nonnull(page);

  GArray* quads               = g_array_new(FALSE, FALSE, sizeof(PopplerQuadrilateral));
  PopplerQuadrilateral q      = {.p1 = {72.0, 694.0}, .p2 = {240.0, 694.0}, .p3 = {72.0, 678.0}, .p4 = {240.0, 678.0}};
  g_array_append_val(quads, q);
  PopplerRectangle rect       = {72.0, 678.0, 240.0, 694.0};
  PopplerAnnot* ext_annot     = poppler_annot_text_markup_new_highlight(doc, &rect, quads);
  g_array_unref(quads);

  /* Custom green RGB (0.2, 0.9, 0.3) from an external viewer */
  PopplerColor ext_color = {.red = (guint16)(0.2 * 65535), .green = (guint16)(0.9 * 65535), .blue = (guint16)(0.3 * 65535)};
  poppler_annot_set_color(ext_annot, &ext_color);
  poppler_annot_set_contents(ext_annot, "External viewer note");
  poppler_page_add_annot(page, ext_annot);
  g_object_unref(ext_annot);
  g_object_unref(page);

  g_autofree gchar* saved_uri = g_filename_to_uri(saved_pdf, NULL, NULL);
  g_assert_true(poppler_document_save(doc, saved_uri, NULL));
  g_object_unref(doc);

  zathura_t zathura = {0};
  zathura_annotations_init(&zathura);
  g_assert_true(zathura_annotations_load_pdf(&zathura, saved_pdf, NULL));
  g_assert_cmpuint(girara_list_size(zathura.annotations.list), ==, 1);

  zathura_annotation_t* imported = girara_list_nth(zathura.annotations.list, 0);
  g_assert_cmpint(imported->color, ==, ZATHURA_ANNOTATION_COLOR_RED);
  g_assert_cmpstr(imported->comment, ==, "External viewer note");

  zathura_annotations_free(&zathura);
  g_unlink(saved_pdf);
  g_unlink(pdf_path);
  g_rmdir(tmp_dir);
}

int main(int argc, char* argv[]) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/annotations/pdf/save_and_reload_roundtrip", test_pdf_save_and_reload_roundtrip);
  g_test_add_func("/annotations/pdf/resolved_state_persistence", test_pdf_resolved_state_persistence);
  g_test_add_func("/annotations/pdf/edit_recolor_and_delete", test_pdf_edit_recolor_and_delete);
  g_test_add_func("/annotations/pdf/external_highlight_import", test_pdf_external_highlight_import);
  return g_test_run();
}
