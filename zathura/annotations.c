/* SPDX-License-Identifier: Zlib */
/* Part of zathura-annot (modified fork of pwmt/zathura) */

#include "annotations.h"

#include <math.h>
#include <string.h>

#include "adjustment.h"
#include "annotation-panel.h"
#include "annotations-pdf.h"
#include "document.h"
#include "page-widget.h"
#include "page.h"
#include "utils.h"
#include "zathura.h"

static zathura_rectangle_t normalize_rectangle(zathura_rectangle_t rect) {
  zathura_rectangle_t norm = {
      .x1 = MIN(rect.x1, rect.x2),
      .y1 = MIN(rect.y1, rect.y2),
      .x2 = MAX(rect.x1, rect.x2),
      .y2 = MAX(rect.y1, rect.y2),
  };
  return norm;
}

static bool string_is_blank(const char* str) {
  if (str == NULL) {
    return true;
  }
  for (const char* p = str; *p != '\0'; ++p) {
    if (!g_ascii_isspace((guchar)*p)) {
      return false;
    }
  }
  return true;
}

girara_list_t* zathura_annotation_rectangles_copy(girara_list_t* rectangles) {
  girara_list_t* copy = girara_list_new_with_free(g_free);
  if (copy == NULL || rectangles == NULL) {
    return copy;
  }

  for (size_t idx = 0; idx != girara_list_size(rectangles); ++idx) {
    zathura_rectangle_t* rect = girara_list_nth(rectangles, idx);
    if (rect != NULL) {
      zathura_rectangle_t* dup = g_malloc(sizeof(zathura_rectangle_t));
      *dup                     = normalize_rectangle(*rect);
      girara_list_append(copy, dup);
    }
  }

  return copy;
}

zathura_rectangle_t zathura_annotation_get_bounds(girara_list_t* rectangles) {
  zathura_rectangle_t bounds = {0.0, 0.0, 0.0, 0.0};
  if (rectangles == NULL || girara_list_size(rectangles) == 0) {
    return bounds;
  }

  bool first = true;
  for (size_t idx = 0; idx != girara_list_size(rectangles); ++idx) {
    zathura_rectangle_t* rect = girara_list_nth(rectangles, idx);
    if (rect == NULL) {
      continue;
    }
    zathura_rectangle_t norm = normalize_rectangle(*rect);
    if (first) {
      bounds = norm;
      first  = false;
    } else {
      bounds.x1 = MIN(bounds.x1, norm.x1);
      bounds.y1 = MIN(bounds.y1, norm.y1);
      bounds.x2 = MAX(bounds.x2, norm.x2);
      bounds.y2 = MAX(bounds.y2, norm.y2);
    }
  }

  return bounds;
}

zathura_annotation_t* zathura_annotation_new(unsigned int page, zathura_annotation_color_t color,
                                             girara_list_t* rectangles, const char* quote, const char* comment) {
  (void)color;
  zathura_annotation_t* annotation = g_try_malloc0(sizeof(zathura_annotation_t));
  if (annotation == NULL) {
    return NULL;
  }

  annotation->id               = g_uuid_string_random();
  annotation->page             = page;
  annotation->color            = string_is_blank(comment) ? ZATHURA_ANNOTATION_COLOR_YELLOW
                                                          : ZATHURA_ANNOTATION_COLOR_RED;
  annotation->rectangles       = zathura_annotation_rectangles_copy(rectangles);
  annotation->bounds           = zathura_annotation_get_bounds(annotation->rectangles);
  annotation->quote            = g_strdup(quote != NULL ? quote : "");
  annotation->comment          = g_strdup(comment != NULL ? comment : "");
  annotation->resolved         = false;
  annotation->baked_in_surface = false;
  annotation->created_at       = g_get_real_time();
  annotation->updated_at       = annotation->created_at;

  return annotation;
}

