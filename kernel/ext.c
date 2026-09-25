#include "ext.h"

#define COL_TEXT  0x0F          // white on black
#define COL_SRC   0x0A          // light green
#define COL_DISC  0x0E          // yellow
#define COL_ARCH  0x0B          // light cyan
#define COL_PROG  0x0C          // light red
#define COL_CONF  0x09          // light blue
#define COL_OTHER 0x07          // light grey

static const struct ext_type types[] = {
    { "txt",  "plain text",          COL_TEXT  },
    { "md",   "markdown",            COL_TEXT  },
    { "log",  "log file",            COL_TEXT  },
    { "c",    "C source",            COL_SRC   },
    { "h",    "C header",            COL_SRC   },
    { "cpp",  "C++ source",          COL_SRC   },
    { "hpp",  "C++ header",          COL_SRC   },
    { "s",    "assembly source",     COL_SRC   },
    { "asm",  "assembly source",     COL_SRC   },
    { "js",   "JavaScript source",   COL_SRC   },
    { "ts",   "TypeScript source",   COL_SRC   },
    { "py",   "Python source",       COL_SRC   },
    { "java", "Java source",         COL_SRC   },
    { "rs",   "Rust source",         COL_SRC   },
    { "go",   "Go source",           COL_SRC   },
    { "sh",   "shell script",        COL_SRC   },
    { "iso",  "disc image",          COL_DISC  },
    { "img",  "disc image",          COL_DISC  },
    { "dmg",  "disc image",          COL_DISC  },
    { "vhd",  "disc image",          COL_DISC  },
    { "tar",  "tar archive",         COL_ARCH  },
    { "gz",   "gzip archive",        COL_ARCH  },
    { "zip",  "zip archive",         COL_ARCH  },
    { "7z",   "7-zip archive",       COL_ARCH  },
    { "spgk", "spgk package",        COL_ARCH  },
    { "bin",  "program",             COL_PROG  },
    { "elf",  "program (ELF)",       COL_PROG  },
    { "exe",  "program",             COL_PROG  },
    { "cfg",  "config file",         COL_CONF  },
    { "ini",  "config file",         COL_CONF  },
    { "json", "data (JSON)",         COL_CONF  },
    { "",     "unknown",             COL_OTHER },
};

static const struct ext_type other = { "", "unknown", COL_OTHER };

static int ext_match(const char *name, const char *ext)
{
    int nl = 0;
    while (name[nl]) nl++;
    int el = 0;
    while (ext[el]) el++;
    if (el == 0)
        return 0;
    if (nl <= el)
        return 0;
    if (name[nl - el - 1] != '.')
        return 0;
    for (int i = 0; i < el; i++)
        if (name[nl - el + i] != ext[i])
            return 0;
    return 1;
}

const struct ext_type *ext_lookup(const char *name)
{
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++)
        if (types[i].ext[0] && ext_match(name, types[i].ext))
            return &types[i];
    // no extension at all?
    int nl = 0;
    while (name[nl]) nl++;
    for (int i = 0; i < nl; i++)
        if (name[i] == '.')
            return &other;           // has a dot but unknown type
    return &other;
}

int ext_is_text(const char *name)
{
    const struct ext_type *t = ext_lookup(name);
    return t->vga_color == COL_TEXT || t->vga_color == COL_SRC ||
           t->vga_color == COL_CONF;
}
