# Plan: highlights and comments in zathura

Status: proposal, not implemented. Revised to store all highlights and comments
directly inside the PDF file itself via `poppler-glib`, with no SQLite table or
external plugin repository required.

Goal: select text, right click, and choose a light yellow highlight, a light red
highlight, or a comment, in the style of Word and Google Docs. Comments can be
edited, resolved, reopened and deleted. Clicking a card reveals the matching text
on the page and clicking a highlight reveals its card. Everything is written
directly into the PDF file as standard `/Highlight` annotations so that files
synced over Dropbox or opened in Adobe Acrobat, Okular, and macOS Preview carry
all highlights and comments with them.

## 1. What the code does today

The tree at `develop` (2026.10.23) builds cleanly with `meson setup build` and
`ninja -C build`, and passes `meson test -C build document types utils`.

### 1.1 Existing pieces to reuse

Text selection is already implemented:

* `zathura/page-widget.c:1156` (`cb_zathura_page_widget_motion_notify`)
  converts the dragged rectangle to page coordinates with `rotate_point`, divides
  by the document scale, and calls `zathura_page_get_selection`. The plugin
  returns one rectangle per text line, in unrotated page coordinates, which
  `recalc_rectangle` (`zathura/utils.c:247`) transforms to widget coordinates at
  any zoom or rotation.
* `page-widget.c:1082` (`cb_zathura_page_widget_button_release_event`) obtains
  the selected text with `zathura_page_get_text` and emits `text-selected`, which
  `callbacks.c:611` forwards to the clipboard.
* `page-widget.c:703` draws a per-line rectangle list over the page for the live
  selection.

Right-click already opens a popover:

* `page-widget.c:1078` routes `GDK_BUTTON_SECONDARY` to
  `zathura_page_widget_popup_menu` (`:1244`), which builds a `GMenu` and shows a
  lazily created `GtkPopoverMenu` parented to the page widget.

Window layout:

* `girara-gtk/session.c:286` builds a `GtkStack` (`session->gtk.view`) inside a
  vertical box (`session->gtk.box`), inside a `GtkOverlay`. `girara_set_view`
  (`:566`) adds a child to that stack, and the index view uses the same mechanism
  (`shortcuts.c:1398`). A horizontal `GtkPaned` can hold the document scrolled
  window (`zathura->ui.view`) as its start child and the comment panel as its end
  child.

### 1.2 Why local SQLite storage (`bookmarks.sqlite`) is dropped

Storing annotations in `~/.local/share/zathura/bookmarks.sqlite` was considered
and rejected:

* `bookmarks.sqlite` stays on the local machine and does not travel with the PDF
  across Dropbox, Git, or email, and is invisible to Okular, Adobe Acrobat, and
  Preview.
* Maintaining both `bookmarks.sqlite` and PDF `/Highlight` objects creates two
  sources of truth: if an annotation is deleted or edited in Okular or on another
  machine via Dropbox, a local SQLite table on the first machine either resurrects
  deleted annotations or serves stale text.
* `poppler-glib` (installed here at 26.08.0 in
  `/usr/include/poppler/glib/poppler-annot.h` and `poppler-page.h`) already
  provides pure-C functions to read, create, modify, and remove PDF `/Highlight`
  annotations (`poppler_annot_text_markup_new_highlight`,
  `poppler_annot_text_markup_set_quadrilaterals`,
  `poppler_page_get_annot_mapping`, `poppler_page_add_annot`,
  `poppler_page_remove_annot`, `poppler_document_save`).
* Linking `poppler-glib` in `zathura` core lets this repository read and write
  standard PDF annotations directly without touching `/usr/lib/zathura/libpdf-poppler.so`
  or maintaining a second repository, and saves ~400 lines of SQLite schema and
  sync code.

## 2. How comments and highlights are saved in the PDF

All highlights and comments are stored as standard PDF `/Highlight` (`AnnotTextMarkup`)
objects inside the PDF file itself.

### 2.1 PDF dictionary mapping (`zathura/annotations-pdf.c`)

