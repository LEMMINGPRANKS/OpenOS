#include "js.h"
#include "heap.h"
#include "term.h"

// --- tokens ---------------------------------------------------------------

enum { T_END, T_NUM, T_STR, T_IDENT, T_OP };   // T_OP: kind = the ascii char

struct tok {
    uint8_t kind;
    char op;                 // for T_OP
    int64_t num;             // for T_NUM
    char *str;               // for T_STR / T_IDENT
    int line;
};

#define MAX_TOKS 4096
static struct tok toks[MAX_TOKS];
static int ntoks;

static int lex_line;
static const char *jserr;
static int jserr_line;

static void fail(const char *msg)
{
    if (!jserr) {
        jserr = msg;
        jserr_line = lex_line;
    }
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static char *dup_str(const char *s, int len)
{
    char *d = kmalloc((uint32_t)len + 1);
    if (!d) {
        fail("out of memory");
        return 0;
    }
    for (int i = 0; i < len; i++)
        d[i] = s[i];
    d[len] = 0;
    return d;
}

static void lex(const char *src, uint32_t len)
{
    uint32_t i = 0;
    lex_line = 1;
    ntoks = 0;
    while (i < len && ntoks < MAX_TOKS - 1) {
        char c = src[i];
        if (c == '\n') { lex_line++; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
        if (c == '/' && i + 1 < len && src[i + 1] == '/') {
            while (i < len && src[i] != '\n') i++;
            continue;
        }
        struct tok *t = &toks[ntoks];
        t->line = lex_line;
        if (is_digit(c)) {
            int64_t v = 0;
            while (i < len && is_digit(src[i])) {
                v = v * 10 + (src[i] - '0');
                i++;
            }
            t->kind = T_NUM; t->num = v;
        } else if (is_alpha(c)) {
            uint32_t start = i;
            while (i < len && (is_alpha(src[i]) || is_digit(src[i]))) i++;
            t->kind = T_IDENT; t->str = dup_str(src + start, (int)(i - start));
        } else if (c == '"') {
            i++;
            uint32_t start = i;
            while (i < len && src[i] != '"' && src[i] != '\n') i++;
            t->kind = T_STR; t->str = dup_str(src + start, (int)(i - start));
            if (i < len && src[i] == '"') i++;
        } else if ((c == '=' || c == '!' || c == '<' || c == '>') &&
                   i + 1 < len && src[i + 1] == '=') {
            t->kind = T_OP;
            t->op = (char)(c == '=' ? 0xF0 : c == '!' ? 0xF1
                       : c == '<' ? 0xF2 : 0xF3);      // == != <= >=
            i += 2;
        } else if (c == '+' && i + 1 < len && src[i + 1] == '+') {
            t->kind = T_OP; t->op = (char)0xF4; i += 2;       // ++
        } else if (c == '-' && i + 1 < len && src[i + 1] == '-') {
            t->kind = T_OP; t->op = (char)0xF5; i += 2;       // --
        } else if (c == '+' && i + 1 < len && src[i + 1] == '=') {
            t->kind = T_OP; t->op = (char)0xF6; i += 2;       // +=
        } else if (c == '-' && i + 1 < len && src[i + 1] == '=') {
            t->kind = T_OP; t->op = (char)0xF7; i += 2;       // -=
        } else if (c == '&' && i + 1 < len && src[i + 1] == '&') {
            t->kind = T_OP; t->op = (char)0xE0; i += 2;       // &&
        } else if (c == '|' && i + 1 < len && src[i + 1] == '|') {
            t->kind = T_OP; t->op = (char)0xE1; i += 2;       // ||
        } else {
            t->kind = T_OP; t->op = c;
            i++;
        }
        if (jserr)
            return;
        ntoks++;
    }
}

// --- AST -------------------------------------------------------------------

enum {
    N_NUM, N_STR, N_IDENT, N_BIN, N_UN, N_CALL, N_MEMBER, N_INC,
    N_VAR, N_ASSIGN, N_EXPR, N_IF, N_WHILE, N_FOR, N_BLOCK,
    N_FUNC, N_RETURN, N_BREAK, N_CONTINUE, N_AND, N_OR
};

struct node {
    uint8_t kind;
    char op;                 // N_BIN / N_UN operator
    char *name;              // N_IDENT / N_VAR / N_FUNC / N_MEMBER member
    int64_t num;             // N_NUM
    char *str;               // N_STR
    struct node *a, *b, *c, *d;
    struct node *next;       // statement / argument lists
};

static struct node *new_node(int kind)
{
    struct node *n = kmalloc(sizeof *n);
    if (!n) {
        fail("out of memory");
        return 0;
    }
    n->kind = (uint8_t)kind;
    n->name = 0; n->str = 0; n->num = 0; n->op = 0;
    n->a = n->b = n->c = n->d = 0; n->next = 0;
    return n;
}

static int tp;               // token position

static struct tok *peek(void) { return &toks[tp]; }
static struct tok *next(void) { return &toks[tp++]; }

static int at_op(char op)
{
    struct tok *t = peek();
    return t->kind == T_OP && t->op == op;
}

static int at_kw(const char *kw)
{
    struct tok *t = peek();
    if (t->kind != T_IDENT)
        return 0;
    for (int i = 0; kw[i] || t->str[i]; i++)
        if (kw[i] != t->str[i])
            return 0;
    return 1;
}

static int eat_op(char op)
{
    if (at_op(op)) { tp++; return 1; }
    return 0;
}

static int eat_kw(const char *kw)
{
    if (at_kw(kw)) { tp++; return 1; }
    return 0;
}

static struct node *parse_block(void);
static struct node *parse_stmt(void);
static struct node *parse_expr(void);

static struct node *parse_primary(void)
{
    struct tok *t = peek();
    if (t->kind == T_NUM) {
        tp++;
        struct node *n = new_node(N_NUM);
        if (n) n->num = t->num;
        return n;
    }
    if (t->kind == T_STR) {
        tp++;
        struct node *n = new_node(N_STR);
        if (n) n->str = t->str;
        return n;
    }
    if (t->kind == T_IDENT) {
        tp++;
        struct node *n = new_node(N_IDENT);
        if (n) n->name = t->str;
        return n;
    }
    if (eat_op('(')) {
        struct node *e = parse_expr();
        if (!eat_op(')'))
            fail("expected )");
        return e;
    }
    fail("unexpected token in expression");
    return 0;
}

static struct node *parse_postfix(void)
{
    struct node *n = parse_primary();
    while (n && !jserr) {
        if (at_op('(')) {
            tp++;
            struct node *call = new_node(N_CALL);
            if (!call) return 0;
            call->a = n;
            struct node **tail = &call->b;
            while (!at_op(')')) {
                struct node *arg = parse_expr();
                *tail = arg;
                tail = &arg->next;
                if (!eat_op(','))
                    break;
            }
            if (!eat_op(')'))
                fail("expected ) in call");
            n = call;
        } else if (at_op('.')) {
            tp++;
            struct tok *m = next();
            if (m->kind != T_IDENT) {
                fail("expected name after .");
                return 0;
            }
            struct node *mem = new_node(N_MEMBER);
            if (!mem) return 0;
            mem->a = n;
            mem->name = m->str;
            n = mem;
        } else {
            break;
        }
    }
    return n;
}

static struct node *parse_unary(void)
{
    if (at_op('-')) {
        tp++;
        struct node *n = new_node(N_UN);
        if (n) { n->op = '-'; n->a = parse_unary(); }
        return n;
    }
    if (at_op('!')) {
        tp++;
        struct node *n = new_node(N_UN);
        if (n) { n->op = '!'; n->a = parse_unary(); }
        return n;
    }
    if (at_op((char)0xF4) || at_op((char)0xF5)) {    // ++x / --x (prefix)
        char op = next()->op;
        struct node *n = new_node(N_INC);
        if (n) { n->op = op; n->num = 1; n->a = parse_unary(); }
        return n;
    }
    struct node *n = parse_postfix();
    if (n && (at_op((char)0xF4) || at_op((char)0xF5))) {   // x++ / x--
        char op = next()->op;
        struct node *inc = new_node(N_INC);
        if (inc) { inc->op = op; inc->num = 0; inc->a = n; }
        return inc;
    }
    return n;
}

static struct node *bin(char op, struct node *a, struct node *b)
{
    struct node *n = new_node(N_BIN);
    if (n) { n->op = op; n->a = a; n->b = b; }
    return n;
}

static struct node *parse_mul(void)
{
    struct node *n = parse_unary();
    while (!jserr && (at_op('*') || at_op('/') || at_op('%'))) {
        char op = next()->op;
        n = bin(op, n, parse_unary());
    }
    return n;
}

static struct node *parse_add(void)
{
    struct node *n = parse_mul();
    while (!jserr && (at_op('+') || at_op('-'))) {
        char op = next()->op;
        n = bin(op, n, parse_mul());
    }
    return n;
}

static struct node *parse_cmp(void)
{
    struct node *n = parse_add();
    for (;;) {
        struct tok *t = peek();
        if (t->kind != T_OP)
            break;
        char op = t->op;                       // '<' '>' or <= >= (0xF2/0xF3)
        if (op != '<' && op != '>' && op != (char)0xF2 && op != (char)0xF3)
            break;
        tp++;
        n = bin(op, n, parse_add());
    }
    return n;
}

static struct node *parse_eq(void)
{
    struct node *n = parse_cmp();
    while (!jserr && (at_op((char)0xF0) || at_op((char)0xF1))) {   // == !=
        char op = next()->op;
        n = bin(op, n, parse_cmp());
    }
    return n;
}

static struct node *parse_and(void)
{
    struct node *n = parse_eq();
    while (!jserr && at_op((char)0xE0)) {              // &&
        tp++;
        struct node *e = new_node(N_AND);
        if (e) { e->a = n; e->b = parse_eq(); }
        n = e;
    }
    return n;
}

static struct node *parse_or(void)
{
    struct node *n = parse_and();
    while (!jserr && at_op((char)0xE1)) {              // ||
        tp++;
        struct node *e = new_node(N_OR);
        if (e) { e->a = n; e->b = parse_and(); }
        n = e;
    }
    return n;
}

static struct node *parse_expr(void)
{
    struct node *lhs = parse_or();
    if (jserr || !lhs)
        return lhs;
    if (at_op('=')) {
        tp++;
        struct node *n = new_node(N_ASSIGN);
        if (n) { n->a = lhs; n->b = parse_expr(); }   // right associative
        return n;
    }
    if (at_op((char)0xF6) || at_op((char)0xF7)) {     // += -= (sugar for x = x ± e)
        char op = next()->op;
        struct node *rhs = parse_expr();
        struct node *n = new_node(N_ASSIGN);
        if (n) { n->a = lhs; n->b = bin(op == (char)0xF6 ? '+' : '-', lhs, rhs); }
        return n;
    }
    return lhs;
}

static struct node *parse_stmt(void)
{
    if (eat_kw("var") || eat_kw("let") || eat_kw("const")) {
        struct tok *id = next();
        if (id->kind != T_IDENT) {
            fail("expected variable name");
            return 0;
        }
        struct node *n = new_node(N_VAR);
        if (!n) return 0;
        n->name = id->str;
        if (eat_op('='))
            n->a = parse_expr();
        if (!eat_op(';'))
            fail("expected ; after var");
        return n;
    }
    if (eat_kw("function")) {
        struct tok *id = next();
        if (id->kind != T_IDENT) {
            fail("expected function name");
            return 0;
        }
        struct node *n = new_node(N_FUNC);
        if (!n) return 0;
        n->name = id->str;
        if (!eat_op('('))
            fail("expected ( after function name");
        struct node **tail = &n->a;      // parameter list
        while (!at_op(')')) {
            struct tok *p = next();
            if (p->kind != T_IDENT) {
                fail("expected parameter name");
                return 0;
            }
            struct node *pn = new_node(N_IDENT);
            if (!pn) return 0;
            pn->name = p->str;
            *tail = pn;
            tail = &pn->next;
            if (!eat_op(','))
                break;
        }
        if (!eat_op(')'))
            fail("expected ) after parameters");
        n->b = parse_block();            // body
        return n;
    }
    if (eat_kw("return")) {
        struct node *n = new_node(N_RETURN);
        if (!n) return 0;
        if (!at_op(';'))
            n->a = parse_expr();
        if (!eat_op(';'))
            fail("expected ; after return");
        return n;
    }
    if (eat_kw("if")) {
        struct node *n = new_node(N_IF);
        if (!n) return 0;
        if (!eat_op('(')) { fail("expected ( after if"); return 0; }
        n->a = parse_expr();
        if (!eat_op(')')) { fail("expected ) after if condition"); return 0; }
        n->b = parse_stmt();
        if (eat_kw("else"))
            n->c = parse_stmt();
        return n;
    }
    if (eat_kw("while")) {
        struct node *n = new_node(N_WHILE);
        if (!n) return 0;
        if (!eat_op('(')) { fail("expected ( after while"); return 0; }
        n->a = parse_expr();
        if (!eat_op(')')) { fail("expected ) after while condition"); return 0; }
        n->b = parse_stmt();
        return n;
    }
    if (eat_kw("for")) {
        struct node *n = new_node(N_FOR);
        if (!n) return 0;
        if (!eat_op('(')) { fail("expected ( after for"); return 0; }
        n->a = parse_stmt();             // var i = 0;  (eats its ';')
        n->b = at_op(';') ? 0 : parse_expr();
        if (!eat_op(';')) { fail("expected ; in for"); return 0; }
        n->c = at_op(')') ? 0 : parse_expr();
        if (!eat_op(')')) { fail("expected ) after for"); return 0; }
        n->d = parse_stmt();
        return n;
    }
    if (eat_kw("break")) {
        if (!eat_op(';'))
            fail("expected ; after break");
        return new_node(N_BREAK);
    }
    if (eat_kw("continue")) {
        if (!eat_op(';'))
            fail("expected ; after continue");
        return new_node(N_CONTINUE);
    }
    if (at_op('{'))
        return parse_block();
    struct node *n = new_node(N_EXPR);
    if (!n) return 0;
    n->a = parse_expr();
    if (!eat_op(';'))
        fail("expected ; after expression");
    return n;
}

static struct node *parse_block(void)
{
    if (!eat_op('{')) {
        fail("expected {");
        return 0;
    }
    struct node *blk = new_node(N_BLOCK);
    if (!blk) return 0;
    struct node **tail = &blk->a;
    while (!jserr && !at_op('}')) {
        if (peek()->kind == T_END) {
            fail("missing }");
            return 0;
        }
        struct node *s = parse_stmt();
        *tail = s;
        tail = &s->next;
    }
    if (!eat_op('}')) {
        fail("missing }");
        return 0;
    }
    return blk;
}

// --- values ----------------------------------------------------------------

enum { V_NIL, V_NUM, V_STR, V_FN };

struct val {
    uint8_t t;
    int64_t n;
    char *s;
    struct node *fn;                     // N_FUNC node for V_FN
};

static struct val nil_val(void)
{
    struct val v;
    v.t = V_NIL; v.n = 0; v.s = 0; v.fn = 0;
    return v;
}

static struct val num_val(int64_t n)
{
    struct val v;
    v.t = V_NUM; v.n = n; v.s = 0; v.fn = 0;
    return v;
}

static struct val str_val(char *s)
{
    struct val v;
    v.t = V_STR; v.s = s; v.fn = 0; v.n = 0;
    return v;
}

#define MAX_GLOBALS 128
#define MAX_LOCALS  64
#define MAX_DEPTH   64

struct binding { char *name; struct val v; };
static struct binding globals[MAX_GLOBALS];
static int nglobals;

static struct binding locals[MAX_DEPTH][MAX_LOCALS];
static int nlocals[MAX_DEPTH];
static int depth;

static uint64_t steps;
#define STEP_BUDGET 20000000ULL

static struct binding *find_var(char *name)
{
    for (int d = depth; d >= 0; d--)
        for (int i = nlocals[d] - 1; i >= 0; i--) {
            struct binding *b = &locals[d][i];
            int same = 1;
            for (int j = 0; ; j++) {
                if (b->name[j] != name[j]) { same = 0; break; }
                if (!name[j]) break;
            }
            if (same)
                return b;
        }
    for (int i = 0; i < nglobals; i++) {
        struct binding *b = &globals[i];
        int same = 1;
        for (int j = 0; ; j++) {
            if (b->name[j] != name[j]) { same = 0; break; }
            if (!name[j]) break;
        }
        if (same)
            return b;
    }
    return 0;
}

static void set_var(char *name, struct val v)
{
    struct binding *b = find_var(name);
    if (b) {
        b->v = v;
        return;
    }
    if (depth >= 0 && nlocals[depth] < MAX_LOCALS) {
        b = &locals[depth][nlocals[depth]++];
    } else if (nglobals < MAX_GLOBALS) {
        b = &globals[nglobals++];
    } else {
        fail("too many variables");
        return;
    }
    b->name = name;
    b->v = v;
}

// --- printing helpers --------------------------------------------------------

static int str_eq(const char *a, const char *b)
{
    for (int i = 0; ; i++) {
        if (a[i] != b[i]) return 0;
        if (!a[i]) return 1;
    }
}

static void print_num(int64_t v)
{
    char digits[21];
    int n = 0;
    uint64_t u;
    if (v < 0) {
        term_putc('-');
        u = (uint64_t)(-(v + 1)) + 1;
    } else {
        u = (uint64_t)v;
    }
    if (!u) digits[n++] = '0';
    while (u) { digits[n++] = (char)('0' + u % 10); u /= 10; }
    while (n) term_putc(digits[--n]);
}

static char *num_to_str(int64_t v)
{
    char digits[21];
    int n = 0;
    uint64_t u = v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    if (!u) digits[n++] = '0';
    while (u) { digits[n++] = (char)('0' + u % 10); u /= 10; }
    if (v < 0)
        digits[n++] = '-';
    char *s = kmalloc((uint32_t)n + 1);
    if (!s) {
        fail("out of memory");
        return 0;
    }
    for (int i = 0; i < n; i++)
        s[i] = digits[n - 1 - i];
    s[n] = 0;
    return s;
}

static int val_true(struct val v)
{
    if (v.t == V_NUM) return v.n != 0;
    if (v.t == V_STR) return v.s[0] != 0;
    return 0;
}

static void print_val(struct val v, int *first)
{
    if (!*first)
        term_putc(' ');
    *first = 0;
    if (v.t == V_NUM)
        print_num(v.n);
    else if (v.t == V_STR)
        term_puts(v.s ? v.s : "");
    else if (v.t == V_FN)
        term_puts("[function]");
    else
        term_puts("undefined");
}

enum { RUN_OK, RUN_RETURN, RUN_BREAK, RUN_CONTINUE };

static struct val return_val;

static int run_stmt(struct node *n);
static struct val eval(struct node *n);
static struct val call_function(struct node *fn, struct node *args);

static void tick(void)
{
    if (++steps > STEP_BUDGET)
        fail("script ran too long (stopped to protect the machine)");
}

static struct val eval(struct node *n)
{
    struct val v = nil_val();
    if (!n || jserr)
        return v;
    tick();
    if (jserr)
        return v;
    switch (n->kind) {
    case N_NUM: return num_val(n->num);
    case N_STR: return str_val(n->str);
    case N_IDENT: {
        struct binding *b = find_var(n->name);
        if (!b) {
            fail("undefined variable");
            return v;
        }
        return b->v;
    }
    case N_UN: {
        struct val a = eval(n->a);
        if (jserr) return v;
        if (n->op == '-')
            return num_val(-a.n);
        return num_val(!val_true(a));
    }
    case N_BIN: {
        struct val a = eval(n->a);
        struct val b = eval(n->b);
        if (jserr) return v;
        if (n->op == '+') {
            if (a.t == V_STR || b.t == V_STR) {
                // string concatenation (numbers convert to text)
                char *sa = a.t == V_STR ? a.s : num_to_str(a.n);
                char *sb = b.t == V_STR ? b.s : num_to_str(b.n);
                if (jserr) return v;
                int la = 0, lb = 0;
                while (sa[la]) la++;
                while (sb[lb]) lb++;
                char *s = kmalloc((uint32_t)la + (uint32_t)lb + 1);
                if (!s) { fail("out of memory"); return v; }
                for (int i = 0; i < la; i++) s[i] = sa[i];
                for (int i = 0; i < lb; i++) s[la + i] = sb[i];
                s[la + lb] = 0;
                return str_val(s);
            }
            return num_val(a.n + b.n);
        }
        if (n->op == '-') return num_val(a.n - b.n);
        if (n->op == '*') return num_val(a.n * b.n);
        if (n->op == '/') {
            if (b.n == 0) { fail("division by zero"); return v; }
            return num_val(a.n / b.n);
        }
        if (n->op == '%') {
            if (b.n == 0) { fail("division by zero"); return v; }
            return num_val(a.n % b.n);
        }
        if (n->op == '<')  return num_val(a.n < b.n);
        if (n->op == '>')  return num_val(a.n > b.n);
        if (n->op == (char)0xF2) return num_val(a.n <= b.n);
        if (n->op == (char)0xF3) return num_val(a.n >= b.n);
        if (n->op == (char)0xF0) return num_val(a.t == b.t && a.n == b.n && a.s == b.s);
        if (n->op == (char)0xF1) return num_val(!(a.t == b.t && a.n == b.n && a.s == b.s));
        fail("unknown operator");
        return v;
    }
    case N_AND: {                                       // && short-circuits
        struct val a = eval(n->a);
        if (jserr) return v;
        if (!val_true(a))
            return num_val(0);
        return num_val(val_true(eval(n->b)));
    }
    case N_OR: {                                        // || short-circuits
        struct val a = eval(n->a);
        if (jserr) return v;
        if (val_true(a))
            return num_val(1);
        return num_val(val_true(eval(n->b)));
    }
    case N_INC: {
        if (n->a->kind != N_IDENT) {
            fail("++/-- needs a variable");
            return v;
        }
        struct binding *b = find_var(n->a->name);
        if (!b) {
            fail("undefined variable");
            return v;
        }
        int64_t old = b->v.n;
        int64_t nv = n->op == (char)0xF4 ? old + 1 : old - 1;
        b->v = num_val(nv);
        return num_val(n->num ? nv : old);      // prefix returns the new value
    }
    case N_ASSIGN: {
        struct val r = eval(n->b);
        if (jserr) return v;
        if (n->a->kind != N_IDENT) {
            fail("can only assign to variables");
            return v;
        }
        set_var(n->a->name, r);
        return r;
    }
    case N_CALL: {
        // console.log(...) -- the one supported member call
        if (n->a->kind == N_MEMBER && n->a->a->kind == N_IDENT) {
            if (str_eq(n->a->a->name, "console") && str_eq(n->a->name, "log")) {
                int first = 1;
                for (struct node *arg = n->b; arg && !jserr; arg = arg->next)
                    print_val(eval(arg), &first);
                term_putc('\n');
                return nil_val();
            }
            fail("only console.log is supported");
            return v;
        }
        if (n->a->kind == N_IDENT && str_eq(n->a->name, "print")) {
            int first = 1;
            for (struct node *arg = n->b; arg && !jserr; arg = arg->next)
                print_val(eval(arg), &first);
            term_putc('\n');
            return nil_val();
        }
        struct val f = eval(n->a);
        if (jserr) return v;
        if (f.t != V_FN) {
            fail("not a function");
            return v;
        }
        return call_function(f.fn, n->b);
    }
    default:
        fail("unexpected expression");
        return v;
    }
}

static int run_stmt(struct node *n)
{
    if (!n || jserr)
        return RUN_OK;
    tick();
    if (jserr)
        return RUN_OK;
    switch (n->kind) {
    case N_VAR:
        set_var(n->name, n->a ? eval(n->a) : nil_val());
        return RUN_OK;
    case N_ASSIGN:
    case N_EXPR:
        eval(n->a);
        return RUN_OK;
    case N_FUNC: {
        struct val f = nil_val();
        f.t = V_FN;
        f.fn = n;
        set_var(n->name, f);
        return RUN_OK;
    }
    case N_IF:
        if (val_true(eval(n->a))) {
            if (n->b) return run_stmt(n->b);
        } else if (n->c) {
            return run_stmt(n->c);
        }
        return RUN_OK;
    case N_WHILE:
        while (!jserr && val_true(eval(n->a))) {
            int code = run_stmt(n->b);
            if (code == RUN_BREAK) break;
            if (code == RUN_RETURN) return RUN_RETURN;
        }
        return RUN_OK;
    case N_FOR: {
        if (n->a) run_stmt(n->a);                 // var i = 0;
        while (!jserr && (n->b ? val_true(eval(n->b)) : 1)) {
            int code = run_stmt(n->d);
            if (code == RUN_BREAK) break;
            if (code == RUN_RETURN) return RUN_RETURN;
            if (n->c) eval(n->c);                 // i = i + 1
        }
        return RUN_OK;
    }
    case N_BLOCK:
        for (struct node *s = n->a; s && !jserr; s = s->next) {
            int code = run_stmt(s);
            if (code != RUN_OK)
                return code;                      // hand break/return upward
        }
        return RUN_OK;
    case N_RETURN:
        return_val = n->a ? eval(n->a) : nil_val();
        return RUN_RETURN;
    case N_BREAK:
        return RUN_BREAK;
    case N_CONTINUE:
        return RUN_CONTINUE;
    default:
        eval(n);
        return RUN_OK;
    }
}

static struct val call_function(struct node *fn, struct node *args)
{
    struct val v = nil_val();
    if (depth + 1 >= MAX_DEPTH) {
        fail("recursion too deep");
        return v;
    }
    depth++;
    nlocals[depth] = 0;
    struct node *p = fn->a;                       // parameters
    struct node *a = args;
    while (p && a) {
        struct val av = eval(a);
        if (jserr) { depth--; return v; }
        set_var(p->name, av);
        p = p->next;
        a = a->next;
    }
    for (; p; p = p->next)
        set_var(p->name, nil_val());              // missing args = undefined
    int code = RUN_OK;
    for (struct node *s = fn->b->a; s && !jserr; s = s->next) {
        code = run_stmt(s);
        if (code != RUN_OK)
            break;
    }
    if (code == RUN_RETURN)
        v = return_val;
    depth--;
    return v;
}

int js_run(const char *src, uint32_t len, char *err, int errmax)
{
    jserr = 0;
    jserr_line = 0;
    nglobals = 0;
    depth = -1;
    steps = 0;
    return_val = nil_val();

    lex(src, len);
    if (!jserr) {
        struct node *prog = new_node(N_BLOCK);
        if (prog) {
            struct node **tail = &prog->a;
            while (!jserr && peek()->kind != T_END) {
                struct node *s = parse_stmt();
                *tail = s;
                tail = &s->next;
            }
            for (struct node *s = prog->a; s && !jserr; s = s->next)
                if (run_stmt(s) != RUN_OK)
                    break;                        // top-level return: done
        }
    }
    if (!jserr)
        return 0;
    // "line N: message" into the caller's buffer
    int pos = 0;
    if (!jserr_line)
        jserr_line = 1;
    char lbuf[12];
    int ln = 0;
    uint32_t u = (uint32_t)jserr_line;
    if (!u) lbuf[ln++] = '0';
    while (u) { lbuf[ln++] = (char)('0' + u % 10); u /= 10; }
    const char *pre = "line ";
    for (int i = 0; pre[i] && pos < errmax - 1; i++) err[pos++] = pre[i];
    for (int i = ln - 1; i >= 0 && pos < errmax - 1; i--) err[pos++] = lbuf[i];
    const char *mid = ": ";
    for (int i = 0; mid[i] && pos < errmax - 1; i++) err[pos++] = mid[i];
    for (int i = 0; jserr[i] && pos < errmax - 1; i++) err[pos++] = jserr[i];
    err[pos] = 0;
    return -1;
}
