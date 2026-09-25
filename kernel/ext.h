#ifndef OPENOS_EXT_H
#define OPENOS_EXT_H

#include <stdint.h>

// File extension support: every file type OpenOS knows, with a colour
// (VGA attribute byte) and a human description. Unknown extensions
// fall into EXT_OTHER.

struct ext_type {
    const char *ext;        // "txt", "cpp", ... ("" = no extension)
    const char *desc;       // "plain text", "C++ source", ...
    uint8_t vga_color;      // TERM_COLOR_* style attr for listings
};

const struct ext_type *ext_lookup(const char *name); // never NULL
int  ext_is_text(const char *name);                  // viewable as text

#endif
