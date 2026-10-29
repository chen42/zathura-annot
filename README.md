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
  * Select text on a PDF page and right-click to open the annotation menu (`Highlight in yellow`, `Highlight in red`, `Add comment`, or `Remove highlight` if the selection overlaps an existing highlight).
  * Right-click an existing highlight to add or edit its comment, switch its color between yellow and red, mark it resolved or unresolved, or remove the highlight.
* **Comment Side Panel**:
  * Displays comment cards ordered by page and vertical position, with compact action buttons (`Page N`, `Edit`, `Red`/`Yellow`, `Resolve`/`Unresolve`, `Delete`) and a `Show resolved` filter.
  * Clicking a comment card scrolls to the corresponding page and pulses the highlight; clicking a highlight selects and focuses its card in the panel.
  * Hiding the comment panel keeps page highlights visible while hiding the comment indicator boxes and side panel.
* **Keyboard Shortcuts and Commands**:
  * `C` or `c`: Toggle the comment side panel (also available via `:comments [on|off|toggle]`).
  * `]` / `[`: Jump to the next or previous comment.
  * `Delete` / `BackSpace`: Remove the currently selected highlight or annotation.
  * `:annotations_save [path]`: Explicitly write current annotations to the PDF file (or a target path). When `annotation-autosave` is `true` (default), changes are saved atomically to the PDF file automatically.
* **Configuration (`zathurarc`)**:
  * `annotation-panel-visible` (`bool`, default `false`): Initial visibility of the comment side panel when opening a document.
  * `annotation-panel-width` (`int`, default `340`): Width of the comment side panel in pixels.
  * `annotation-default-color` (`string`, default `"yellow"`): Default highlight color for new comments (`"yellow"` or `"red"`).
  * `annotation-autosave` (`bool`, default `true`): Automatically save annotation changes back into the PDF file.

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
