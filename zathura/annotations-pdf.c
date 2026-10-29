/* SPDX-License-Identifier: Zlib */
/* Part of zathura-annot (modified fork of pwmt/zathura) */

#include "annotations-pdf.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <poppler.h>
#include <string.h>
#include <unistd.h>

#include "annotation-panel.h"
#include "annotations.h"
#include "document.h"
#include "file-monitor.h"
#include "page.h"
#include "zathura.h"

static char* extract_quote_from_poppler_page(PopplerPage* page, girara_list_t* rectangles) {
  if (page == NULL || rectangles == NULL || girara_list_size(rectangles) == 0) {
    return g_strdup("");
  }

  GString* builder = g_string_new(NULL);
  for (size_t idx = 0; idx != girara_list_size(rectangles); ++idx) {
    zathura_rectangle_t* rect = girara_list_nth(rectangles, idx);
    if (rect == NULL) {
      continue;
    }
    PopplerRectangle sel_rect = {
        .x1 = MIN(rect->x1, rect->x2),
        .y1 = MIN(rect->y1, rect->y2),
        .x2 = MAX(rect->x1, rect->x2),
        .y2 = MAX(rect->y1, rect->y2),
    };
    g_autofree gchar* line_text = poppler_page_get_selected_text(page, POPPLER_SELECTION_GLYPH, &sel_rect);
    if (line_text != NULL) {
      g_strstrip(line_text);
      if (line_text[0] != '\0') {
        if (builder->len > 0) {
          g_string_append_c(builder, ' ');
        }
        g_string_append(builder, line_text);
      }
    }
  }

  return g_string_free(builder, FALSE);
}

bool zathura_annotations_load_pdf_file(girara_list_t* list, const char* path, const char* password) {
  if (list == NULL || path == NULL || path[0] == '\0') {
    return false;
  }

  g_autofree gchar* uri = g_filename_to_uri(path, NULL, NULL);
  if (uri == NULL) {
    return false;
  }

  PopplerDocument* doc = poppler_document_new_from_file(uri, password, NULL);
  if (doc == NULL) {
    return false;
  }

  int n_pages = poppler_document_get_n_pages(doc);
  for (int p = 0; p < n_pages; ++p) {
    PopplerPage* page = poppler_document_get_page(doc, p);
    if (page == NULL) {
      continue;
    }

    double page_width  = 0.0;
    double page_height = 0.0;
    poppler_page_get_size(page, &page_width, &page_height);

    GList* mappings  = poppler_page_get_annot_mapping(page);
    unsigned int idx = 0;
    for (GList* l = mappings; l != NULL; l = l->next, ++idx) {
      PopplerAnnotMapping* mapping = l->data;
      if (mapping == NULL || mapping->annot == NULL) {
        continue;
      }

      PopplerAnnot* annot = mapping->annot;
      if (poppler_annot_get_annot_type(annot) != POPPLER_ANNOT_HIGHLIGHT) {
        continue;
      }

      girara_list_t* rectangles = girara_list_new_with_free(g_free);
      if (POPPLER_IS_ANNOT_TEXT_MARKUP(annot)) {
        GArray* quads = poppler_annot_text_markup_get_quadrilaterals(POPPLER_ANNOT_TEXT_MARKUP(annot));
        if (quads != NULL) {
          for (guint qi = 0; qi < quads->len; ++qi) {
            PopplerQuadrilateral q         = g_array_index(quads, PopplerQuadrilateral, qi);
            zathura_annotation_quad_t quad = {
                .p1_x = q.p1.x,
                .p1_y = q.p1.y,
                .p2_x = q.p2.x,
                .p2_y = q.p2.y,
                .p3_x = q.p3.x,
                .p3_y = q.p3.y,
                .p4_x = q.p4.x,
                .p4_y = q.p4.y,
            };
            zathura_rectangle_t* rect = g_malloc(sizeof(zathura_rectangle_t));
            *rect                     = zathura_annotation_pdf_quad_to_rect(quad, page_height);
            girara_list_append(rectangles, rect);
          }
          g_array_unref(quads);
        }
      }

      if (girara_list_size(rectangles) == 0) {
        zathura_annotation_quad_t fallback_quad = {
            .p1_x = mapping->area.x1,
            .p1_y = mapping->area.y2,
            .p2_x = mapping->area.x2,
            .p2_y = mapping->area.y2,
            .p3_x = mapping->area.x1,
            .p3_y = mapping->area.y1,
            .p4_x = mapping->area.x2,
            .p4_y = mapping->area.y1,
        };
        zathura_rectangle_t* rect = g_malloc(sizeof(zathura_rectangle_t));
        *rect                     = zathura_annotation_pdf_quad_to_rect(fallback_quad, page_height);
        girara_list_append(rectangles, rect);
      }

      zathura_annotation_color_t ann_color = ZATHURA_ANNOTATION_COLOR_YELLOW;
      PopplerColor* pdf_color              = poppler_annot_get_color(annot);
      if (pdf_color != NULL) {
        if (pdf_color->red > 0x9999 && pdf_color->green < 0x9999) {
          ann_color = ZATHURA_ANNOTATION_COLOR_RED;
        }
        poppler_color_free(pdf_color);
      }

      bool resolved = false;
      if (POPPLER_IS_ANNOT_MARKUP(annot)) {
        gdouble opacity = poppler_annot_markup_get_opacity(POPPLER_ANNOT_MARKUP(annot));
        if (ann_color == ZATHURA_ANNOTATION_COLOR_YELLOW) {
          resolved = (opacity > 0.0 && fabs(opacity - 0.34) < 0.005);
        } else {
          resolved = (opacity > 0.0 && fabs(opacity - 0.29) < 0.005);
        }
      }

      g_autofree char* quote      = extract_quote_from_poppler_page(page, rectangles);
      g_autofree gchar* contents  = poppler_annot_get_contents(annot);
      zathura_annotation_t* entry =
          zathura_annotation_new((unsigned int)p, ann_color, rectangles, quote, contents != NULL ? contents : "");
      girara_list_free(rectangles);

      if (entry == NULL) {
        continue;
      }

      g_autofree gchar* nm = poppler_annot_get_name(annot);
      g_free(entry->id);
      if (nm != NULL && nm[0] != '\0') {
        entry->id = g_strdup(nm);
      } else {
        entry->id = g_strdup_printf("p%u-%u-%.0f-%.0f", (unsigned int)p, idx, entry->bounds.x1, entry->bounds.y1);
      }

      entry->resolved         = resolved;
      entry->baked_in_surface = true;

      girara_list_append(list, entry);
    }

    poppler_page_free_annot_mapping(mappings);
    g_object_unref(page);
  }

  girara_list_sort(list, zathura_annotation_compare);
  g_object_unref(doc);
  return true;
}