| `zathura_annotation_t` field | PDF `/Highlight` dictionary key | `poppler-glib` API |
| --- | --- | --- |
| `id` | `/NM` (when present) or deterministic `p<page>-<idx>-<x1>-<y1>` | Read via `poppler_annot_get_name` when present; otherwise generated deterministically from page and rectangle coordinates |
| `rectangles` | `/QuadPoints` and `/Rect` | `PopplerQuadrilateral` array passed to `poppler_annot_text_markup_new_highlight` and `poppler_annot_text_markup_set_quadrilaterals`, with `y_top = page_height - rect.y1`, `y_bottom = page_height - rect.y2`, `p1=(x1,y_top)`, `p2=(x2,y_top)`, `p3=(x1,y_bottom)`, `p4=(x2,y_bottom)` |
| `color` | `/C` | `poppler_annot_set_color` / `poppler_annot_get_color` (`PopplerColor` 16-bit RGB: yellow `{65535, 62258, 26214}`, red `{65535, 32768, 32768}`) |
| `resolved` + alpha | `/CA` (opacity) | `poppler_annot_markup_set_opacity` / `poppler_annot_markup_get_opacity`: `0.35` when open, `0.34` when resolved (`fabs(opacity - 0.34) < 0.005` on load marks `resolved = true` while keeping the visible highlight background identical on the page) |
| `quote` | none (derived from page text) | Computed on creation via `zathura_page_get_text` and on load directly via `poppler_page_get_selected_text(poppler_page, POPPLER_SELECTION_GLYPH, &area)` so unloaded `zathura_page_t` instances are not required |
| `body` | `/Contents` | `poppler_annot_set_contents` / `poppler_annot_get_contents` (`NULL` or `""` for a plain highlight; non-empty for a comment) |
| `author` | `/T` | `poppler_annot_markup_set_label` / `poppler_annot_markup_get_label` |
| `modified` | `/M` | Read via `poppler_annot_get_modified`; updated automatically by Poppler on mutation |
| sticky-note popup | `/Popup` (`Open = false`) | `poppler_annot_markup_set_popup` and `poppler_annot_markup_set_popup_is_open(..., FALSE)` so Acrobat and Okular show the comment in their sidebar without popping open sticky windows |

### 2.2 When writes happen (atomic in-place PDF save)

`PopplerDocument` cannot overwrite the exact file descriptor it is mmap-reading
from in place, and `poppler-glib` does not expose `poppler_annot_set_name`.
Therefore, `zathura_annotations_save_pdf(zathura, target_path)` performs a
stateless atomic write of all non-draft annotations in `zathura->annotations.list`:

1. Opens `target_path` (or the source document path when saving a new copy via
   `:write`) with `poppler_document_new_from_file`.
2. For each page, removes existing `POPPLER_ANNOT_HIGHLIGHT` objects and adds fresh
   `PopplerAnnotTextMarkup` highlight objects for every non-draft
   `zathura_annotation_t` on that page.
3. Writes the updated `PopplerDocument` via `poppler_document_save` to a
   temporary file in the same directory (`<dir>/.<basename>.zathura-XXXXXX.pdf`).
4. Copies the file permissions (`stat` / `chmod`) of `target_path` if it exists.
5. Stops `zathura->file_monitor.monitor` (`zathura_filemonitor_stop`) so the
   atomic replace does not trigger an unwanted self-reload.
6. Atomically replaces `target_path` with `g_rename(tmp_path, target_path)`.
7. Restarts `zathura->file_monitor.monitor`.

Every committed user action triggers `zathura_annotations_save_pdf` immediately:

* **New highlight (`Highlight in yellow` / `Highlight in red`)**: appends the
  highlight to `zathura->annotations.list` and atomically saves the PDF.
* **New comment (`Add comment`)**: creates an in-memory draft (`draft = true`,
  empty `body`), opens the comment panel, and focuses the card's `GtkTextView`.
  Nothing is written to the PDF while the comment is an unsaved draft.
  * Clicking **Save** (or pressing `Ctrl+Return`) with non-whitespace text clears
    `draft` and atomically saves the PDF.
  * Clicking **Cancel** (or pressing `Escape`) on a draft removes it from memory
    without touching the PDF.
* **Editing a comment (`Edit` -> `Save`), changing color, or toggling `Resolve` /
  `Reopen`**: updates the `zathura_annotation_t` in memory and atomically saves
  the PDF.
* **Deleting an annotation (`Delete`)**: removes the `zathura_annotation_t` from
  memory and atomically saves the PDF.
