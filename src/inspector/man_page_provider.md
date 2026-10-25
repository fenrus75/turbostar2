# `man_page_provider`

## Goals
- Provide authoritative signatures, argument prototypes, and one-sentence summaries for standard C and POSIX library functions directly from system manual pages in section 3 (`/usr/share/man/man3/`).
- Seamlessly collapse multi-line prototypes with indentations into a clean, single-line signature fitting the Symbol Inspector companion window.
- Extract concise summaries from the `.SH NAME` or `.SH DESCRIPTION` sections.
- Extract required C header `#include` files (e.g. `<stdlib.h>`, `<unistd.h>`) directly from the synopsis and present them inline alongside the declaration reference.
- Employ in-memory caching for sub-microsecond cache hits and negative caching for fast rejection of non-library tokens.
- Handle both compressed (`.3.gz`) and uncompressed (`.3`) man pages, as well as symlinks and `.so` redirect directives.

## Constraints
- Never blocks the UI thread for disk decompression; file existence is checked in < 0.05ms synchronously, while decompression and troff parsing run in background worker threads.
- Completely free of ncurses or terminal APIs.
- Emits clean Markdown Lite strings using `inspector::markdown_builder`.
- Returns score `80` (`inspector_scores::MAN_PAGE`) on success, and score `0` (`inspector_scores::INVALID`) when no man page exists.

## Lessons Learned
- In multi-function manual pages (e.g. `printf.3` defining `printf`, `fprintf`, `sprintf`, `snprintf`), scanning the synopsis for the specific token followed by an open parenthesis `(` ensures the exact signature for the queried function is returned.
- Multi-line prototypes frequently format parameter lists across several lines with varying tab indents; collapsing consecutive whitespace and trimming spaces around punctuation produces a crisp single-line prototype.
- Negative caching (recording symbols that do not exist in `man3/`) eliminates repeated filesystem stat calls as the user moves across project-local variables.
- Interleaved `#include` headers: Some multi-function man pages (e.g. `warn.3`) introduce additional headers (like `<stdarg.h>`) only before specific functions. Narrowing header search to the synopsis preceding the queried function ensures precise include suggestions.