bool zathura_annotations_save_pdf_file(girara_list_t* list, const char* source_path, const char* password,
                                       const char* target_path) {
#ifdef WITH_SANDBOX
  (void)list;
  (void)source_path;
  (void)password;
  (void)target_path;
  return false;
#else
  if (list == NULL || source_path == NULL || source_path[0] == '\0') {
    return false;
  }

  const char* dest_path = (target_path != NULL && target_path[0] != '\0') ? target_path : source_path;

  g_autofree gchar* source_uri = g_filename_to_uri(source_path, NULL, NULL);
  if (source_uri == NULL) {
    return false;
  }

  PopplerDocument* doc = poppler_document_new_from_file(source_uri, password, NULL);
  if (doc == NULL) {
    return false;
  }

  int n_pages = poppler_document_get_n_pages(doc);
  for (int p = 0; p < n_pages; ++p) {
    PopplerPage* page = poppler_document_get_page(doc, p);
    if (page == NULL) {
      continue;
    }

    double page_width  = 0.0;
    double page_height = 0.0;
    poppler_page_get_size(page, &page_width, &page_height);

    /* Remove all existing highlight annotations on this page */
    GList* mappings = poppler_page_get_annot_mapping(page);
    for (GList* l = mappings; l != NULL; l = l->next) {
      PopplerAnnotMapping* mapping = l->data;
      if (mapping != NULL && mapping->annot != NULL &&
          poppler_annot_get_annot_type(mapping->annot) == POPPLER_ANNOT_HIGHLIGHT) {
        poppler_page_remove_annot(page, mapping->annot);
      }
    }
    poppler_page_free_annot_mapping(mappings);

    /* Write all current annotations for this page */
    for (size_t idx = 0; idx != girara_list_size(list); ++idx) {
      zathura_annotation_t* ann = girara_list_nth(list, idx);
      if (ann == NULL || ann->page != (unsigned int)p) {
        continue;
      }

      GArray* quads = g_array_new(FALSE, FALSE, sizeof(PopplerQuadrilateral));
      if (ann->rectangles != NULL && girara_list_size(ann->rectangles) > 0) {
        for (size_t r_idx = 0; r_idx != girara_list_size(ann->rectangles); ++r_idx) {
          zathura_rectangle_t* rect = girara_list_nth(ann->rectangles, r_idx);
          if (rect == NULL) {
            continue;
          }
          zathura_annotation_quad_t q   = zathura_annotation_rect_to_pdf_quad(*rect, page_height);
          PopplerQuadrilateral pdf_quad = {
              .p1 = {q.p1_x, q.p1_y},
              .p2 = {q.p2_x, q.p2_y},
              .p3 = {q.p3_x, q.p3_y},
              .p4 = {q.p4_x, q.p4_y},
          };
          g_array_append_val(quads, pdf_quad);
        }
      } else {
        zathura_annotation_quad_t q   = zathura_annotation_rect_to_pdf_quad(ann->bounds, page_height);
        PopplerQuadrilateral pdf_quad = {
            .p1 = {q.p1_x, q.p1_y},
            .p2 = {q.p2_x, q.p2_y},
            .p3 = {q.p3_x, q.p3_y},
            .p4 = {q.p4_x, q.p4_y},
        };
        g_array_append_val(quads, pdf_quad);
      }

      if (quads->len == 0) {
        g_array_unref(quads);
        continue;
      }

      PopplerRectangle pdf_bounds = {
          .x1 = ann->bounds.x1,
          .y1 = page_height - ann->bounds.y2,
          .x2 = ann->bounds.x2,
          .y2 = page_height - ann->bounds.y1,
      };

      PopplerAnnot* annot = poppler_annot_text_markup_new_highlight(doc, &pdf_bounds, quads);
      g_array_unref(quads);
      if (annot == NULL) {
        continue;
      }

      PopplerColor pdf_color = {0};
      gdouble opacity        = 0.35;
      if (ann->color == ZATHURA_ANNOTATION_COLOR_RED) {
        pdf_color.red   = 0xFF00;
        pdf_color.green = 0x5200;
        pdf_color.blue  = 0x5200;
        opacity         = ann->resolved ? 0.29 : 0.30;
      } else {
        pdf_color.red   = 0xFFFF;
        pdf_color.green = 0xDE00;
        pdf_color.blue  = 0x2100;
        opacity         = ann->resolved ? 0.34 : 0.35;
      }

      poppler_annot_set_color(annot, &pdf_color);
      if (POPPLER_IS_ANNOT_MARKUP(annot)) {
        poppler_annot_markup_set_opacity(POPPLER_ANNOT_MARKUP(annot), opacity);
        const char* user_name = g_get_user_name();
        if (user_name != NULL) {
          poppler_annot_markup_set_label(POPPLER_ANNOT_MARKUP(annot), user_name);
        }
      }
      poppler_annot_set_contents(annot, ann->comment != NULL ? ann->comment : "");

      poppler_page_add_annot(page, annot);
      g_object_unref(annot);
    }

    g_object_unref(page);
  }

  g_autofree gchar* dest_dir = g_path_get_dirname(dest_path);
  g_autofree gchar* tmp_path = g_build_filename(dest_dir, ".zathura-annot-XXXXXX.pdf", NULL);
  int fd                     = g_mkstemp(tmp_path);
  if (fd < 0) {
    g_object_unref(doc);
    return false;
  }
  close(fd);

  g_autofree gchar* tmp_uri = g_filename_to_uri(tmp_path, NULL, NULL);
  if (tmp_uri == NULL) {
    g_unlink(tmp_path);
    g_object_unref(doc);
    return false;
  }

  gboolean saved = poppler_document_save(doc, tmp_uri, NULL);
  g_object_unref(doc);

  if (!saved) {
    g_unlink(tmp_path);
    return false;
  }

  if (g_rename(tmp_path, dest_path) != 0) {
    g_unlink(tmp_path);
    return false;
  }

  return true;
#endif
}