* **Running `:write <path>` (`document_save`)**: after `zathura_document_save_as`
  writes the base PDF to `<path>`, calls `zathura_annotations_save_pdf(zathura,
  file_path)` so all in-memory annotations are persisted to `<path>`. If the
  original file was on a read-only filesystem, `:write <writable-path>` persists
  all in-memory annotations to the new file.

## 3. Data model (`zathura/annotations.h`, `zathura/annotations.c`)

Plain C structs and a `girara_list_t` with a free function, modelled on
`bookmarks.c`:

```c
typedef enum {
  ZATHURA_ANNOTATION_HIGHLIGHT = 0,
  ZATHURA_ANNOTATION_COMMENT   = 1,
} zathura_annotation_kind_t;

typedef enum {
  ZATHURA_ANNOTATION_YELLOW = 0,
  ZATHURA_ANNOTATION_RED    = 1,
} zathura_annotation_color_t;

typedef struct zathura_annotation_s {
  char* id;                        /* PDF /NM unique name (or temporary UUID while draft) */
  unsigned int page;               /* 0-based page index */
  girara_list_t* rectangles;       /* zathura_rectangle_t*, unrotated page coordinates */
  zathura_annotation_kind_t kind;
  zathura_annotation_color_t color;
  char* quote;                     /* marked text, cached from zathura_page_get_text */
  char* body;                      /* comment text, NULL for a plain highlight */
  char* author;                    /* PDF /T */
  char* modified;                  /* display timestamp */
  bool resolved;                   /* true when resolved (PDF /CA = 0.34) */
  bool draft;                      /* true until a new comment is saved for the first time */
  bool baked_in_surface;           /* true if libpdf-poppler already painted this highlight on priv->surface */
} zathura_annotation_t;
```

Store, held by `zathura_t` in `zathura.h` next to `bookmarks`:

```c
struct {
  girara_list_t* list;          /* zathura_annotation_t* for the open document */
  GHashTable* by_page;          /* GUINT_TO_POINTER(page) -> girara_list_t* of borrowed pointers */
  char* active_id;              /* id of the active annotation, or NULL */
} annotations;
```

`by_page` is rebuilt on every mutation, so page drawing is a hash lookup instead
of a full scan. Page widgets are created lazily (`document-widget.c:764`) and
query `zathura->annotations.by_page` at draw time.

## 4. Drawing and avoiding double-rendered highlights (`zathura/page-widget.c`)

### 4.1 Coordinating with `libpdf-poppler.so`'s surface rendering

When a PDF is opened, `/usr/lib/zathura/libpdf-poppler.so` renders each page via
`poppler_page_render`, which paints any `/Highlight` annotations that were
already in the PDF on disk into `priv->surface`.

To prevent double-painting while keeping newly created edits instant:

* Annotations loaded from the PDF at `document_open` have
  `baked_in_surface = true`. For these annotations,
  `zathura_page_widget_draw_annotations` skips the base translucent fill (since
  `priv->surface` already has it) and draws only the comment badge, the active
  border when selected, and the reveal pulse.
* Annotations created during the current session have `baked_in_surface = false`.
  For these, `zathura_page_widget_draw_annotations` draws the translucent fill
  (`rgba(1.00, 0.94, 0.35, 0.35)` for yellow, `rgba(1.00, 0.42, 0.42, 0.30)` for
  red), plus the badge (when the comment panel is visible), active border, and
  pulse. Because atomic save replaces the directory entry via `g_rename` while
  `libpdf-poppler.so` retains its open file descriptor to the initial inode,
  zooming or panning does not bake newly created session highlights into
  `priv->surface` until a full reload occurs.
* Hiding the comment panel (`:comments`, `Ctrl+Alt+C`, or the panel close
  button) or resolving a comment never hides or dims the highlighted background
  over the text on the page. When the comment panel is hidden, the `8 px` corner
  badge is also omitted so only the clean yellow or red highlight over the text
  remains visible, and hovering over a commented highlight displays its comment
  text as a tooltip on the page widget.
* If the user deletes or recolors an annotation that had
  `baked_in_surface == true`, after the atomic PDF save completes, zathura
  reloads the document in place (preserving the current page, scroll ratios, and
  comment panel state via `sc_reload`) so `priv->surface` reflects the removed or
  recolored highlight. Resolving or reopening a comment does not alter the
  highlight fill and requires no reload.

### 4.2 Placement in `cb_page_draw`

