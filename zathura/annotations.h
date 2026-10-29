/* SPDX-License-Identifier: Zlib */
/* Part of zathura-annot (modified fork of pwmt/zathura) */

#ifndef ANNOTATIONS_H
#define ANNOTATIONS_H

#include <stdbool.h>
#include <glib.h>
#include <girara/datastructures.h>

#include "types.h"

/**
 * Highlight color categories
 */
typedef enum zathura_annotation_color_e {
  ZATHURA_ANNOTATION_COLOR_YELLOW = 0,
  ZATHURA_ANNOTATION_COLOR_RED    = 1,
} zathura_annotation_color_t;

/**
 * PDF QuadPoints representation for a single highlighted line
 */
typedef struct zathura_annotation_quad_s {
  double p1_x, p1_y;
  double p2_x, p2_y;
  double p3_x, p3_y;
  double p4_x, p4_y;
} zathura_annotation_quad_t;

/**
 * Annotation data structure
 */
typedef struct zathura_annotation_s {
  char* id;                         /**< Unique annotation ID (Poppler /NM or generated ID) */
  unsigned int page;                /**< 0-based page index */
  zathura_annotation_color_t color; /**< YELLOW or RED */
  zathura_rectangle_t bounds;       /**< Normalized union bounding box in unscaled page points */
  girara_list_t* rectangles;        /**< Owned zathura_rectangle_t* per highlighted line */
  char* quote;                      /**< Captured selected text */
  char* comment;                    /**< Plain-text comment (empty string for plain highlight) */
  bool resolved;                    /**< Resolved state (hides card in panel, keeps highlight visible) */
  bool baked_in_surface;            /**< True if already rendered into page's Cairo surface by poppler */
  gint64 created_at;                /**< Unix epoch microseconds */
  gint64 updated_at;                /**< Unix epoch microseconds */
} zathura_annotation_t;

/* Lifecycle and geometry */
zathura_annotation_t* zathura_annotation_new(unsigned int page, zathura_annotation_color_t color,
                                             girara_list_t* rectangles, const char* quote, const char* comment);
zathura_annotation_t* zathura_annotation_copy(const zathura_annotation_t* annotation);
void zathura_annotation_free(void* data);
girara_list_t* zathura_annotation_rectangles_copy(girara_list_t* rectangles);
zathura_rectangle_t zathura_annotation_get_bounds(girara_list_t* rectangles);
int zathura_annotation_compare(const void* a, const void* b);
bool zathura_annotation_has_comment(const zathura_annotation_t* annotation);

/* Coordinate conversion between zathura_rectangle_t (top-left origin) and PDF QuadPoints (bottom-left origin) */
zathura_annotation_quad_t zathura_annotation_rect_to_pdf_quad(zathura_rectangle_t rect, double page_height);
zathura_rectangle_t zathura_annotation_pdf_quad_to_rect(zathura_annotation_quad_t quad, double page_height);

/* Store operations on zathura_t */
void zathura_annotations_init(zathura_t* zathura);
void zathura_annotations_clear(zathura_t* zathura);
void zathura_annotations_free(zathura_t* zathura);
void zathura_annotations_rebuild_index(zathura_t* zathura, unsigned int page_count);
zathura_annotation_t* zathura_annotations_add(zathura_t* zathura, zathura_annotation_t* annotation);
zathura_annotation_t* zathura_annotations_find(zathura_t* zathura, const char* id);
bool zathura_annotations_remove(zathura_t* zathura, const char* id);
GPtrArray* zathura_annotations_get_for_page(zathura_t* zathura, unsigned int page);
zathura_annotation_t* zathura_annotations_hit_test(zathura_t* zathura, unsigned int page, double page_x, double page_y);

/* Edit/draft workflow */
bool zathura_annotations_start_edit(zathura_t* zathura, const char* id, bool draft_is_new);
bool zathura_annotations_commit_edit(zathura_t* zathura, const char* id, const char* text);
void zathura_annotations_cancel_edit(zathura_t* zathura);
void zathura_annotations_set_active(zathura_t* zathura, const char* id);
void zathura_annotations_reveal(zathura_t* zathura, const zathura_annotation_t* annotation);
void zathura_annotations_mark_dirty(zathura_t* zathura);
void zathura_annotations_queue_draw_page(zathura_t* zathura, unsigned int page);
void zathura_annotations_queue_draw_all(zathura_t* zathura);

#endif // ANNOTATIONS_H
