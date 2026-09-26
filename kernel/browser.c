#include "browser.h"

// A tiny tag-aware HTML-to-text renderer. One line of output per block
// element, colours for headings and links, <script>/<style> hidden.

#define BR_TEXT  0x1F           // white on blue
#define BR_TITLE 0x1E           // yellow
#define BR_H1    0x1E           // yellow
#define BR_H2    0x1C           // light red
#define BR_LINK  0x1B           // light cyan

static int line_fresh;          // nothing printed on this line yet
static int pending_space;       // collapsed whitespace run

static void emit_nl(void)
{
    term_putc('\n');
    line_fresh = 1;
    pending_space = 0;
}

static void emit_char(char c)
{
    if (pending_space && !line_fresh)
        term_putc(' ');
    pending_space = 0;
    term_putc(c);
    line_fresh = 0;
}

static void emit_str(const char *s)
{
    while (*s)
        emit_char(*s++);
}

static int lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    return c;
}

static int eq(const char *a, const char *b)
{
    while (*a && *b && lower(*a) == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

// does the tag name at s (raw buffer) start with name, as a whole word?
static int tag_at(const char *s, const char *name)
{
    int k = 0;
    for (; name[k]; k++)
        if (lower(s[k]) != name[k])
            return 0;
    char c = s[k];
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

static int is_heading(const char *tag)
{
    return tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && !tag[2];
}

// the "<title>" line shown at the very top of the page
static void render_title(const char *html, uint32_t len)
{
    for (uint32_t i = 0; i + 7 < len; i++) {
        if (html[i] != '<' || !tag_at(html + i + 1, "title"))
            continue;
        uint32_t j = i + 6;
        while (j < len && html[j] != '>') j++;      // past attributes
        j++;                                        // and past the '>' itself
        term_setcolor(BR_TITLE);
        while (j < len && html[j] != '<') {
            char c = html[j++];
            if (c != '\n' && c != '\r' && c != '\t')
                term_putc(c);
        }
        term_setcolor(BR_TEXT);
        return;
    }
}

void browser_render(struct console *con, const char *html, uint32_t len,
                    const char *url)
{
    term_use(con);
    term_setcolor(BR_TEXT);
    term_clear();
    line_fresh = 1;
    pending_space = 0;

    if (url && url[0]) {             // the browser's address line
        term_setcolor(BR_LINK);
        term_puts("open: ");
        term_puts(url);
        term_putc('\n');
        term_setcolor(BR_TEXT);
        line_fresh = 1;
    }

    render_title(html, len);
    if (!line_fresh)
        emit_nl();
    emit_nl();

    char skip_tag[8] = { 0 };   // inside <script>/<style>: swallow all
    char href[96];              // link target currently being read
    int in_link = 0;

    uint32_t i = 0;
    while (i < len) {
        if (html[i] != '<') {
            char c = html[i++];
            if (skip_tag[0] || c == '\n' || c == '\r' || c == '\t')
                continue;
            if (c == '&') {                     // a few named entities
                if (len - i >= 3 && eq(html + i, "lt;"))  { emit_char('<');  i += 3; }
                else if (len - i >= 3 && eq(html + i, "gt;"))  { emit_char('>');  i += 3; }
                else if (len - i >= 4 && eq(html + i, "amp;")) { emit_char('&');  i += 4; }
                else if (len - i >= 5 && eq(html + i, "quot;")){ emit_char('"');  i += 5; }
                else emit_char('&');
                continue;
            }
            if (c == ' ') {
                pending_space = 1;
                continue;
            }
            emit_char(c);
            continue;
        }

        // a tag: pull out its name and attributes
        uint32_t j = i + 1;
        int closing = j < len && html[j] == '/';
        if (closing) j++;
        char name[8];
        int nn = 0;
        while (j < len && nn < 7 &&
               ((html[j] >= 'a' && html[j] <= 'z') || (html[j] >= 'A' && html[j] <= 'Z') ||
                html[j] == '1' || html[j] == '2' || html[j] == '3' ||
                html[j] == '4' || html[j] == '5' || html[j] == '6'))
            name[nn++] = (char)lower(html[j++]);
        name[nn] = 0;
        uint32_t attrs = j;
        while (j < len && html[j] != '>') j++;   // rest of the tag
        if (j >= len)
            break;
        i = j + 1;

        if (skip_tag[0]) {                       // only the close tag ends it
            if (closing && eq(skip_tag, name))
                skip_tag[0] = 0;
            continue;
        }
        if (!closing && (eq(name, "script") || eq(name, "style") ||
                         eq(name, "title"))) {   // title shown up top already
            for (int k = 0; k < 8 && name[k]; k++) skip_tag[k] = name[k];
            skip_tag[7] = 0;
            continue;
        }
        if (eq(name, "br")) {
            emit_nl();
        } else if (!closing && is_heading(name)) {
            if (!line_fresh) emit_nl();
            if (name[1] <= '2') emit_nl();       // air around big headings
            term_setcolor(name[1] == '1' ? BR_H1 : BR_H2);
        } else if (closing && is_heading(name)) {
            term_setcolor(in_link ? BR_LINK : BR_TEXT);
            emit_nl();
        } else if (eq(name, "p") || eq(name, "div") || eq(name, "li") ||
                   eq(name, "tr") || eq(name, "ul") || eq(name, "ol") ||
                   eq(name, "table") || eq(name, "blockquote")) {
            if (!line_fresh) emit_nl();
            if (!closing && (eq(name, "p") || eq(name, "ul") ||
                             eq(name, "ol") || eq(name, "table")))
                emit_nl();                       // blank line between blocks
        } else if (!closing && eq(name, "a")) {
            // fish href="..." out of the attribute text
            href[0] = 0;
            uint32_t k = attrs;
            while (k + 5 < j) {
                if (html[k] == 'h' && html[k+1] == 'r' && html[k+2] == 'e' &&
                    html[k+3] == 'f' && html[k+4] == '=') {
                    k += 5;
                    char q = html[k++];
                    if (q != '"' && q != '\'') q = ' ';
                    int n = 0;
                    while (k < j && html[k] != q && n < 95)
                        href[n++] = html[k++];
                    href[n] = 0;
                    break;
                }
                k++;
            }
            in_link = 1;
            term_setcolor(BR_LINK);
        } else if (closing && eq(name, "a")) {
            if (href[0]) {
                emit_str(" [");
                emit_str(href);
                emit_char(']');
            }
            in_link = 0;
            term_setcolor(BR_TEXT);
        }
    }
    if (!line_fresh)
        emit_nl();
    term_setcolor(BR_TEXT);
}