In `cb_page_draw` (`page-widget.c:504`), lines 536 to 559 apply a rotation
matrix to paint `priv->surface` and call `cairo_restore(cairo)` at line 559.
Because `recalc_rectangle` (`utils.c:247`) already rotates and scales page
coordinates into widget coordinates,
`zathura_page_widget_draw_annotations(ZathuraPageWidget* page, cairo_t* cairo)`
is called **after** `cairo_restore(cairo)` at line 559 (after the thumbnail
early-return at line 590, and before `/* draw links */` at line 592).

For each annotation on the page:

* If `!annotation->baked_in_surface`, fill each transformed rectangle at low
  alpha.
* If `g_strcmp0(annotation->id, zathura->annotations.active_id) == 0`, add `0.10`
  to the fill alpha and stroke a `1.5 px` border in a darker tone of the same
  hue.
* If `annotation->kind == ZATHURA_ANNOTATION_COMMENT` and the comment panel is
  visible (`gtk_widget_get_visible(zathura->ui.annotation_panel)`), draw an
  `8 px` rounded badge at the top-right corner of the first rectangle: filled
  when `!annotation->resolved`, stroked (hollow) when `annotation->resolved`.
  When the comment panel is hidden, omit the badge so only the highlighted text
  background is shown.

Reveal pulse: `ZathuraPageWidgetPrivate` gets `gint64 flash_started`, `gint64
flash_until`, and `guint flash_tick_id`. While `g_get_monotonic_time() <
flash_until`, the active annotation receives a sine-wave alpha boost of up to
`0.25`. A `gtk_widget_add_tick_callback` queues redraws until `flash_until` and
removes itself. `zathura_page_widget_dispose` removes `flash_tick_id` if active.

## 5. Creating and interacting with annotations (`zathura/page-widget.c`)

### 5.1 Preserving the selection snapshot across pointer movement

In the current code:

* `cb_zathura_page_widget_motion_notify` (`page-widget.c:1181`) writes
  `priv->mouse.selection.x2` and `y2` without normalizing `x1 <= x2` and
  `y1 <= y2`;
* `cb_zathura_page_widget_button_release_event` (`:1127`) resets
  `priv->mouse.selection` to `-1`;
* `cb_zathura_page_widget_leave_notify` (`:1226`) calls
  `zathura_page_widget_clear_selection(page)` when `selection-keep-highlight` is
  `false` (the default), which frees `priv->selection.list` as soon as the
  pointer enters a context popover.

Add to `ZathuraPageWidgetPrivate`:

```c
struct {
  zathura_rectangle_t bounds; /* normalized selection box in page coordinates */
  girara_list_t* rectangles;  /* deep-copied zathura_rectangle_t* list in page coordinates */
  char* text;                 /* selected text from zathura_page_get_text */
  bool valid;
} selection_snapshot;
```

Lifecycle of `selection_snapshot`:

* Populated in `cb_zathura_page_widget_button_release_event` when
  `priv->selection.list != NULL && girara_list_size(priv->selection.list) > 0`:
  normalize `priv->mouse.selection` with `next_selection_rectangle`, divide by
  `scale`, deep-copy `priv->selection.list` into `selection_snapshot.rectangles`,
  store `text` from `zathura_page_get_text`, and set `valid = true`.
* Not cleared by `cb_zathura_page_widget_leave_notify`, and while
  `selection_snapshot.valid == true`, `cb_zathura_page_widget_leave_notify` also
  skips `zathura_page_widget_clear_selection(page)` so opening a `GtkPopoverMenu`
  keeps both the snapshot and the visible selection highlight on the page.
* Cleared explicitly on a new primary button press
  (`cb_zathura_page_widget_button_press_event` at `:1038`), when an annotation
  action consumes it, and in `zathura_page_widget_finalize`.

### 5.2 Context popovers and actions

Register an `annotation` `GSimpleActionGroup` on `ZathuraPageWidget` in
`zathura_page_widget_init` (next to `image_actions` at `page-widget.c:253`):

* Selection popover (`priv->selection_popover`), shown on right-click when
  `selection_snapshot.valid == true`:
  * `Highlight in yellow` (`annotation.yellow`)
  * `Highlight in red` (`annotation.red`)
  * `Add comment` (`annotation.comment`)
