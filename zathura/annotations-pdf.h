/* SPDX-License-Identifier: Zlib */
/* Part of zathura-annot (modified fork of pwmt/zathura) */

#ifndef ANNOTATIONS_PDF_H
#define ANNOTATIONS_PDF_H

#include <stdbool.h>
#include <girara/datastructures.h>

#include "types.h"

/**
 * Tighten selection or annotation rectangles on a page so each rectangle
 * matches a single line of text and its vertical height is limited to the
 * text height without including inter-line spacing.
 */
girara_list_t* zathura_annotations_tighten_page_rectangles(zathura_page_t* page, girara_list_t* rectangles);

/**
 * Load all /Highlight annotations from a PDF file into a list of zathura_annotation_t*.
 */
bool zathura_annotations_load_pdf_file(girara_list_t* list, const char* path, const char* password);

/**
 * Save all annotations from a list of zathura_annotation_t* into a PDF file.
 */
bool zathura_annotations_save_pdf_file(girara_list_t* list, const char* source_path, const char* password,
                                       const char* target_path);

/**
 * Load all /Highlight annotations from a PDF file into zathura->annotations.
 */
bool zathura_annotations_load_pdf(zathura_t* zathura, const char* path, const char* password);

/**
 * Save all annotations in zathura->annotations into the PDF at target_path
 * (or the current document path if target_path is NULL).
 */
bool zathura_annotations_save_pdf(zathura_t* zathura, const char* target_path);

#endif // ANNOTATIONS_PDF_H