zathura_annotation_t* zathura_annotation_copy(const zathura_annotation_t* annotation) {
  if (annotation == NULL) {
    return NULL;
  }

  zathura_annotation_t* copy = g_try_malloc0(sizeof(zathura_annotation_t));
  if (copy == NULL) {
    return NULL;
  }

  copy->id               = g_strdup(annotation->id);
  copy->page             = annotation->page;
  copy->color            = annotation->color;
  copy->rectangles       = zathura_annotation_rectangles_copy(annotation->rectangles);
  copy->bounds           = normalize_rectangle(annotation->bounds);
  copy->quote            = g_strdup(annotation->quote != NULL ? annotation->quote : "");
  copy->comment          = g_strdup(annotation->comment != NULL ? annotation->comment : "");
  copy->resolved         = annotation->resolved;
  copy->baked_in_surface = annotation->baked_in_surface;
  copy->created_at       = annotation->created_at;
  copy->updated_at       = annotation->updated_at;

  return copy;
}

void zathura_annotation_free(void* data) {
  if (data == NULL) {
    return;
  }

  zathura_annotation_t* annotation = data;
  g_free(annotation->id);
  g_free(annotation->quote);
  g_free(annotation->comment);
  if (annotation->rectangles != NULL) {
    girara_list_free(annotation->rectangles);
  }
  g_free(annotation);
}

bool zathura_annotation_has_comment(const zathura_annotation_t* annotation) {
  if (annotation == NULL) {
    return false;
  }
  return !string_is_blank(annotation->comment);
}

int zathura_annotation_compare(const void* a, const void* b) {
  const zathura_annotation_t* ann_a = a;
  const zathura_annotation_t* ann_b = b;

  if (ann_a == NULL && ann_b == NULL) {
    return 0;
  }
  if (ann_a == NULL) {
    return -1;
  }
  if (ann_b == NULL) {
    return 1;
  }

  if (ann_a->page != ann_b->page) {
    return (ann_a->page < ann_b->page) ? -1 : 1;
  }
  if (fabs(ann_a->bounds.y1 - ann_b->bounds.y1) > 1e-6) {
    return (ann_a->bounds.y1 < ann_b->bounds.y1) ? -1 : 1;
  }
  if (fabs(ann_a->bounds.x1 - ann_b->bounds.x1) > 1e-6) {
    return (ann_a->bounds.x1 < ann_b->bounds.x1) ? -1 : 1;
  }
  if (ann_a->created_at != ann_b->created_at) {
    return (ann_a->created_at < ann_b->created_at) ? -1 : 1;
  }
  return g_strcmp0(ann_a->id, ann_b->id);
}

zathura_annotation_quad_t zathura_annotation_rect_to_pdf_quad(zathura_rectangle_t rect, double page_height) {
  zathura_rectangle_t norm       = normalize_rectangle(rect);
  double y_top                   = page_height - norm.y1;
  double y_bottom                = page_height - norm.y2;
  zathura_annotation_quad_t quad = {
      .p1_x = norm.x1,
      .p1_y = y_top,
      .p2_x = norm.x2,
      .p2_y = y_top,
      .p3_x = norm.x1,
      .p3_y = y_bottom,
      .p4_x = norm.x2,
      .p4_y = y_bottom,
  };
  return quad;
}

zathura_rectangle_t zathura_annotation_pdf_quad_to_rect(zathura_annotation_quad_t quad, double page_height) {
  double min_x     = MIN(MIN(quad.p1_x, quad.p2_x), MIN(quad.p3_x, quad.p4_x));
  double max_x     = MAX(MAX(quad.p1_x, quad.p2_x), MAX(quad.p3_x, quad.p4_x));
  double min_pdf_y = MIN(MIN(quad.p1_y, quad.p2_y), MIN(quad.p3_y, quad.p4_y));
  double max_pdf_y = MAX(MAX(quad.p1_y, quad.p2_y), MAX(quad.p3_y, quad.p4_y));

  zathura_rectangle_t rect = {
      .x1 = min_x,
      .y1 = page_height - max_pdf_y,
      .x2 = max_x,
      .y2 = page_height - min_pdf_y,
  };
  return normalize_rectangle(rect);
}