* Existing-annotation popover (`priv->annotation_popover`), shown on right-click
  when `selection_snapshot.valid == false` and an annotation rectangle contains
  the click point (`rotate_point` divided by `scale`, iterating back-to-front so
  the topmost annotation wins):
  * `Edit comment` / `Add comment` (`annotation.edit`)
  * `Highlight in yellow` (`annotation.recolor-yellow`)
  * `Highlight in red` (`annotation.recolor-red`)
  * `Resolve` / `Reopen` (`annotation.toggle-resolved`, enabled for comments)
  * `Delete` (`annotation.delete`)
* Image popover (`priv->image_popover`), shown on right-click when neither a
  selection snapshot nor an annotation matches and an image is under the cursor.

### 5.3 Left-click activation vs. drag selection and links

In `cb_zathura_page_widget_button_release_event` (`page-widget.c:1104`), note
that `cb_zathura_page_widget_button_press_event` (`:1066`) initializes
`priv->mouse.selection.x2 = x` and `y2 = y` on `n_press == 1` (and only sets
`x2 = -1, y2 = -1` on `n_press == 2 || n_press == 3`):

* A double click occurred if `priv->mouse.selection.x2 == -1 &&
  priv->mouse.selection.y2 == -1`; if `double_click_follow`, follow a link at
  `(oldx, oldy)` as today.
* A drag selection occurred if `priv->selection.list != NULL &&
  girara_list_size(priv->selection.list) > 0`.
* A stationary single click occurred if `priv->mouse.selection.x1 != -1` and
  `(priv->selection.list == NULL || girara_list_size(priv->selection.list) == 0)`.
  On a stationary single click:
  1. Check if a PDF link is under `(oldx, oldy)` when `!double_click_follow`; if
     so, follow the link as today.
  2. Otherwise, hit-test annotations on the page at `(oldx, oldy)`. If an
     annotation is hit, call `zathura_annotation_set_active(zathura,
     annotation->id)`, start the pulse on the page, and if it is a comment,
     ensure the comment panel is visible and scroll its card into view. If no
     annotation is hit, clear `zathura->annotations.active_id`.

## 6. Comment panel (`zathura/annotation-panel.{c,h}`)

### 6.1 Widget hierarchy and `GtkPaned` sizing

In `init_ui` (`zathura.c:249`):

```c
zathura->ui.view             = gtk_scrolled_window_new();
zathura->ui.annotation_panel = zathura_annotation_panel_new(zathura);
zathura->ui.view_box         = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
gtk_paned_set_start_child(GTK_PANED(zathura->ui.view_box), zathura->ui.view);
gtk_paned_set_end_child(GTK_PANED(zathura->ui.view_box), zathura->ui.annotation_panel);
gtk_paned_set_resize_start_child(GTK_PANED(zathura->ui.view_box), TRUE);
gtk_paned_set_resize_end_child(GTK_PANED(zathura->ui.view_box), FALSE);
gtk_paned_set_shrink_start_child(GTK_PANED(zathura->ui.view_box), TRUE);
gtk_paned_set_shrink_end_child(GTK_PANED(zathura->ui.view_box), FALSE);
gtk_widget_set_visible(zathura->ui.annotation_panel, FALSE);
girara_set_view(zathura->ui.session, zathura->ui.view_box);
```

Setting `resize-end-child = FALSE` and `shrink-end-child = FALSE` with
`gtk_widget_set_size_request(zathura->ui.annotation_panel, comments_width, -1)`
gives the panel its configured initial width (`comments-width`, default `320`)
while giving all window resize delta to the PDF view. The three `girara_set_view`
calls that pass `zathura->ui.view` (`zathura.c:250`, `zathura.c:1230`,
`shortcuts.c:1398`) pass `zathura->ui.view_box` instead.

### 6.2 Event-controller guards in `girara-gtk/callbacks.c`

`session->gtk.view` (`GtkStack`) installs capture-phase key and scroll
controllers plus click and motion controllers (`girara-gtk/session.c:316-335`).
Because `ui.annotation_panel` sits inside `ui.view_box` in that stack,
`girara-gtk/callbacks.c` guards all five callbacks:

