#include "news.h"
#include "http.h"
#include "browser.h"
#include "term.h"

void news_fetch(struct console *con)
{
    term_use(con);
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    term_clear();
    term_puts("NEWS -- fetching the latest OpenOS updates...\n\n");
    if (http_ensure_net() != 0) {
        term_puts("\ncan't reach the network.\n");
        term_puts("(is the spgk server running on the host?)\n");
        return;
    }
    static uint8_t body[HTTP_MAX];
    int n = http_get("/news", body, HTTP_MAX);
    if (n == -2) {
        term_puts("\nthe server is up but has no news page yet\n");
        return;
    }
    if (n < 0) {
        term_puts("\nthe news server did not answer.\n");
        return;
    }
    browser_render(con, (const char *)body, (uint32_t)n, "news");
}