void zathura_annotations_init(zathura_t* zathura) {
  if (zathura == NULL) {
    return;
  }

  if (zathura->annotations.list == NULL) {
    zathura->annotations.list = girara_list_new_with_free(zathura_annotation_free);
  }
  if (zathura->annotations.by_page == NULL) {
    zathura->annotations.by_page = g_ptr_array_new_with_free_func((GDestroyNotify)g_ptr_array_unref);
  }
  zathura->annotations.active_id          = NULL;
  zathura->annotations.editing_id         = NULL;
  zathura->annotations.draft_comment      = NULL;
  zathura->annotations.draft_is_new       = false;
  zathura->annotations.dirty              = false;
  zathura->annotations.autosave_source_id = 0;
  zathura->annotations.next_id_counter    = 0;
}

void zathura_annotations_clear(zathura_t* zathura) {
  if (zathura == NULL) {
    return;
  }

  if (zathura->annotations.autosave_source_id > 0) {
    g_source_remove(zathura->annotations.autosave_source_id);
    zathura->annotations.autosave_source_id = 0;
  }

  if (zathura->annotations.list != NULL) {
    girara_list_clear(zathura->annotations.list);
  }
  if (zathura->annotations.by_page != NULL) {
    g_ptr_array_set_size(zathura->annotations.by_page, 0);
  }

  g_clear_pointer(&zathura->annotations.active_id, g_free);
  g_clear_pointer(&zathura->annotations.editing_id, g_free);
  g_clear_pointer(&zathura->annotations.draft_comment, g_free);
  zathura->annotations.draft_is_new = false;
  zathura->annotations.dirty        = false;
}

void zathura_annotations_free(zathura_t* zathura) {
  if (zathura == NULL) {
    return;
  }

  zathura_annotations_clear(zathura);

  if (zathura->annotations.list != NULL) {
    girara_list_free(zathura->annotations.list);
    zathura->annotations.list = NULL;
  }
  if (zathura->annotations.by_page != NULL) {
    g_ptr_array_free(zathura->annotations.by_page, TRUE);
    zathura->annotations.by_page = NULL;
  }
}

void zathura_annotations_rebuild_index(zathura_t* zathura, unsigned int page_count) {
  if (zathura == NULL) {
    return;
  }

  if (zathura->annotations.list == NULL) {
    zathura->annotations.list = girara_list_new_with_free(zathura_annotation_free);
  }
  if (zathura->annotations.by_page == NULL) {
    zathura->annotations.by_page = g_ptr_array_new_with_free_func((GDestroyNotify)g_ptr_array_unref);
  }

  girara_list_sort(zathura->annotations.list, zathura_annotation_compare);

  unsigned int max_pages = page_count;
  if (zathura->document != NULL) {
    unsigned int doc_pages = zathura_document_get_number_of_pages(zathura->document);
    if (doc_pages > max_pages) {
      max_pages = doc_pages;
    }
  }

  for (size_t idx = 0; idx != girara_list_size(zathura->annotations.list); ++idx) {
    zathura_annotation_t* ann = girara_list_nth(zathura->annotations.list, idx);
    if (ann != NULL && ann->page + 1 > max_pages) {
      max_pages = ann->page + 1;
    }
  }

  g_ptr_array_set_size(zathura->annotations.by_page, 0);
  for (unsigned int i = 0; i < max_pages; ++i) {
    g_ptr_array_add(zathura->annotations.by_page, g_ptr_array_new());
  }

  for (size_t idx = 0; idx != girara_list_size(zathura->annotations.list); ++idx) {
    zathura_annotation_t* ann = girara_list_nth(zathura->annotations.list, idx);
    if (ann != NULL && ann->page < zathura->annotations.by_page->len) {
      GPtrArray* page_array = g_ptr_array_index(zathura->annotations.by_page, ann->page);
      g_ptr_array_add(page_array, ann);
    }
  }
}