* When the focused widget (`gtk_window_get_focus`, for key events) or the picked
  widget at `(x, y)` (`gtk_widget_pick(session->gtk.view, x, y, GTK_PICK_DEFAULT)`,
  for scroll, button press, button release, and motion events) is inside
  `.annotation-panel` (or is a `GtkEditable` / `GtkTextView`), return `FALSE`
  immediately so GTK routes typing, `Space`/`Return`/`Tab` keystrokes, mouse
  wheel scrolling, and middle-click paste to the panel's widgets. In
  `girara_callback_view_scroll_event`, the pick check runs immediately after
  `scroll_event_position(controller, &event.x, &event.y)` before `event.x` and
  `event.y` are overwritten by the scroll deltas.

### 6.3 Panel contents, styling, and two-way reveal

Inside `ui.annotation_panel` (CSS class `"annotation-panel"`):

* Header row: title label (`Comments`), count of open comments, a `Show resolved`
  `GtkCheckButton`, and a close `GtkButton`.
* `GtkScrolledWindow` containing a vertical `GtkBox` of cards, sorted by
  `(page, first_rect.y1, first_rect.x1)`:
  * Meta row: author label, timestamp, and a `Page N` button;
  * Quoted text: italic, ellipsized at 2 lines;
  * Body area: a wrapping `GtkLabel` in display mode, or a `GtkTextView` with
    `Save` and `Cancel` buttons while editing;
  * Action row: `Edit`, `Resolve` (or `Reopen`), and `Delete` buttons.
* Card styles live in `data/zathura.css_t` under `#@session@`, using
  `@index-bg@`, `@index-fg@`, `@index-active-bg@`, and `@index-active-fg@`.
* **Card clicked**: `zathura_annotation_reveal` ensures the target page widget
  exists (`zathura_document_widget_ensure_page`), updates the current page,
  computes `(pos_x, pos_y)` using `recalc_rectangle` and
  `page_number_to_position` (matching `search_document` in `utils.c:692-729`),
  calls `position_set(zathura, pos_x, pos_y)`, marks the annotation active, and
  starts the reveal pulse.
* **Highlight clicked**: `zathura_annotation_set_active` sets `active_id`,
  redraws the page, applies `.annotation-card-active`, and scrolls the card into
  view in the panel.

## 7. Commands, shortcuts, settings

* `:comments` toggles the comment panel (`cmd_comments` in `commands.c`,
  registered in `config.c`).
* `Ctrl+Alt+C` maps to `sc_toggle_comments` in `normal` mode (`config.c`).
* `sc_comment_selection` in `shortcuts.c` creates a comment from the current
  `selection_snapshot` via keyboard.
* Setting `comments-author` (string, default `g_get_real_name()` falling back to
  `g_get_user_name()`).
* Setting `comments-width` (integer, default `320`).

## 8. Testable units and verification plan

The implementation is divided into four independently testable units, each with
its own `GTest` executable in `tests/meson.build` run by `meson test -C build`.

### Unit 1: Annotation data model, coordinate transforms, and in-memory store

* **Files**: `zathura/annotations.h`, `zathura/annotations.c`,
  `tests/test_annotations_model.c`, `meson.build`, `tests/meson.build`.
* **Scope**:
  * `zathura_annotation_new`, `zathura_annotation_free`, `zathura_annotation_copy`.
  * Coordinate conversion helpers between zathura top-left page rectangles and
    PDF bottom-left quadrilaterals (`zathura_annotation_rect_to_pdf_quad` and
    `zathura_annotation_pdf_quad_to_rect`).
  * Store lifecycle (`zathura_annotations_init`, `zathura_annotations_clear`),
    `by_page` hash indexing, sorting comparator `(page, y1, x1)`, hit-testing
    `zathura_annotations_hit_test(zathura, page, page_x, page_y)`, and draft
    lifecycle (`draft = true` on new comment, discarded on cancel, committed on
    save).
* **Unit test (`meson test -C build annotations_model`, headless)**:
  * Create, copy, and free highlight and comment structs.
  * Round-trip rectangle coordinates through `rect_to_pdf_quad` and
    `pdf_quad_to_rect` across multiple page heights and verify exact coordinate
    recovery and vertex ordering.
  * Insert annotations across multiple pages and vertical positions; verify
    `zathura_annotations_get_for_page` and panel sort order.
  * Verify `zathura_annotations_hit_test` returns the topmost annotation when two
    highlights overlap and `NULL` outside their bounds.
  * Verify cancelling a draft comment removes it from the store while cancelling
    an edit on a saved comment keeps its previous `body`.

### Unit 2: Direct PDF persistence via `poppler-glib`