#ifndef WITH_SANDBOX
static void strip_poppler_page_highlights(PopplerPage* page) {
  if (page == NULL) {
    return;
  }
  GList* mappings = poppler_page_get_annot_mapping(page);
  for (GList* l = mappings; l != NULL; l = l->next) {
    PopplerAnnotMapping* mapping = l->data;
    if (mapping != NULL && mapping->annot != NULL &&
        poppler_annot_get_annot_type(mapping->annot) == POPPLER_ANNOT_HIGHLIGHT) {
      poppler_page_remove_annot(page, mapping->annot);
    }
  }
  poppler_page_free_annot_mapping(mappings);
}
#endif

bool zathura_annotations_load_pdf(zathura_t* zathura, const char* path, const char* password) {
  if (zathura == NULL) {
    return false;
  }

  if (zathura->annotations.list == NULL || zathura->annotations.by_page == NULL) {
    zathura_annotations_init(zathura);
  } else {
    zathura_annotations_clear(zathura);
  }

  const char* effective_path = path;
  if ((effective_path == NULL || effective_path[0] == '\0') && zathura->document != NULL) {
    effective_path = zathura_document_get_path(zathura->document);
  }

  const char* effective_password = password;
  if (effective_password == NULL && zathura->document != NULL) {
    effective_password = zathura_document_get_password(zathura->document);
  }

  bool ok = zathura_annotations_load_pdf_file(zathura->annotations.list, effective_path, effective_password);

#ifndef WITH_SANDBOX
  /* Strip POPPLER_ANNOT_HIGHLIGHT from the live in-memory PopplerDocument so Poppler's
   * page renderer does not bake static highlights into page surfaces, allowing dynamic
   * recoloring, brightening on selection, and immediate removal. */
  if (zathura->document != NULL) {
    void* doc_data = zathura_document_get_data(zathura->document);
    if (doc_data != NULL && POPPLER_IS_DOCUMENT(doc_data)) {
      PopplerDocument* poppler_doc = POPPLER_DOCUMENT(doc_data);
      unsigned int num_pages       = zathura_document_get_number_of_pages(zathura->document);
      for (unsigned int p = 0; p < num_pages; ++p) {
        zathura_page_t* zpage = zathura_document_get_page(zathura->document, p);
        if (zpage != NULL) {
          void* page_data = zathura_page_get_data(zpage);
          if (page_data != NULL && POPPLER_IS_PAGE(page_data)) {
            strip_poppler_page_highlights(POPPLER_PAGE(page_data));
          }
        }
        PopplerPage* ppage = poppler_document_get_page(poppler_doc, (int)p);
        if (ppage != NULL) {
          strip_poppler_page_highlights(ppage);
          g_object_unref(ppage);
        }
      }

      for (size_t idx = 0; idx != girara_list_size(zathura->annotations.list); ++idx) {
        zathura_annotation_t* ann = girara_list_nth(zathura->annotations.list, idx);
        if (ann != NULL) {
          ann->baked_in_surface = false;
        }
      }
    }
  }
#endif

  zathura_annotations_rebuild_index(zathura, 0);

  if (zathura->ui.annotation_panel != NULL) {
    zathura_annotation_panel_refresh(zathura->ui.annotation_panel);
  }

  return ok;
}