zathura_annotation_t* zathura_annotations_add(zathura_t* zathura, zathura_annotation_t* annotation) {
  if (zathura == NULL || annotation == NULL) {
    return NULL;
  }

  if (zathura->annotations.list == NULL || zathura->annotations.by_page == NULL) {
    zathura_annotations_init(zathura);
  }

  ++zathura->annotations.next_id_counter;
  if (annotation->id == NULL || annotation->id[0] == '\0') {
    g_free(annotation->id);
    annotation->id = g_strdup_printf("ann-%" G_GUINT64_FORMAT, zathura->annotations.next_id_counter);
  }

  if (annotation->rectangles != NULL && girara_list_size(annotation->rectangles) > 0) {
    annotation->bounds = zathura_annotation_get_bounds(annotation->rectangles);
  } else {
    annotation->bounds = normalize_rectangle(annotation->bounds);
  }

  girara_list_append(zathura->annotations.list, annotation);
  zathura_annotations_rebuild_index(zathura, 0);
  return annotation;
}

zathura_annotation_t* zathura_annotations_find(zathura_t* zathura, const char* id) {
  if (zathura == NULL || zathura->annotations.list == NULL || id == NULL) {
    return NULL;
  }

  for (size_t idx = 0; idx != girara_list_size(zathura->annotations.list); ++idx) {
    zathura_annotation_t* ann = girara_list_nth(zathura->annotations.list, idx);
    if (ann != NULL && g_strcmp0(ann->id, id) == 0) {
      return ann;
    }
  }

  return NULL;
}

bool zathura_annotations_remove(zathura_t* zathura, const char* id) {
  if (zathura == NULL || zathura->annotations.list == NULL || id == NULL) {
    return false;
  }

  g_autofree char* id_copy     = g_strdup(id);
  zathura_annotation_t* target = zathura_annotations_find(zathura, id_copy);
  if (target == NULL) {
    return false;
  }

  if (g_strcmp0(zathura->annotations.active_id, id_copy) == 0) {
    g_clear_pointer(&zathura->annotations.active_id, g_free);
  }
  if (g_strcmp0(zathura->annotations.editing_id, id_copy) == 0) {
    g_clear_pointer(&zathura->annotations.editing_id, g_free);
    g_clear_pointer(&zathura->annotations.draft_comment, g_free);
    zathura->annotations.draft_is_new = false;
  }

  girara_list_remove(zathura->annotations.list, target);
  zathura_annotations_rebuild_index(zathura, 0);
  return true;
}

GPtrArray* zathura_annotations_get_for_page(zathura_t* zathura, unsigned int page) {
  if (zathura == NULL || zathura->annotations.by_page == NULL) {
    return NULL;
  }
  if (page >= zathura->annotations.by_page->len) {
    return NULL;
  }
  return g_ptr_array_index(zathura->annotations.by_page, page);
}

static bool rectangle_contains_point(zathura_rectangle_t rect, double x, double y) {
  zathura_rectangle_t norm = normalize_rectangle(rect);
  return (x >= norm.x1 && x <= norm.x2 && y >= norm.y1 && y <= norm.y2);
}

zathura_annotation_t* zathura_annotations_hit_test(zathura_t* zathura, unsigned int page, double page_x, double page_y) {
  GPtrArray* page_anns = zathura_annotations_get_for_page(zathura, page);
  if (page_anns == NULL || page_anns->len == 0) {
    return NULL;
  }

  for (guint i = 0; i < page_anns->len; ++i) {
    zathura_annotation_t* ann = g_ptr_array_index(page_anns, i);
    if (ann == NULL) {
      continue;
    }

    if (ann->rectangles != NULL && girara_list_size(ann->rectangles) > 0) {
      bool matched = false;
      for (size_t r_idx = 0; r_idx != girara_list_size(ann->rectangles); ++r_idx) {
        zathura_rectangle_t* rect = girara_list_nth(ann->rectangles, r_idx);
        if (rect != NULL && rectangle_contains_point(*rect, page_x, page_y)) {
          matched = true;
          break;
        }
      }
      if (matched) {
        return ann;
      }
    } else if (rectangle_contains_point(ann->bounds, page_x, page_y)) {
      return ann;
    }
  }

  return NULL;
}

