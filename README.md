zathura-annot - a document viewer with PDF highlight and comment annotations
============================================================================

`zathura-annot` is a modified fork of [zathura](https://github.com/pwmt/zathura), a customizable document viewer based on the `girara` user interface library and document plugins.

Origin and License Notice
-------------------------

This repository contains an altered version of upstream `zathura` (`Copyright (c) 2009-2026 pwmt.org`, https://github.com/pwmt/zathura) and is not the original upstream distribution. It is distributed under the zlib License; see `LICENSE` and `AUTHORS` for upstream copyright and attribution details.

Note: The compiled binary retains the name `zathura` so that existing document plugins (such as `zathura-pdf-poppler` in `/usr/lib/zathura`) and `~/.config/zathura/zathurarc` configuration files work without modification.

Added Features in zathura-annot
-------------------------------

`zathura-annot` adds PDF-native text highlighting and comment annotations stored directly inside the PDF file as standard `/Highlight` annotation objects via `poppler-glib`:

* **Text Highlighting and Comments**:
  * Highlights without comments are rendered in yellow (configurable via `annotation-highlight-color`), and highlights with comments are rendered in red (configurable via `annotation-comment-color`).
  * Selecting text on a PDF page automatically opens the annotation menu (`Highlight`, `Add comment`, or `Remove highlight` if the selection overlaps an existing highlight) upon releasing the mouse button.
  * Right-click an existing highlight to add or edit its comment, or remove the highlight.
* **Comment Side Panel**:
  * Displays comment cards ordered by page and vertical position, with a per-card collapse/expand toggle (`▾` / `▸`), a top-right `×` delete button, compact bottom action buttons (`Page N`, `Edit`), and a global `Roll up` / `Expand all` button in the header.
  * Includes a live search/filter bar (`Filter comments...` with `Clear` and `Export` buttons) that filters comments case-insensitively by word or phrase as you type.
  * Clicking `Export` (or running `:comments export [path]`) exports all currently shown comments (respecting the active filter) as a single-line-per-comment TSV table (`page`, `quote`, `comment`) to `<pdf-basename>-comments.tsv` (or a custom `.tsv`/`.csv` path) and copies the table to the system clipboard.
  * Rolling up comments collapses cards to show only the first line of each comment; individual comments can still be expanded or collapsed one by one (via the `▾` / `▸` button or by double-clicking a comment card).
  * Rolling the mouse wheel while hovering over the comment panel scrolls the comment list without scrolling the PDF page.
  * Clicking a comment card selects the annotation; clicking a highlight selects and focuses its card in the panel.
  * Hiding the comment panel keeps page highlights visible.
  * A bottom horizontal scrollbar (`guioptions` default `"sh"`) and horizontal scrolling (`h` / `l` or `Shift` + mouse wheel) allow moving the PDF page left or right on wide monitors so the comment panel does not cover page text.
* **Keyboard Shortcuts and Commands**:
  * `Ctrl+Alt+c`: Toggle the comment side panel (also available via `:comments [open|close|toggle|rollup|expand|filter <phrase>|clear-filter|export [path]]`).
  * `Delete` / `BackSpace`: Remove the currently selected highlight or annotation.
  * `Ctrl+Enter` / `Esc` (inside the inline comment editor): Save or cancel the comment edit.
* **Configuration (`zathurarc`)**:
  * `annotation-panel-open` (`bool`, default `false`): Open the comment side panel automatically when opening a document.
  * `annotation-panel-width` (`int`, default `340`): Width of the comment side panel in pixels.
  * `annotation-highlight-color` (`string`, default `"rgba(255,237,26,0.40)"`): Color for highlights without comments.
  * `annotation-highlight-active-color` (`string`, default `"rgba(255,247,5,0.52)"`): Color for active highlights without comments.
  * `annotation-comment-color` (`string`, default `"rgba(255,77,77,0.36)"`): Color for highlights with comments.
  * `annotation-comment-active-color` (`string`, default `"rgba(255,56,56,0.48)"`): Color for active highlights with comments.

Requirements
------------

The following dependencies are required:

* `gtk4` (>= 4.12)
* `glib` (>= 2.84)
* `girara` (>= 2026.07.07)
* `poppler-glib` (for reading and writing PDF `/Highlight` annotations)
* `libmagic` from file(1): for mime-type detection
* `json-glib`
* `sqlite3` (>= 3.25.0): sqlite3 database backend
* `libxxhash`: file hashing

The following dependencies are optional:
* `libsynctex` from TeXLive (>= 2): SyncTeX support
* `libseccomp`: sandbox support

For building `zathura-annot`, the following dependencies are also required:

* `meson` (>= 1.5)
* `gettext`
* `pkgconf`

The following dependencies are optional build-time only dependencies:

* `librvsg-bin`: PNG icons
* `Sphinx`: manpages and HTML documentation
* `doxygen`: HTML documentation
* `breathe`: for HTML documentation
* `sphinx_rtd_theme`: for HTML documentation

Note that `Sphinx` is needed to build the manpages. If it is not installed, the
man pages won't be built. For building the HTML documentation, `doxygen`,
`breathe` and `sphinx_rtd_theme` are needed in addition to `Sphinx`.

The use of `libseccomp` and/or `landlock` to create a sandboxed environment is
optional and can be disabled by configuring the build system with
`-Dseccomp=disabled` and `-Dlandlock=disabled`. The sandboxed version of zathura
will be built into a separate binary named `zathura-sandbox`. Strict sandbox
mode will reduce the available functionality of zathura and provide a read only
document viewer.

Installation
------------

To build and install `zathura-annot` using meson's ninja backend:

    meson setup build
    ninja -C build
    ninja -C build install

To run the compiled binary directly from `build/` using system-installed document plugins:

    ZATHURA_PLUGINS_PATH=/usr/lib/zathura ./build/zathura /path/to/file.pdf

> **Note:** The default backend for meson might vary based on the platform. Please
refer to the meson documentation for platform specific dependencies.

Upstream Project
----------------

Upstream `zathura` is developed at https://github.com/pwmt/zathura.