bool zathura_annotations_save_pdf(zathura_t* zathura, const char* target_path) {
  if (zathura == NULL || zathura->annotations.list == NULL) {
    return false;
  }

  if (zathura->annotations.autosave_source_id > 0) {
    g_source_remove(zathura->annotations.autosave_source_id);
    zathura->annotations.autosave_source_id = 0;
  }

  const char* source_path = NULL;
  const char* password    = NULL;
  if (zathura->document != NULL) {
    source_path = zathura_document_get_path(zathura->document);
    password    = zathura_document_get_password(zathura->document);
  }
  if (source_path == NULL || source_path[0] == '\0') {
    source_path = target_path;
  }

  const char* dest_path = (target_path != NULL && target_path[0] != '\0') ? target_path : source_path;
  if (source_path == NULL || dest_path == NULL) {
    return false;
  }

  bool suspend_monitor = (zathura->file_monitor.monitor != NULL && g_strcmp0(source_path, dest_path) == 0);
  if (suspend_monitor) {
    zathura_filemonitor_stop(zathura->file_monitor.monitor);
  }

  bool ok = zathura_annotations_save_pdf_file(zathura->annotations.list, source_path, password, dest_path);

  if (suspend_monitor) {
    zathura_filemonitor_start(zathura->file_monitor.monitor);
  }

  if (ok && g_strcmp0(source_path, dest_path) == 0) {
    zathura->annotations.dirty = false;
  }

  return ok;
}