bool zathura_annotations_start_edit(zathura_t* zathura, const char* id, bool draft_is_new) {
  if (zathura == NULL || id == NULL) {
    return false;
  }

  zathura_annotation_t* ann = zathura_annotations_find(zathura, id);
  if (ann == NULL) {
    return false;
  }

  ann->color            = ZATHURA_ANNOTATION_COLOR_RED;
  ann->baked_in_surface = false;

  g_autofree char* id_copy = g_strdup(id);
  g_free(zathura->annotations.editing_id);
  zathura->annotations.editing_id = g_steal_pointer(&id_copy);

  g_free(zathura->annotations.draft_comment);
  zathura->annotations.draft_comment = g_strdup(ann->comment != NULL ? ann->comment : "");
  zathura->annotations.draft_is_new  = draft_is_new;

  return true;
}

bool zathura_annotations_commit_edit(zathura_t* zathura, const char* id, const char* text) {
  if (zathura == NULL) {
    return false;
  }

  const char* lookup_id = (id != NULL) ? id : zathura->annotations.editing_id;
  if (lookup_id == NULL) {
    return false;
  }

  g_autofree char* id_copy  = g_strdup(lookup_id);
  zathura_annotation_t* ann = zathura_annotations_find(zathura, id_copy);
  if (ann == NULL) {
    return false;
  }

  bool is_editing_target = (g_strcmp0(zathura->annotations.editing_id, id_copy) == 0);
  bool is_new_draft      = is_editing_target && zathura->annotations.draft_is_new;

  if (string_is_blank(text)) {
    if (is_new_draft) {
      zathura_annotations_remove(zathura, id_copy);
      return false;
    }
    g_free(ann->comment);
    ann->comment          = g_strdup("");
    ann->color            = ZATHURA_ANNOTATION_COLOR_YELLOW;
    ann->baked_in_surface = false;
    ann->updated_at       = g_get_real_time();
  } else {
    g_free(ann->comment);
    ann->comment          = g_strdup(text);
    ann->color            = ZATHURA_ANNOTATION_COLOR_RED;
    ann->baked_in_surface = false;
    ann->updated_at       = g_get_real_time();
  }

  if (is_editing_target) {
    g_clear_pointer(&zathura->annotations.editing_id, g_free);
    g_clear_pointer(&zathura->annotations.draft_comment, g_free);
    zathura->annotations.draft_is_new = false;
  }

  return true;
}

void zathura_annotations_cancel_edit(zathura_t* zathura) {
  if (zathura == NULL || zathura->annotations.editing_id == NULL) {
    return;
  }

  g_autofree char* editing_id = g_strdup(zathura->annotations.editing_id);
  bool draft_is_new           = zathura->annotations.draft_is_new;

  g_clear_pointer(&zathura->annotations.editing_id, g_free);
  g_clear_pointer(&zathura->annotations.draft_comment, g_free);
  zathura->annotations.draft_is_new = false;

  if (draft_is_new) {
    zathura_annotations_remove(zathura, editing_id);
  } else {
    zathura_annotation_t* ann = zathura_annotations_find(zathura, editing_id);
    if (ann != NULL) {
      ann->color = zathura_annotation_has_comment(ann) ? ZATHURA_ANNOTATION_COLOR_RED : ZATHURA_ANNOTATION_COLOR_YELLOW;
    }
  }
}