* **Files**: `zathura/annotations-pdf.h`, `zathura/annotations-pdf.c`,
  `zathura/zathura.c` (load on open, clear on close, save on `:write`),
  `tests/test_annotations_pdf.c`, `meson.build`, `tests/meson.build`.
* **Scope**:
  * `zathura_annotations_load_pdf` reading `/Highlight` annotations (`PopplerAnnotTextMarkup`)
    from a PDF file via `poppler-glib`.
  * `zathura_annotations_save_pdf` writing all non-draft annotations into the PDF
    file via atomic temp-file write + `g_rename` (pausing `file_monitor`).
  * Sandboxed build guard (`#ifdef WITH_SANDBOX`) so `zathura-sandbox` compiles
    without filesystem write calls.
* **Unit test (`meson test -C build annotations_pdf`, headless)**:
  * Generate a minimal 2-page PDF in a temporary directory using Cairo's
    `cairo_pdf_surface_create` (`cairo-pdf.h`).
  * Add a yellow highlight, a red highlight, an open comment (`author = "Hao
    Chen"`, `body = "Check stats"`), and a resolved comment; call
    `zathura_annotations_save_pdf`.
  * Reopen the saved PDF in a fresh store with `zathura_annotations_load_pdf` and
    also inspect it directly with `poppler_document_new_from_gfile`; assert that
    page indices, rectangle coordinates, colors (yellow vs. red), `/Contents`
    (`body`), `/T` (`author`), `/NM` (`id`), and resolved state (`opacity == 0.34`
    vs. `0.35`) round-trip losslessly.
  * Modify the comment text, toggle `resolved`, delete one highlight, save again,
    and reopen to verify updates and deletions persist inside the PDF file.
  * Verify that draft comments (`draft == true`) are excluded from the saved PDF.
  * Test read-only file handling (`chmod 0444`): verify atomic save fails
    gracefully without corrupting the file, and `:write <writable-copy.pdf>`
    writes all in-memory annotations to the new path.

### Unit 3: Page-widget rendering, selection snapshot, and context popovers

* **Files**: `zathura/page-widget.h`, `zathura/page-widget.c`,
  `zathura/document-widget.c`, `tests/test_annotations_page.c`,
  `tests/meson.build`.
* **Scope**:
  * `selection_snapshot` with normalized bounds and deep-copied per-line
    rectangles surviving `leave-notify`.
  * `zathura_page_widget_draw_annotations(page_widget, cairo)` respecting
    `baked_in_surface`, active border, comment badge (filled vs. hollow), and
    tick-callback reveal pulse.
  * `annotation` `GSimpleActionGroup` and context popovers for new selections and
    existing annotations.
  * Stationary single-click activation in
    `cb_zathura_page_widget_button_release_event`.
* **Unit test (`meson test -C build xvfb_annotations_page`)**:
  * Render onto a `cairo_image_surface_t` (white background), attach yellow and
    red highlights (`baked_in_surface = false` and `baked_in_surface = true`) and
    open/resolved comments, call `zathura_page_widget_draw_annotations` at
    rotations `0`, `90`, `180`, `270` and scales `1.0` and `2.0`, and assert on
    the `CAIRO_FORMAT_ARGB32` pixel buffer that:
    * `baked_in_surface = false` shifts interior pixels to yellow or red while
      exterior pixels stay white;
    * `baked_in_surface = true` does not double-darken the interior fill when
      inactive, and draws the border and comment badge at the expected coordinates.
  * Populate `selection_snapshot`, emit a leave notification, and assert the
    snapshot stays valid.
  * Trigger `annotation.yellow`, `annotation.red`, `annotation.toggle-resolved`,
    and `annotation.delete` on a temporary PDF and verify both the store and the
    on-disk PDF file update immediately.

### Unit 4: Comment panel, event guards, commands, and two-way linkage

* **Files**: `zathura/annotation-panel.h`, `zathura/annotation-panel.c`,
  `girara-gtk/callbacks.c`, `zathura/commands.h`, `zathura/commands.c`,
  `zathura/shortcuts.h`, `zathura/shortcuts.c`, `zathura/config.c`,
  `data/zathura.css_t`, `doc/man/zathurarc.5.rst`,
  `tests/test_annotations_panel.c`, `tests/meson.build`.