void zathura_annotations_queue_draw_page(zathura_t* zathura, unsigned int page) {
  if (zathura == NULL || zathura->document == NULL || zathura->ui.document_widget == NULL) {
    return;
  }
  unsigned int num_pages = zathura_document_get_number_of_pages(zathura->document);
  if (page >= num_pages) {
    return;
  }
  zathura_page_t* doc_page = zathura_document_get_page(zathura->document, page);
  if (doc_page == NULL) {
    return;
  }
  GtkWidget* page_widget = zathura_page_get_widget(zathura, doc_page);
  if (page_widget != NULL) {
    gtk_widget_queue_draw(page_widget);
    GtkWidget* child = gtk_widget_get_first_child(page_widget);
    if (child != NULL) {
      gtk_widget_queue_draw(child);
    }
  }
}

void zathura_annotations_queue_draw_all(zathura_t* zathura) {
  if (zathura == NULL || zathura->document == NULL || zathura->ui.document_widget == NULL) {
    return;
  }
  unsigned int num_pages = zathura_document_get_number_of_pages(zathura->document);
  for (unsigned int i = 0; i < num_pages; ++i) {
    zathura_annotations_queue_draw_page(zathura, i);
  }
}

void zathura_annotations_set_active(zathura_t* zathura, const char* id) {
  if (zathura == NULL) {
    return;
  }

  if (g_strcmp0(zathura->annotations.active_id, id) == 0) {
    return;
  }

  g_autofree char* new_id = (id != NULL) ? g_strdup(id) : NULL;
  g_free(zathura->annotations.active_id);
  zathura->annotations.active_id = g_steal_pointer(&new_id);

  zathura_annotations_queue_draw_all(zathura);
}

void zathura_annotations_reveal(zathura_t* zathura, const zathura_annotation_t* annotation) {
  if (zathura == NULL || annotation == NULL || zathura->document == NULL) {
    return;
  }

  unsigned int num_pages = zathura_document_get_number_of_pages(zathura->document);
  if (annotation->page >= num_pages) {
    return;
  }

  page_set(zathura, annotation->page);

  zathura_page_t* page = zathura_document_get_page(zathura->document, annotation->page);
  if (page == NULL) {
    return;
  }

  double page_height = zathura_page_get_height(page);
  double page_width  = zathura_page_get_width(page);
  if (page_height > 0.0 && page_width > 0.0) {
    double center_x = 0.5 * (annotation->bounds.x1 + annotation->bounds.x2) / page_width;
    double center_y = 0.5 * (annotation->bounds.y1 + annotation->bounds.y2) / page_height;
    double pos_x    = 0.0;
    double pos_y    = 0.0;
    page_number_to_position(zathura, annotation->page, center_x, center_y, &pos_x, &pos_y);
    position_set(zathura, pos_x, pos_y);
  }

  GtkWidget* page_widget = zathura_page_get_widget(zathura, page);
  if (page_widget != NULL) {
    zathura_page_widget_flash_annotation(ZATHURA_PAGE_WIDGET(page_widget), annotation->id);
  }
}

static gboolean cb_annotations_autosave_timeout(gpointer user_data) {
  zathura_t* zathura = user_data;
  if (zathura == NULL) {
    return G_SOURCE_REMOVE;
  }

  zathura->annotations.autosave_source_id = 0;
  if (zathura->annotations.dirty && zathura->document != NULL) {
    zathura_annotations_save_pdf(zathura, NULL);
  }
  return G_SOURCE_REMOVE;
}

void zathura_annotations_mark_dirty(zathura_t* zathura) {
  if (zathura == NULL) {
    return;
  }

  zathura->annotations.dirty = true;

  if (zathura->annotations.autosave_source_id > 0) {
    g_source_remove(zathura->annotations.autosave_source_id);
    zathura->annotations.autosave_source_id = 0;
  }

  if (zathura->document != NULL) {
    zathura->annotations.autosave_source_id = g_timeout_add(500, cb_annotations_autosave_timeout, zathura);
  }
}