* **Scope**:
  * `GtkPaned` container (`ui.view_box`), `ui.annotation_panel`, CSS rules in
    `data/zathura.css_t`.
  * Capture-phase guards in `girara-gtk/callbacks.c` for key, scroll, click, and
    motion events inside `.annotation-panel`.
  * Card list, `GtkTextView` editor with Save and Cancel, `Show resolved` filter,
    `:comments`, `Ctrl+Alt+C`, `comments-author`, `comments-width`.
  * `zathura_annotation_reveal` and `zathura_annotation_set_active`.
* **Unit test (`meson test -C build xvfb_annotations_panel`)**:
  * Initialize a `zathura_t` session with a temporary PDF, execute `:comments`
    (`cmd_comments`), and verify `ui.annotation_panel` visibility toggles.
  * Trigger `Add comment` to create a draft card, then trigger Cancel; verify the
    card is removed and the PDF on disk has 0 annotations.
  * Trigger `Add comment`, set buffer text `"Needs citation"`, trigger Save;
    reopen the PDF on disk with `poppler-glib` and verify it immediately contains
    the `/Highlight` annotation with `/Contents = "Needs citation"` and `/T`
    matching `comments-author`.
  * Edit the comment twice, resolve it, verify it hides when `Show resolved` is
    unchecked and reappears when checked, reopen it, and delete it; verify each
    step is reflected in the PDF file on disk.
  * Focus the panel's `GtkTextView` and a panel `GtkButton`, dispatch key press
    events (`Space`, `Return`, `'j'`, `'q'`) through the capture controller, and
    verify `girara_callback_view_key_press_event` returns `FALSE`.

## 9. Files changed

| File | Change | Unit |
| --- | --- | --- |
| `zathura/annotations.h`, `zathura/annotations.c` | Model, quad/rect coordinate math, in-memory store, reveal | 1, 2, 4 |
| `zathura/annotations-pdf.h`, `zathura/annotations-pdf.c` | Load and atomic save of PDF `/Highlight` annotations via `poppler-glib` | 2 |
| `zathura/page-widget.h`, `zathura/page-widget.c` | Selection snapshot, draw layer, pulse, popovers, actions, click | 3 |
| `zathura/document-widget.c` | Signal hookup if needed | 3 |
| `zathura/annotation-panel.h`, `zathura/annotation-panel.c` | Panel widget, cards, editor, filter, linkage | 4 |
| `girara-gtk/callbacks.c` | Capture-phase guards for key, scroll, click, motion inside panel | 4 |
| `zathura/zathura.h`, `zathura/zathura.c` | `ui.view_box`, `ui.annotation_panel`, PDF annotation load/clear/save | 2, 4 |
| `zathura/commands.h`, `zathura/commands.c` | `cmd_comments` (`:comments`) | 4 |
| `zathura/shortcuts.h`, `zathura/shortcuts.c` | `sc_toggle_comments`, `sc_comment_selection`, `ui.view_box` in index | 4 |
| `zathura/config.c` | `:comments`, `Ctrl+Alt+C`, `comments-author`, `comments-width` | 4 |
| `data/zathura.css_t` | `.annotation-panel` and `.annotation-card*` styles | 4 |
| `doc/man/zathurarc.5.rst` | Document `:comments`, `comments-author`, `comments-width` | 4 |
| `meson.build`, `tests/meson.build` | `poppler-glib` dependency, new sources, 4 unit test targets | 1 to 4 |
| `tests/test_annotations_model.c` | Unit 1 test suite | 1 |
| `tests/test_annotations_pdf.c` | Unit 2 test suite | 2 |
| `tests/test_annotations_page.c` | Unit 3 test suite | 3 |
| `tests/test_annotations_panel.c` | Unit 4 test suite | 4 |

## 10. Safeguards

* **Signed PDFs**: writing annotations into a PDF invalidates digital signatures.
  When `page_get_signatures` reports a signature on the document, zathura refuses
  automatic in-place writes and warns the user to use `:write <copy.pdf>` if they
  want to save an annotated copy.
* **Read-only files**: if the directory or PDF file is not writable, atomic
  in-place save notifies the user (`"Document is read-only; use :write <path> to
  save annotations"`) and keeps the annotations in memory so `:write` can save
  them elsewhere.
* **External reload (`latexmk` / Dropbox)**: pausing `zathura->file_monitor.monitor`
  around `g_rename` prevents self-triggered reloads while keeping automatic
  reloads working when an external process updates the PDF.
