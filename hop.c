/*
 * hop.c — Minimal Hope language interpreter
 *
 * Supports lists, pairs, sections, juxtaposition application,
 * pattern matching, if-then-else, where/whererec, lazy evaluation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include "fenster.h"
#define FENSTER_AUDIO_BUFSZ 1024
#include "fenster_audio.h"

static jmp_buf err_jmp;
static int err_recovery;  /* if set, longjmp instead of exit */

/* ===== Token types ===== */
enum {
    T_NUM, T_ID, T_STR,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_MOD,
    T_LT, T_GT, T_EQ, T_LE, T_GE, T_NE,
    T_LPAREN, T_RPAREN, T_COMMA, T_SEMI,
    T_COLON, T_ARROW, T_VALOF, T_FUN, T_IS,
    T_CONS, T_DOTDOT, T_BAR, T_BAR2, T_APPEND,
    T_LBRACKET, T_RBRACKET, T_HASH,
    T_NOT, T_AND, T_OR,
    T_EOF
};

typedef struct {
    FILE *src; int ch, tok, pb_valid, pb_tok;
    double tok_num, pb_num;
    char tok_id[128], pb_id[128], tok_str[4096];
    const char *tok_id_interned, *pb_id_interned;
    int src_line, src_col, tok_line, tok_col;
    int cur_end_line, cur_end_col, prev_end_line, prev_end_col;
    const char *src_file;
} LexState;

static LexState L = { .src_file = "", .src_line = 1, .tok_line = 1, .cur_end_line = 1, .prev_end_line = 1 };

#define src             L.src
#define ch              L.ch
#define tok             L.tok
#define tok_num         L.tok_num
#define tok_id          L.tok_id
#define tok_str         L.tok_str
#define pb_valid        L.pb_valid
#define pb_tok          L.pb_tok
#define pb_num          L.pb_num
#define pb_id           L.pb_id
#define tok_id_interned L.tok_id_interned
#define pb_id_interned  L.pb_id_interned
#define src_line        L.src_line
#define src_col         L.src_col
#define src_file        L.src_file
#define tok_line        L.tok_line
#define tok_col         L.tok_col
#define cur_end_line    L.cur_end_line
#define cur_end_col     L.cur_end_col
#define prev_end_line   L.prev_end_line
#define prev_end_col    L.prev_end_col

_Noreturn static void die(const char *msg) {
    fprintf(stderr, "%s(%d:%d) %s\n", src_file, tok_line, tok_col, msg);
    if (err_recovery) longjmp(err_jmp, 1);
    exit(1);
}
_Noreturn static void dief(const char *fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    die(buf);
}

#define ALLOC(T) ((T *)calloc(1, sizeof(T)))

/* ===== String Interning ===== */
#define INTERN_BUCKETS 1024
typedef struct InternEntry { const char *str; struct InternEntry *next; } InternEntry;
static InternEntry *intern_buckets[INTERN_BUCKETS];

static unsigned intern_hash(const char *s) {
    unsigned h = 5381;
    for (; *s; s++) h = h * 33 + (unsigned char)*s;
    return h;
}

static const char *intern(const char *s) {
    unsigned h = intern_hash(s) % INTERN_BUCKETS;
    for (InternEntry *e = intern_buckets[h]; e; e = e->next)
        if (!strcmp(e->str, s)) return e->str;
    InternEntry *e = malloc(sizeof(InternEntry));
    e->str = strdup(s);
    e->next = intern_buckets[h];
    intern_buckets[h] = e;
    return e->str;
}

/* Pre-interned symbol names */
static const char *S_then, *S_else, *S_where, *S_whererec, *S_dec, *S_uses;
static const char *S_nil, *S_true, *S_false, *S_if, *S_mod, *S_fun;
static const char *S_not, *S_and, *S_or;
static const char *S_typevar, *S_infix, *S_infixr, *S_data, *S_abstype, *S_type, *S_private, *S_write;
static const char *S_sin, *S_cos, *S_tan, *S_asin, *S_acos, *S_atan, *S_atan2;
static const char *S_exp, *S_log, *S_log10, *S_sqrt, *S_pow, *S_floor, *S_ceil, *S_fabs, *S_pi;
static const char *S_map, *S_head, *S_tail, *S_succ, *S_front, *S_length, *S_nth;
static const char *S_xor, *S_fst, *S_snd, *S_rand, *S_srand, *S_timeseed;
static const char *S_array, *S_aget, *S_alen, *S_aset, *S_amake, *S_tabulate, *S_amap;
static const char *S_gopen, *S_gclose, *S_gplot, *S_gclear, *S_gloop, *S_gsync;
static const char *S_gwidth, *S_gheight, *S_gmouse, *S_gkey, *S_gline, *S_gcircle;
static const char *S_gtext, *S_gscale, *S_gpen, *S_gtitle, *S_gblit, *S_gdrawcol;
static const char *S_gkeyevent, *S_gbeep, *S_gclick, *S_gsave, *S_grestore;
static const char *S_underscore;

static void intern_init(void) {
    S_then=intern("then"); S_else=intern("else"); S_where=intern("where"); S_whererec=intern("whererec");
    S_dec=intern("dec"); S_uses=intern("uses"); S_nil=intern("nil"); S_true=intern("true"); S_false=intern("false");
    S_if=intern("if"); S_mod=intern("mod"); S_fun=intern("fun"); S_not=intern("not"); S_and=intern("and"); S_or=intern("or");
    S_typevar=intern("typevar"); S_infix=intern("infix"); S_infixr=intern("infixr");
    S_data=intern("data"); S_abstype=intern("abstype"); S_type=intern("type"); S_private=intern("private"); S_write=intern("write");
    S_sin=intern("sin"); S_cos=intern("cos"); S_tan=intern("tan");
    S_asin=intern("asin"); S_acos=intern("acos"); S_atan=intern("atan"); S_atan2=intern("atan2");
    S_exp=intern("exp"); S_log=intern("log"); S_log10=intern("log10"); S_sqrt=intern("sqrt"); S_pow=intern("pow");
    S_floor=intern("floor"); S_ceil=intern("ceil"); S_fabs=intern("fabs"); S_pi=intern("pi");
    S_map=intern("map"); S_head=intern("head"); S_tail=intern("tail"); S_succ=intern("succ");
    S_front=intern("front"); S_length=intern("length"); S_nth=intern("nth");
    S_xor=intern("xor"); S_fst=intern("fst"); S_snd=intern("snd");
    S_rand=intern("rand"); S_srand=intern("srand"); S_timeseed=intern("timeseed");
    S_array=intern("array"); S_aget=intern("aget"); S_alen=intern("alen"); S_aset=intern("aset");
    S_amake=intern("amake"); S_tabulate=intern("tabulate"); S_amap=intern("amap");
    S_gopen=intern("gopen"); S_gclose=intern("gclose"); S_gplot=intern("gplot"); S_gclear=intern("gclear");
    S_gloop=intern("gloop"); S_gsync=intern("gsync"); S_gwidth=intern("gwidth"); S_gheight=intern("gheight");
    S_gmouse=intern("gmouse"); S_gkey=intern("gkey"); S_gline=intern("gline"); S_gcircle=intern("gcircle");
    S_gtext=intern("gtext"); S_gscale=intern("gscale"); S_gpen=intern("gpen"); S_gtitle=intern("gtitle");
    S_gblit=intern("gblit"); S_gdrawcol=intern("gdrawcol"); S_gkeyevent=intern("gkeyevent");
    S_gbeep=intern("gbeep"); S_gclick=intern("gclick"); S_gsave=intern("gsave"); S_grestore=intern("grestore");
    S_underscore=intern("_");
}

/* ===== Builtin Registry ===== */
enum { BF_FUN = 1, BF_GFX = 2, BF_PRIO = 4 };
#define BUILTIN_HASH_SIZE 128
typedef struct { const char *name; int flags; } BuiltinInfo;
static BuiltinInfo builtin_hash[BUILTIN_HASH_SIZE];

static void builtin_register(const char *name, int flags) {
    unsigned h = ((uintptr_t)name >> 3) % BUILTIN_HASH_SIZE;
    while (builtin_hash[h].name) {
        if (builtin_hash[h].name == name) { builtin_hash[h].flags |= flags; return; }
        h = (h + 1) % BUILTIN_HASH_SIZE;
    }
    builtin_hash[h].name = name;
    builtin_hash[h].flags = flags;
}

static int builtin_flags(const char *name) {
    unsigned h = ((uintptr_t)name >> 3) % BUILTIN_HASH_SIZE;
    while (builtin_hash[h].name) {
        if (builtin_hash[h].name == name) return builtin_hash[h].flags;
        h = (h + 1) % BUILTIN_HASH_SIZE;
    }
    return 0;
}

static void builtin_init(void) {
    /* math builtins (priority - can't be shadowed by user bindings) */
    const char *math_funs[] = { S_sin, S_cos, S_tan, S_asin, S_acos, S_atan, S_atan2,
                                 S_exp, S_log, S_log10, S_sqrt, S_pow, S_floor, S_ceil, S_fabs, NULL };
    for (const char **p = math_funs; *p; p++) builtin_register(*p, BF_FUN | BF_PRIO);
    builtin_register(S_pi, BF_PRIO);  /* constant, not BF_FUN */

    /* core builtins */
    const char *core_funs[] = { S_map, S_head, S_tail, S_succ, S_front, S_length, S_nth,
                                 S_xor, S_fst, S_snd, S_rand, S_srand,
                                 S_array, S_aget, S_alen, S_aset, S_amake, S_tabulate, S_amap, NULL };
    for (const char **p = core_funs; *p; p++) builtin_register(*p, BF_FUN);

    /* graphics builtins */
    const char *gfx_funs[] = { S_gopen, S_gclose, S_gplot, S_gclear, S_gloop, S_gsync,
                                S_gwidth, S_gheight, S_gmouse, S_gkey, S_gline, S_gcircle,
                                S_gtext, S_gscale, S_gpen, S_gtitle, S_gblit, S_gdrawcol,
                                S_gkeyevent, S_gbeep, S_gclick, S_gsave, S_grestore, NULL };
    for (const char **p = gfx_funs; *p; p++) builtin_register(*p, BF_FUN | BF_GFX);
}

/* ===== Values (with lazy thunks) ===== */
typedef struct Val Val;
typedef struct Env Env;
typedef struct Expr Expr;
typedef struct Pat Pat;
typedef Val *(*CThunkFn)(void *);

enum { V_NUM, V_NIL, V_CONS, V_PAIR, V_SEC, V_FUN, V_THUNK, V_ARR };
struct Val {
    int    tag;
    char   gc_mark;
    union {
        double num;                                /* V_NUM */
        struct { Val *hd, *tl; } cons;             /* V_CONS */
        struct { Val *fst, *snd; } pair;           /* V_PAIR */
        int sec_op;                                /* V_SEC */
        const char *name;                          /* V_FUN */
        struct {                                   /* V_THUNK */
            Expr *expr;
            Env  *env;
            CThunkFn fn;
            void *data;
        } thunk;
        struct { Val **data; int len; } arr;       /* V_ARR */
    } u;
};

#define ARR(v)    ((v)->u.arr.data)
#define ARRLEN(v) ((v)->u.arr.len)

struct Env { const char *name; Val *val; Env *next; char gc_mark; };

/* ===== Simple GC: pool allocator + mark-sweep ===== */
#define VAL_POOL_SIZE  (1 << 18)   /* 256K Val nodes (~24MB) */
#define ENV_POOL_SIZE  (1 << 18)   /* 256K Env nodes */

static Val val_pool[VAL_POOL_SIZE];
static int val_pool_used = 0;
static Val *val_free_list = NULL;

static Env env_pool[ENV_POOL_SIZE];
static int env_pool_used = 0;
static Env *env_free_list = NULL;

static void *gc_stack_base = NULL;
static int gc_val_allocs = 0;
static int gc_threshold = 200000;

static int is_val_ptr(void *p) {
    return (char*)p >= (char*)val_pool &&
           (char*)p < (char*)(val_pool + val_pool_used) &&
           ((size_t)((char*)p - (char*)val_pool) % sizeof(Val)) == 0;
}
static int is_env_ptr(void *p) {
    return (char*)p >= (char*)env_pool &&
           (char*)p < (char*)(env_pool + env_pool_used) &&
           ((size_t)((char*)p - (char*)env_pool) % sizeof(Env)) == 0;
}

static void gc_mark_val(Val *v);
static void gc_mark_env(Env *e) {
    for (; e; e = e->next) {
        if (!is_env_ptr(e)) return;
        if (e->gc_mark) return;
        e->gc_mark = 1;
        if (e->val) gc_mark_val(e->val);
    }
}
static void gc_mark_val(Val *v) {
    if (!v || !is_val_ptr(v) || v->gc_mark) return;
    v->gc_mark = 1;
    switch (v->tag) {
    case V_CONS: gc_mark_val(v->u.cons.hd); gc_mark_val(v->u.cons.tl); break;
    case V_PAIR: gc_mark_val(v->u.pair.fst); gc_mark_val(v->u.pair.snd); break;
    case V_THUNK:
        gc_mark_env(v->u.thunk.env);
        if (v->u.thunk.data) {
            /* scan thunk_data (MapD/ZipD) conservatively — 4 words */
            void **words = (void **)v->u.thunk.data;
            for (int i = 0; i < 4; i++) {
                if (is_val_ptr(words[i])) gc_mark_val((Val*)words[i]);
                if (is_env_ptr(words[i])) gc_mark_env((Env*)words[i]);
            }
        }
        break;
    case V_ARR:
        for (int i = 0; i < ARRLEN(v); i++) gc_mark_val(ARR(v)[i]);
        break;
    }
}

static void (*gc_mark_extra_roots)(void) = NULL;

static void gc_collect(void) {
    /* clear marks */
    for (int i = 0; i < val_pool_used; i++) val_pool[i].gc_mark = 0;
    for (int i = 0; i < env_pool_used; i++) env_pool[i].gc_mark = 0;

    /* flush registers to stack via setjmp */
    jmp_buf regs;
    (void)setjmp(regs);
    /* also scan the jmp_buf itself */
    void **rb = (void**)&regs;
    for (size_t i = 0; i < sizeof(regs)/(sizeof(void*)); i++) {
        if (is_val_ptr(rb[i])) gc_mark_val((Val*)rb[i]);
        if (is_env_ptr(rb[i])) gc_mark_env((Env*)rb[i]);
    }

    /* conservative stack scan */
    void *stack_top = __builtin_frame_address(0);
    void **lo, **hi;
    if (stack_top < gc_stack_base)
        { lo = (void**)stack_top; hi = (void**)gc_stack_base; }
    else
        { lo = (void**)gc_stack_base; hi = (void**)stack_top; }
    for (void **p = lo; p < hi; p++) {
        if (is_val_ptr(*p)) gc_mark_val((Val*)*p);
        if (is_env_ptr(*p)) gc_mark_env((Env*)*p);
    }

    /* mark extra roots (e.g. cached 0-arg function values) */
    if (gc_mark_extra_roots) gc_mark_extra_roots();

    /* sweep Vals */
    val_free_list = NULL;
    int val_alive = 0;
    for (int i = 0; i < val_pool_used; i++) {
        if (!val_pool[i].gc_mark) {
            if (val_pool[i].tag == V_ARR && val_pool[i].u.arr.data)
                free(val_pool[i].u.arr.data);
            if (val_pool[i].tag == V_THUNK && val_pool[i].u.thunk.data)
                free(val_pool[i].u.thunk.data);
            *(Val**)&val_pool[i] = val_free_list;
            val_free_list = &val_pool[i];
        } else val_alive++;
    }
    /* sweep Envs */
    env_free_list = NULL;
    for (int i = 0; i < env_pool_used; i++) {
        if (!env_pool[i].gc_mark) {
            *(Env**)&env_pool[i] = env_free_list;
            env_free_list = &env_pool[i];
        }
    }
    gc_val_allocs = 0;
    gc_threshold = val_alive * 2 + 10000;
}

static Val *val_alloc(void) {
    gc_val_allocs++;
    if (val_free_list) {
        Val *v = val_free_list;
        val_free_list = *(Val**)v;
        memset(v, 0, sizeof(Val));
        return v;
    }
    if (val_pool_used >= VAL_POOL_SIZE) {
        gc_collect();
        if (val_free_list) return val_alloc();
        die("out of memory (Val pool)");
    }
    return &val_pool[val_pool_used++];  /* already zeroed (static) */
}

static Env *env_alloc(void) {
    if (env_free_list) {
        Env *e = env_free_list;
        env_free_list = *(Env**)e;
        memset(e, 0, sizeof(Env));
        return e;
    }
    if (env_pool_used >= ENV_POOL_SIZE) {
        gc_collect();
        if (env_free_list) return env_alloc();
        die("out of memory (Env pool)");
    }
    return &env_pool[env_pool_used++];
}

static Val val_nil_s = { .tag = V_NIL };
static Val *val_nil = &val_nil_s;

static Val *vnum(double n) {
    Val *v = val_alloc(); v->tag = V_NUM; v->u.num = n; return v;
}
static Val *vcons(Val *h, Val *t) {
    Val *v = val_alloc(); v->tag = V_CONS; v->u.cons.hd = h; v->u.cons.tl = t; return v;
}
static Val *vpair(Val *a, Val *b) {
    Val *v = val_alloc(); v->tag = V_PAIR; v->u.pair.fst = a; v->u.pair.snd = b; return v;
}
static Val *vsec(int op) {
    Val *v = val_alloc(); v->tag = V_SEC; v->u.sec_op = op; return v;
}
static Val *vfun(const char *n) {
    Val *v = val_alloc(); v->tag = V_FUN; v->u.name = n; return v;
}
static Val *mkthunk_e(Expr *expr, Env *env) {
    Val *v = val_alloc(); v->tag = V_THUNK;
    v->u.thunk.expr = expr; v->u.thunk.env = env; return v;
}
static Val *mkthunk_c(CThunkFn fn, void *data) {
    Val *v = val_alloc(); v->tag = V_THUNK;
    v->u.thunk.fn = fn; v->u.thunk.data = data; return v;
}

static Val *eval(Expr *e, Env *env);

static Val *force(Val *v) {
    while (v->tag == V_THUNK) {
        void *data = v->u.thunk.data;
        Val *r;
        if (v->u.thunk.fn) r = v->u.thunk.fn(data);
        else               r = eval(v->u.thunk.expr, v->u.thunk.env);
        *v = *r;
        free(data); /* free MapD/ZipD/RangeD (NULL is safe) */
    }
    return v;
}

/* write_chars: print a char list or a lazy list of char lists (strings) */
static void write_chars(Val *v) {
    while ((v = force(v))->tag == V_CONS) {
        Val *h = force(v->u.cons.hd);
        if (h->tag == V_CONS || h->tag == V_NIL)
            write_chars(h);   /* nested string */
        else
            putchar((int)h->u.num);
        v = v->u.cons.tl;
    }
}

static void print_val(Val *v);
static void print_list(Val *v) {
    print_val(v->u.cons.hd);
    Val *t = force(v->u.cons.tl);
    if (t->tag == V_CONS) { printf(", "); print_list(t); }
}
static int is_string(Val *v) {
    for (Val *p = v; p->tag == V_CONS; p = force(p->u.cons.tl)) {
        Val *h = force(p->u.cons.hd);
        if (h->tag != V_NUM) return 0;
        long c = (long)h->u.num;
        if (h->u.num != c || c < 32 || c > 255) return 0;
    }
    return v->tag == V_CONS; /* non-empty */
}
static void print_val(Val *v) {
    v = force(v);
    switch (v->tag) {
    case V_NUM:
        if (v->u.num == (long)v->u.num && v->u.num > -1e15 && v->u.num < 1e15)
            printf("%ld", (long)v->u.num);
        else printf("%.15g", v->u.num);
        break;
    case V_NIL:  printf("[]"); break;
    case V_CONS:
        if (is_string(v)) {
            printf("\"");
            for (Val *p=v; p->tag==V_CONS; p=force(p->u.cons.tl))
                putchar((int)force(p->u.cons.hd)->u.num);
            printf("\"");
        } else { printf("["); print_list(v); printf("]"); }
        break;
    case V_PAIR: printf("("); print_val(v->u.pair.fst); printf(", ");
                 print_val(v->u.pair.snd); printf(")"); break;
    case V_SEC:  printf("<section>"); break;
    case V_FUN:  printf("<function %s>", v->u.name); break;
    case V_ARR:
        printf("[|");
        for (int i = 0; i < ARRLEN(v); i++) {
            if (i) printf(", ");
            print_val(ARR(v)[i]);
        }
        printf("|]"); break;
    default: break;
    }
}

/* ===== Lexer ===== */
static char src_dir[1024];  /* directory of main source file */

static void lex_save(LexState *s) { *s = L; }
static void lex_restore(const LexState *s) { L = *s;
}

#define MAX_MODULES 64
static char *loaded_modules[MAX_MODULES];
static int n_loaded;

static void readch(void) {
    if (ch == '\n') { src_line++; src_col = 0; }
    ch = fgetc(src);
    src_col++;
}

static const char *tok_name(int t) {
    switch (t) {
    case T_NUM: return "number";
    case T_ID: return "identifier";
    case T_STR: return "string";
    case T_PLUS: return "'+'";
    case T_MINUS: return "'-'";
    case T_STAR: return "'*'";
    case T_SLASH: return "'/'";
    case T_MOD: return "'mod'";
    case T_LT: return "'<'";
    case T_GT: return "'>'";
    case T_EQ: return "'=='";
    case T_LE: return "'<='";
    case T_GE: return "'>='";
    case T_NE: return "'!='";
    case T_LPAREN: return "'('";
    case T_RPAREN: return "')'";
    case T_COMMA: return "','";
    case T_SEMI: return "';'";
    case T_COLON: return "':'";
    case T_ARROW: return "'->'";
    case T_VALOF: return "'---'";
    case T_FUN: return "'fun'";
    case T_IS: return "'='";
    case T_CONS: return "'::'";
    case T_DOTDOT: return "'..'";
    case T_BAR: return "'|'";
    case T_BAR2: return "'||'";
    case T_NOT: return "'not'";
    case T_AND: return "'and'";
    case T_OR:  return "'or'";
    case T_APPEND: return "'<>'";
    case T_LBRACKET: return "'['";
    case T_RBRACKET: return "']'";
    case T_HASH: return "'#'";
    case T_EOF: return "end of file";
    default: return "unknown";
    }
}

static int scan_escape(void) {
    readch();
    switch (ch) {
    case 'n': return '\n'; case 't': return '\t';
    case '\\': return '\\'; case '\'': return '\''; case '"': return '"';
    default: return ch;
    }
}

static void scan_inner(void) {
    for (;;) {
        while (ch != EOF && isspace(ch)) readch();
        if (ch == '!') {
            int pk = fgetc(src);
            if (pk == '=') { readch(); tok=T_NE; return; }
            if (pk != EOF) ungetc(pk, src);
            while (ch != '\n' && ch != EOF) readch(); continue;
        }
        break;
    }
    if (ch == EOF) { tok = T_EOF; tok_line = src_line; tok_col = src_col; return; }

    tok_line = src_line; tok_col = src_col;
    if (isdigit(ch)) {
        tok = T_NUM; tok_num = 0;
        while (isdigit(ch)) { tok_num = tok_num * 10 + ch - '0'; readch(); }
        if (ch == '.') {
            int pk = fgetc(src);
            if (pk != EOF && isdigit(pk)) {
                ch = pk; double f = 0.1;
                tok_num += (ch-'0')*f; f*=0.1; readch();
                while (isdigit(ch)) { tok_num += (ch-'0')*f; f*=0.1; readch(); }
            } else { if (pk != EOF) ungetc(pk, src); }
        }
        return;
    }
    if (isalpha(ch) || ch == '_') {
        tok = T_ID; int i = 0;
        while (isalnum(ch) || ch == '_' || ch == '\'') { if (i<126) tok_id[i++]=ch; readch(); }
        tok_id[i] = 0;
        tok_id_interned = intern(tok_id);
        if (tok_id_interned == S_mod) tok = T_MOD;
        if (tok_id_interned == S_fun) tok = T_FUN;
        if (tok_id_interned == S_not) tok = T_NOT;
        if (tok_id_interned == S_and) tok = T_AND;
        if (tok_id_interned == S_or)  tok = T_OR;
        return;
    }
    switch (ch) {
    case '\'': {
        readch();
        int cv = (ch == '\\') ? scan_escape() : ch;
        readch();
        if (ch == '\'') readch();
        tok = T_NUM; tok_num = cv; return;
    }
    case '"': {
        readch(); int i = 0;
        while (ch != '"' && ch != EOF) {
            tok_str[i++] = (ch == '\\') ? scan_escape() : ch;
            readch();
        }
        tok_str[i]=0; if (ch=='"') readch();
        tok = T_STR; return;
    }
    case '+': tok=T_PLUS;  readch(); return;
    case '*': tok=T_STAR;  readch(); return;
    case '/': tok=T_SLASH; readch(); return;
    case '(': tok=T_LPAREN;  readch(); return;
    case ')': tok=T_RPAREN;  readch(); return;
    case '[': tok=T_LBRACKET; readch(); return;
    case ']': tok=T_RBRACKET; readch(); return;
    case ',': tok=T_COMMA; readch(); return;
    case ';': tok=T_SEMI;  readch(); return;
    case ':': readch();
        if (ch==':') { readch(); tok=T_CONS; return; }
        tok=T_COLON; return;
    case '.': readch();
        if (ch=='.') { readch(); tok=T_DOTDOT; return; }
        die("unexpected '.'");
    case '|': readch();
        if (ch=='|') { readch(); tok=T_BAR2; return; }
        tok=T_BAR; return;
    case '>': readch();
        if (ch=='=') { readch(); tok=T_GE; return; }
        tok=T_GT; return;
    case '=': readch();
        if (ch=='=') { readch(); tok=T_EQ; return; }
        tok=T_IS; return;
    case '-': readch();
        if (ch=='-') { readch(); if (ch=='-'){readch();tok=T_VALOF;return;} die("unexpected '--'"); }
        if (ch=='>') { readch(); tok=T_ARROW; return; }
        tok=T_MINUS; return;
    case '<': readch();
        if (ch=='=') { readch(); tok=T_LE; return; }
        if (ch=='>') { readch(); tok=T_APPEND; return; }
        tok=T_LT; return;
    case '#': readch(); tok=T_HASH; return;
    case '@': readch(); tok=T_HASH; return;  /* reuse token, only skipped */
    case '`': readch(); tok=T_HASH; return;
    default:
        tok_line=src_line; tok_col=src_col;
        dief("unexpected character: '%c'",ch);
    }
}

static void scan(void) {
    if (pb_valid) {
        pb_valid = 0; tok = pb_tok; tok_num = pb_num;
        memcpy(tok_id, pb_id, sizeof(tok_id)); tok_id_interned = pb_id_interned; return;
    }
    prev_end_line = cur_end_line; prev_end_col = cur_end_col;
    scan_inner();
    cur_end_line = src_line; cur_end_col = src_col;
}

static void pushback(void) {
    pb_valid=1; pb_tok=tok; pb_num=tok_num;
    memcpy(pb_id, tok_id, sizeof(tok_id)); pb_id_interned = tok_id_interned;
}
static void expect(int t) {
    if (tok!=t) {
        fprintf(stderr,"%s(%d:%d) unexpected %s, expected %s\n",src_file,tok_line,tok_col,tok_name(tok),tok_name(t));
        if (err_recovery) longjmp(err_jmp, 1);
        exit(1);
    }
    scan();
}

/* ===== AST ===== */
enum { E_NUM, E_VAR, E_NIL, E_SEC, E_PAIR, E_APP, E_BIN, E_NEG, E_IF,
       E_WHERE, E_WHEREREC, E_COMP, E_NOT };
#define MAX_ARGS 16

struct Expr {
    int    tag;
    int    line, col;         /* source position */
    const char *file;         /* source file */
    double num;
    const char *name;
    int    op;
    Expr  *l, *r;
    Expr  *cond, *then_e, *else_e;
    int    nargs;
    Expr  *args[MAX_ARGS];
    Pat   *wpat;              /* E_WHERE: pattern for destructuring */
};

static Expr *mkexpr(int t) { Expr *e = ALLOC(Expr); e->tag=t; e->line=tok_line; e->col=tok_col; e->file=src_file; return e; }
static Expr *e_num(double n) { Expr *e=mkexpr(E_NUM); e->num=n; return e; }
static Expr *e_var(const char *s) { Expr *e=mkexpr(E_VAR); e->name=intern(s); return e; }
static Expr *e_nil(void) { return mkexpr(E_NIL); }
static Expr *e_sec(int op) { Expr *e=mkexpr(E_SEC); e->op=op; return e; }
static Expr *e_pair(Expr *a, Expr *b) { Expr *e=mkexpr(E_PAIR); e->l=a; e->r=b; return e; }
static Expr *e_bin(int op, Expr *l, Expr *r) { Expr *e=mkexpr(E_BIN); e->op=op; e->l=l; e->r=r; return e; }
static Expr *e_neg(Expr *x) { Expr *e=mkexpr(E_NEG); e->l=x; return e; }
static Expr *e_not(Expr *x) { Expr *e=mkexpr(E_NOT); e->l=x; return e; }
static Expr *e_if(Expr *c, Expr *t, Expr *el) { Expr *e=mkexpr(E_IF); e->cond=c; e->then_e=t; e->else_e=el; return e; }
static Expr *e_app(Expr *fn, Expr **a, int n) {
    Expr *e=mkexpr(E_APP); e->l=fn; e->nargs=n;
    for (int i=0;i<n;i++) e->args[i]=a[i]; return e;
}
static Expr *e_where(int rec, Expr *body, const char *nm, Expr *def, Pat *wp) {
    Expr *e = mkexpr(rec ? E_WHEREREC : E_WHERE);
    e->l = body; if (nm) e->name = intern(nm); e->r = def; e->wpat = wp; return e;
}

/* ===== Patterns (types) ===== */
enum { P_NUM, P_VAR, P_NIL, P_CONS, P_PAIR, P_WILD };
struct Pat {
    int tag; double num; const char *var;
    Pat *hd, *tl, *fst, *snd;
};
static Pat *p_num(double n) { Pat *p=ALLOC(Pat); p->tag=P_NUM; p->num=n; return p; }
static Pat *p_var(const char *s) { Pat *p=ALLOC(Pat); p->tag=P_VAR; p->var=intern(s); return p; }
static Pat *p_nil(void) { Pat *p=ALLOC(Pat); p->tag=P_NIL; return p; }
static Pat *p_wild(void) { Pat *p=ALLOC(Pat); p->tag=P_WILD; return p; }
static Pat *p_cons(Pat *h, Pat *t) { Pat *p=ALLOC(Pat); p->tag=P_CONS; p->hd=h; p->tl=t; return p; }
static Pat *p_pair(Pat *a, Pat *b) { Pat *p=ALLOC(Pat); p->tag=P_PAIR; p->fst=a; p->snd=b; return p; }

/* ===== Parser ===== */
static Expr *parse_expr(void);
static Expr *parse_or(void);
static Expr *parse_and(void);
static Expr *parse_cons(void);
static Expr *parse_append(void);
static Pat *parse_pat(void);

static int is_atom_start(void) {
    if (tok==T_NUM||tok==T_LPAREN||tok==T_LBRACKET||tok==T_STR) return 1;
    if (tok==T_ID)
        return tok_id_interned != S_then && tok_id_interned != S_else &&
               tok_id_interned != S_where && tok_id_interned != S_whererec &&
               tok_id_interned != S_dec && tok_id_interned != S_uses;
    return 0;
}

static Expr *parse_atom(void) {
    if (tok==T_NUM) { Expr *e=e_num(tok_num); scan(); return e; }
    if (tok==T_ID) {
        if (tok_id_interned == S_if) {
            scan(); Expr *c=parse_or();
            if (tok!=T_ID||tok_id_interned != S_then) die("expected 'then'");
            scan(); Expr *t=parse_or();
            if (tok!=T_ID||tok_id_interned != S_else) die("expected 'else'");
            scan(); return e_if(c,t,parse_or());
        }
        if (tok_id_interned == S_nil)  { scan(); return e_nil(); }
        if (tok_id_interned == S_true) { scan(); return e_num(1); }
        if (tok_id_interned == S_false){ scan(); return e_num(0); }
        Expr *e=e_var(tok_id); scan(); return e;
    }
    if (tok==T_LPAREN) {
        scan();
        if (tok==T_PLUS||tok==T_STAR||tok==T_SLASH||tok==T_MOD||
            tok==T_CONS||tok==T_MINUS||
            tok==T_LT||tok==T_GT||tok==T_EQ||tok==T_LE||tok==T_GE||tok==T_NE) {
            int op=tok; scan();
            if (tok==T_RPAREN) { scan(); return e_sec(op); }
            if (op==T_MINUS) { Expr *e=e_neg(parse_expr()); expect(T_RPAREN); return e; }
            die("invalid section");
        }
        Expr *e=parse_expr();
        if (tok==T_COMMA) { scan(); Expr *e2=parse_expr(); expect(T_RPAREN); return e_pair(e,e2); }
        expect(T_RPAREN); return e;
    }
    if (tok==T_LBRACKET) {
        scan();
        if (tok==T_RBRACKET) { scan(); return e_nil(); }
        Expr *el[256]; int n=0;
        el[n++]=parse_expr();
        if (tok==T_BAR) {
            /* list comprehension: [output | gen1, gen2, ..., guard] */
            scan(); /* consume '|' */
            Expr *comp=mkexpr(E_COMP);
            comp->l=el[0];    /* output expr */
            comp->r=e_num(1); /* default guard: true */
            comp->nargs=0;
            while (1) {
                if (tok==T_ID) {
                    const char *vn = intern(tok_id); scan();
                    if (tok==T_LT) {
                        scan(); /* consume '<' */
                        if (tok==T_MINUS) {
                            /* generator: vn <- range */
                            scan(); /* consume '-' */
                            if (comp->nargs+2>MAX_ARGS) die("too many generators");
                            comp->args[comp->nargs++]=e_var(vn);
                            comp->args[comp->nargs++]=parse_cons();
                            if (tok==T_COMMA) { scan(); continue; }
                            break;
                        }
                        /* guard: vn < rhs */
                        comp->r=e_bin(T_LT,e_var(vn),parse_append());
                        break;
                    } else if (tok==T_GT||tok==T_EQ||tok==T_LE||tok==T_GE||tok==T_NE) {
                        int op=tok; scan();
                        comp->r=e_bin(op,e_var(vn),parse_append());
                        break;
                    } else {
                        /* function application guard or bare variable */
                        Expr *gfn=e_var(vn);
                        Expr *gargs[MAX_ARGS]; int gn=0;
                        while (is_atom_start()&&gn<MAX_ARGS) gargs[gn++]=parse_atom();
                        Expr *gexpr=(gn==0)?gfn:e_app(gfn,gargs,gn);
                        if (tok==T_LT||tok==T_GT||tok==T_EQ||tok==T_LE||tok==T_GE||tok==T_NE) {
                            int op=tok; scan();
                            gexpr=e_bin(op,gexpr,parse_append());
                        }
                        comp->r=gexpr;
                        break;
                    }
                } else {
                    comp->r=parse_cons();
                    break;
                }
            }
            expect(T_RBRACKET);
            return comp;
        }
        while (tok==T_COMMA) { scan(); el[n++]=parse_expr(); }
        expect(T_RBRACKET);
        Expr *lst=e_nil();
        for (int i=n-1;i>=0;i--) lst=e_bin(T_CONS,el[i],lst);
        return lst;
    }
    if (tok==T_STR) {
        Expr *lst=e_nil();
        for (int i=(int)strlen(tok_str)-1;i>=0;i--)
            lst=e_bin(T_CONS,e_num((unsigned char)tok_str[i]),lst);
        scan(); return lst;
    }
    die("unexpected token in expression");
    return NULL;
}

static Expr *parse_app(void) {
    Expr *fn=parse_atom();
    Expr *a[MAX_ARGS]; int n=0;
    while (is_atom_start() && n<MAX_ARGS) a[n++]=parse_atom();
    if (n==0) return fn;
    return e_app(fn,a,n);
}
static Expr *parse_unary(void) {
    if (tok==T_MINUS) { scan(); return e_neg(parse_unary()); }
    if (tok==T_NOT)   { scan(); return e_not(parse_unary()); }
    return parse_app();
}
static Expr *parse_mul(void) {
    Expr *e=parse_unary();
    while (tok==T_STAR||tok==T_SLASH||tok==T_MOD) { int op=tok; scan(); e=e_bin(op,e,parse_unary()); }
    return e;
}
static Expr *parse_add(void) {
    Expr *e=parse_mul();
    while (tok==T_PLUS||tok==T_MINUS) { int op=tok; scan(); e=e_bin(op,e,parse_mul()); }
    return e;
}
static Expr *parse_range(void) {
    Expr *e=parse_add();
    if (tok==T_DOTDOT) { scan(); e=e_bin(T_DOTDOT,e,parse_add()); }
    return e;
}
static Expr *parse_zip(void) {
    Expr *e=parse_range();
    while (tok==T_BAR2) { scan(); e=e_bin(T_BAR2,e,parse_range()); }
    return e;
}
static Expr *parse_append(void) {
    Expr *e=parse_zip();
    while (tok==T_APPEND) { scan(); e=e_bin(T_APPEND,e,parse_zip()); }
    return e;
}
static Expr *parse_cmp(void) {
    Expr *e=parse_append();
    if (tok==T_LT||tok==T_GT||tok==T_EQ||tok==T_LE||tok==T_GE||tok==T_NE) { int op=tok; scan(); e=e_bin(op,e,parse_append()); }
    return e;
}
static Expr *parse_cons(void) {
    Expr *e=parse_cmp();
    if (tok==T_CONS) { scan(); e=e_bin(T_CONS,e,parse_cons()); }
    return e;
}
static Expr *parse_and(void) {
    Expr *e=parse_cons();
    while (tok==T_AND) { scan(); e=e_bin(T_AND,e,parse_cons()); }
    return e;
}
static Expr *parse_or(void) {
    Expr *e=parse_and();
    while (tok==T_OR) { scan(); e=e_bin(T_OR,e,parse_and()); }
    return e;
}
static Expr *parse_expr(void) {
    Expr *e = parse_or();
    while (tok==T_ID && (tok_id_interned == S_where || tok_id_interned == S_whererec)) {
        int rec = tok_id_interned == S_whererec;
        scan();
        if (rec) {
            /* whererec: simple name only (needed for circular thunk) */
            if (tok!=T_ID) die("expected name after whererec");
            const char *nm = intern(tok_id); scan();
            expect(T_IS);
            Expr *def = parse_cons();
            e = e_where(1, e, nm, def, NULL);
        } else {
            /* where: support pattern destructuring */
            Pat *wp = parse_pat();
            expect(T_IS);
            Expr *def = parse_cons();
            if (wp->tag == P_VAR)
                e = e_where(0, e, wp->var, def, NULL);
            else
                e = e_where(0, e, NULL, def, wp);
        }
    }
    return e;
}

/* ===== Pattern parsing ===== */
static Pat *parse_pat_atom(void) {
    if (tok==T_NUM) { Pat *p=p_num(tok_num); scan(); return p; }
    if (tok==T_ID && tok_id_interned == S_nil) { scan(); return p_nil(); }
    if (tok==T_ID && tok_id_interned == S_underscore) { scan(); return p_wild(); }
    if (tok==T_ID) { Pat *p=p_var(tok_id); scan(); return p; }
    if (tok==T_LBRACKET) {
        scan();
        if (tok==T_RBRACKET) { scan(); return p_nil(); }
        Pat *el[64]; int n=0;
        el[n++]=parse_pat();
        while (tok==T_COMMA) { scan(); el[n++]=parse_pat(); }
        expect(T_RBRACKET);
        Pat *lst=p_nil();
        for (int i=n-1;i>=0;i--) lst=p_cons(el[i],lst);
        return lst;
    }
    if (tok==T_LPAREN) {
        scan(); Pat *p=parse_pat();
        if (tok==T_COMMA) { scan(); Pat *p2=parse_pat(); expect(T_RPAREN); return p_pair(p,p2); }
        expect(T_RPAREN); return p;
    }
    die("unexpected token in pattern");
    return NULL;
}
static Pat *parse_pat(void) {
    Pat *p=parse_pat_atom();
    if (tok==T_CONS) { scan(); p=p_cons(p,parse_pat()); }
    return p;
}

/* ===== Function table ===== */
typedef struct Branch { int nargs; Pat *pats[MAX_ARGS]; Expr *body; struct Branch *next; } Branch;
typedef struct Func { const char *name; Branch *branches; struct Func *next; Val *cached; } Func;
static Func *funcs;

static void gc_mark_func_cache(void) {
    for (Func *f = funcs; f; f = f->next)
        if (f->cached) gc_mark_val(f->cached);
}

static Func *find_func(const char *name) {
    for (Func *f=funcs;f;f=f->next) if (f->name == name) return f;
    return NULL;
}
static void add_branch(const char *fn, int na, Pat **pats, Expr *body) {
    Func *f=find_func(fn);
    if (!f) { f=ALLOC(Func); f->name=intern(fn); f->next=funcs; funcs=f; }
    Branch *b=ALLOC(Branch); b->nargs=na;
    for (int i=0;i<na;i++) b->pats[i]=pats[i];
    b->body=body;
    Branch **pp=&f->branches; while(*pp) pp=&(*pp)->next; *pp=b;
}

/* ===== Evaluator ===== */
static int match_pat(Pat *p, Val *v, Env **env) {
    /* P_VAR and P_WILD must NOT force — lazy tails stay as thunks */
    if (p->tag == P_VAR) {
        Env *ne=env_alloc();
        ne->name=p->var; ne->val=v; ne->next=*env; *env=ne;
        return 1;
    }
    if (p->tag == P_WILD) return 1;
    v = force(v);
    switch (p->tag) {
    case P_NUM:  return v->tag==V_NUM && v->u.num==p->num;
    case P_NIL:  return v->tag==V_NIL;
    case P_CONS: return v->tag==V_CONS && match_pat(p->hd,v->u.cons.hd,env) && match_pat(p->tl,v->u.cons.tl,env);
    case P_PAIR: return v->tag==V_PAIR && match_pat(p->fst,v->u.pair.fst,env) && match_pat(p->snd,v->u.pair.snd,env);
    }
    return 0;
}

/* ===== Fenster graphics state ===== */
#define GFX_MAX_W 1920
#define GFX_MAX_H 1080
static uint32_t gfx_buf[GFX_MAX_W * GFX_MAX_H];
static uint32_t gfx_snap[GFX_MAX_W * GFX_MAX_H];
static struct fenster gfx_f;
static int gfx_open;
static int64_t gfx_last_time;
static int gfx_scale = 2;  /* font scale factor (default 2x) */
static int gfx_pen = 1;    /* pen width (default 1px) */
static int gfx_prev_keys[256] = {0};

/* ===== Audio ===== */
static struct fenster_audio gfx_audio;
static int audio_open = 0;
static double beep_freq = 0;
static double beep_phase = 0;
static int beep_remain = 0;   /* samples remaining */

static void audio_feed(void) {
    if (!audio_open) return;
    int avail = fenster_audio_available(&gfx_audio);
    if (avail <= 0) return;
    float buf[512];
    double step = 2.0 * 3.14159265358979 * beep_freq / FENSTER_SAMPLE_RATE;
    while (avail > 0) {
        int n = avail > 512 ? 512 : avail;
        for (int i = 0; i < n; i++) {
            if (beep_remain > 0) {
                buf[i] = (float)(0.3 * sin(beep_phase));
                beep_phase += step;
                beep_remain--;
            } else {
                buf[i] = 0.0f;
            }
        }
        fenster_audio_write(&gfx_audio, buf, n);
        avail -= n;
    }
}

static void gfx_plot(int x, int y, uint32_t c) {
    if (x>=0 && x<gfx_f.width && y>=0 && y<gfx_f.height)
        fenster_pixel(&gfx_f, x, y) = c;
}

/* plot a filled square of size pen×pen centered at (x,y) */
static void gfx_plot_pen(int x, int y, uint32_t c) {
    int r = gfx_pen / 2;
    for (int dy=-r; dy<gfx_pen-r; dy++)
        for (int dx=-r; dx<gfx_pen-r; dx++)
            gfx_plot(x+dx, y+dy, c);
}

static void gfx_line(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx=abs(x1-x0), dy=-abs(y1-y0);
    int sx=x0<x1?1:-1, sy=y0<y1?1:-1;
    int e=dx+dy;
    for (;;) {
        gfx_plot_pen(x0,y0,c);
        if (x0==x1 && y0==y1) break;
        int e2=2*e;
        if (e2>=dy) { e+=dy; x0+=sx; }
        if (e2<=dx) { e+=dx; y0+=sy; }
    }
}

static void gfx_circle(int cx, int cy, int r, uint32_t c) {
    int x=0, y=r, d=3-2*r;
    while (x<=y) {
        gfx_plot_pen(cx+x,cy+y,c); gfx_plot_pen(cx-x,cy+y,c);
        gfx_plot_pen(cx+x,cy-y,c); gfx_plot_pen(cx-x,cy-y,c);
        gfx_plot_pen(cx+y,cy+x,c); gfx_plot_pen(cx-y,cy+x,c);
        gfx_plot_pen(cx+y,cy-x,c); gfx_plot_pen(cx-y,cy-x,c);
        if (d<0) d+=4*x+6; else { d+=4*(x-y)+10; y--; }
        x++;
    }
}

/* 5x7 dot matrix font for ASCII 32..126, stored as 7 bytes per glyph (MSB=col4) */
static const uint8_t font5x7[][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*   */
    {0x04,0x04,0x04,0x04,0x00,0x04,0x00}, /* ! */
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, /* # */
    {0x04,0x0F,0x05,0x0E,0x14,0x0F,0x04}, /* $ */
    {0x13,0x0B,0x08,0x04,0x1A,0x19,0x00}, /* % */
    {0x06,0x09,0x06,0x15,0x09,0x16,0x00}, /* & */
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, /* ( */
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, /* ) */
    {0x00,0x0A,0x04,0x1F,0x04,0x0A,0x00}, /* * */
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x04,0x04,0x02}, /* , */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x04,0x00}, /* . */
    {0x10,0x08,0x08,0x04,0x02,0x02,0x01}, /* / */
    {0x0E,0x11,0x19,0x15,0x13,0x11,0x0E}, /* 0 */
    {0x04,0x06,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x10,0x08,0x04,0x02,0x1F}, /* 2 */
    {0x0E,0x11,0x10,0x0C,0x10,0x11,0x0E}, /* 3 */
    {0x08,0x0C,0x0A,0x09,0x1F,0x08,0x08}, /* 4 */
    {0x1F,0x01,0x0F,0x10,0x10,0x11,0x0E}, /* 5 */
    {0x0C,0x02,0x01,0x0F,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x10,0x08,0x04,0x04,0x04,0x04}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x1E,0x10,0x08,0x06}, /* 9 */
    {0x00,0x04,0x00,0x00,0x04,0x00,0x00}, /* : */
    {0x00,0x04,0x00,0x00,0x04,0x04,0x02}, /* ; */
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, /* < */
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* = */
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, /* > */
    {0x0E,0x11,0x10,0x08,0x04,0x00,0x04}, /* ? */
    {0x0E,0x11,0x1D,0x15,0x1D,0x01,0x0E}, /* @ */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
    {0x0F,0x11,0x11,0x0F,0x11,0x11,0x0F}, /* B */
    {0x0E,0x11,0x01,0x01,0x01,0x11,0x0E}, /* C */
    {0x0F,0x11,0x11,0x11,0x11,0x11,0x0F}, /* D */
    {0x1F,0x01,0x01,0x0F,0x01,0x01,0x1F}, /* E */
    {0x1F,0x01,0x01,0x0F,0x01,0x01,0x01}, /* F */
    {0x0E,0x11,0x01,0x19,0x11,0x11,0x0E}, /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
    {0x1C,0x08,0x08,0x08,0x08,0x09,0x06}, /* J */
    {0x11,0x09,0x05,0x03,0x05,0x09,0x11}, /* K */
    {0x01,0x01,0x01,0x01,0x01,0x01,0x1F}, /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x13,0x15,0x19,0x11,0x11,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x0F,0x11,0x11,0x0F,0x01,0x01,0x01}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x09,0x16}, /* Q */
    {0x0F,0x11,0x11,0x0F,0x05,0x09,0x11}, /* R */
    {0x0E,0x11,0x01,0x0E,0x10,0x11,0x0E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
    {0x1F,0x10,0x08,0x04,0x02,0x01,0x1F}, /* Z */
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, /* [ */
    {0x01,0x02,0x02,0x04,0x08,0x08,0x10}, /* \ */
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, /* ] */
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, /* _ */
    {0x02,0x04,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x0E,0x10,0x1E,0x11,0x1E}, /* a */
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, /* b */
    {0x00,0x00,0x0E,0x11,0x01,0x11,0x0E}, /* c */
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, /* d */
    {0x00,0x00,0x0E,0x11,0x1F,0x01,0x0E}, /* e */
    {0x0C,0x12,0x02,0x07,0x02,0x02,0x02}, /* f */
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x0E}, /* g */
    {0x01,0x01,0x0D,0x13,0x11,0x11,0x11}, /* h */
    {0x04,0x00,0x06,0x04,0x04,0x04,0x0E}, /* i */
    {0x08,0x00,0x08,0x08,0x08,0x09,0x06}, /* j */
    {0x01,0x01,0x09,0x05,0x03,0x05,0x09}, /* k */
    {0x06,0x04,0x04,0x04,0x04,0x04,0x0E}, /* l */
    {0x00,0x00,0x0B,0x15,0x15,0x11,0x11}, /* m */
    {0x00,0x00,0x0D,0x13,0x11,0x11,0x11}, /* n */
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, /* o */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, /* p */
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, /* q */
    {0x00,0x00,0x0D,0x13,0x01,0x01,0x01}, /* r */
    {0x00,0x00,0x1E,0x01,0x0E,0x10,0x0F}, /* s */
    {0x02,0x02,0x07,0x02,0x02,0x12,0x0C}, /* t */
    {0x00,0x00,0x11,0x11,0x11,0x19,0x16}, /* u */
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, /* v */
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, /* w */
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, /* x */
    {0x00,0x00,0x11,0x11,0x1E,0x10,0x0E}, /* y */
    {0x00,0x00,0x1F,0x08,0x04,0x02,0x1F}, /* z */
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08}, /* { */
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, /* | */
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02}, /* } */
    {0x00,0x00,0x12,0x0D,0x00,0x00,0x00}, /* ~ */
};

static void gfx_draw_char(int x, int y, uint32_t c, int glyph_ch) {
    if (glyph_ch<32 || glyph_ch>126) return;
    int s = gfx_scale;
    const uint8_t *glyph = font5x7[glyph_ch-32];
    for (int row=0; row<7; row++)
        for (int col=0; col<5; col++)
            if (glyph[row] & (1<<col))
                for (int dy=0; dy<s; dy++)
                    for (int dx=0; dx<s; dx++)
                        gfx_plot(x+col*s+dx, y+row*s+dy, c);
}

static void gfx_text(int x, int y, uint32_t c, Val *str) {
    int s = gfx_scale;
    int cx = x;
    Val *p = force(str);
    while (p->tag == V_CONS) {
        int c_ch = (int)force(p->u.cons.hd)->u.num;
        if (c_ch == '\n') { y += 9*s; cx = x; }
        else { gfx_draw_char(cx, y, c, c_ch); cx += 6*s; }
        p = force(p->u.cons.tl);
    }
}

static Val *env_lookup(const char *name, Env *env) {
    int bf = builtin_flags(name);
    /* priority builtins (math) — checked before env, can't be shadowed */
    if (bf & BF_PRIO) {
        if (bf & BF_FUN) return vfun(name);
        if (name == S_pi) return vnum(3.14159265358979323846);
    }
    /* user environment */
    for (Env *p=env;p;p=p->next) if (p->name == name) return p->val;
    /* non-priority builtins */
    if (bf & BF_FUN) return vfun(name);
    if (name == S_timeseed) return vnum((double)(time(NULL)%1000000));
    /* user-defined functions */
    Func *f=find_func(name);
    if (f && f->branches) {
        if (f->branches->nargs==0) {
            if (!f->cached) f->cached = eval(f->branches->body,NULL);
            return f->cached;
        }
        return vfun(name);
    }
    dief("unbound variable: %s",name);
    return NULL;
}

static Val *apply_section(int op, Val *arg) {
    if (op==T_CONS) {
        arg=force(arg);
        if (arg->tag==V_PAIR) return vcons(arg->u.pair.fst,arg->u.pair.snd);
        die(":: section expects a pair");
    }
    arg=force(arg);
    if (arg->tag==V_PAIR) {
        double a=force(arg->u.pair.fst)->u.num, b=force(arg->u.pair.snd)->u.num;
        switch (op) {
        case T_PLUS:  return vnum(a+b);
        case T_MINUS: return vnum(a-b);
        case T_STAR:  return vnum(a*b);
        case T_SLASH: if(b==0) die("div0"); return vnum(a/b);
        case T_MOD:   { long la=(long)a,lb=(long)b; if(!lb) die("mod0"); return vnum(la%lb); }
        case T_LT:    return vnum(a<b);
        case T_GT:    return vnum(a>b);
        case T_EQ:    return vnum(a==b);
        case T_LE:    return vnum(a<=b);
        case T_GE:    return vnum(a>=b);
        case T_NE:    return vnum(a!=b);
        }
    }
    die("section: bad arg"); return NULL;
}

/* lazy built-ins */
typedef struct { Val *fn; Val *lst; } MapD;
typedef struct { Val *a; Val *b; } ZipD;

static Val *apply_val(Val *fn, Val **args, int nargs);
static Val *builtin_map(Val *fn, Val *lst);
static Val *builtin_zip(Val *a, Val *b);

static Val *map_thunk(void *d) { MapD *m=d; return builtin_map(m->fn,m->lst); }
static Val *zip_thunk(void *d) { ZipD *z=d; return builtin_zip(z->a,z->b); }

static Val *builtin_map(Val *fn, Val *lst) {
    Val *l=force(lst);
    if (l->tag==V_NIL) return val_nil;
    if (l->tag==V_ARR) {
        /* map over array: produce a list */
        Val *result = val_nil;
        for (int i = ARRLEN(l) - 1; i >= 0; i--)
            result = vcons(apply_val(fn, &ARR(l)[i], 1), result);
        return result;
    }
    Val *hd = apply_val(fn, &l->u.cons.hd, 1);
    MapD *md=malloc(sizeof(MapD)); md->fn=fn; md->lst=l->u.cons.tl;
    return vcons(hd, mkthunk_c(map_thunk, md));
}

static Val *builtin_zip(Val *a, Val *b) {
    Val *fa=force(a), *fb=force(b);
    if (fa->tag==V_NIL||fb->tag==V_NIL) return val_nil;
    ZipD *zd=malloc(sizeof(ZipD)); zd->a=fa->u.cons.tl; zd->b=fb->u.cons.tl;
    return vcons(vpair(fa->u.cons.hd,fb->u.cons.hd), mkthunk_c(zip_thunk,zd));
}

static Val *builtin_append(Val *a, Val *b) {
    Val *fa=force(a);
    if (fa->tag==V_NIL) return b;
    return vcons(fa->u.cons.hd, builtin_append(fa->u.cons.tl, b));
}

typedef struct { long cur, hi; } RangeD;
static Val *range_thunk(void *d) {
    RangeD *r=d;
    if (r->cur > r->hi) return val_nil;
    long c=r->cur;
    RangeD *nr=malloc(sizeof(RangeD)); nr->cur=c+1; nr->hi=r->hi;
    return vcons(vnum(c), mkthunk_c(range_thunk, nr));
}
static Val *builtin_range(Val *a, Val *b) {
    long lo=(long)a->u.num, hi=(long)b->u.num;
    if (lo>hi) return val_nil;
    RangeD *r=malloc(sizeof(RangeD)); r->cur=lo; r->hi=hi;
    return range_thunk(r);
}

static Val *builtin_front(double n, Val *lst) {
    if (n<=0) return val_nil;
    Val *l=force(lst);
    if (l->tag==V_NIL) return val_nil;
    return vcons(l->u.cons.hd, builtin_front(n-1, l->u.cons.tl));
}

static Val *builtin_length(Val *lst) {
    long n = 0;
    Val *l = force(lst);
    while (l->tag == V_CONS) { n++; l = force(l->u.cons.tl); }
    return vnum(n);
}

static Val *call_func(const char *name, Val **args, int nargs);

static Val *apply_val(Val *fn, Val **args, int nargs) {
    fn=force(fn);
    if (fn->tag==V_FUN) return call_func(fn->u.name,args,nargs);
    if (fn->tag==V_SEC && nargs==1) return apply_section(fn->u.sec_op,args[0]);
    die("cannot apply value"); return NULL;
}

/* try_call_builtin: returns result if name is a built-in, NULL otherwise */
static Val *try_call_builtin(const char *name, Val **args, int nargs) {
    if (name==S_map && nargs==2) return builtin_map(args[0],args[1]);
    if (name==S_head && nargs==1) { Val *l=force(args[0]); if(l->tag==V_CONS) return l->u.cons.hd; die("head of []"); }
    if (name==S_tail && nargs==1) { Val *l=force(args[0]); if(l->tag==V_CONS) return l->u.cons.tl; die("tail of []"); }
    if (name==S_succ && nargs==1) return vnum(force(args[0])->u.num+1);
    if (name==S_xor && nargs==2) return vnum((double)((long)force(args[0])->u.num ^ (long)force(args[1])->u.num));
    if (name==S_front && nargs==1 && force(args[0])->tag==V_PAIR) {
        Val *p=force(args[0]); return builtin_front(force(p->u.pair.fst)->u.num, p->u.pair.snd);
    }
    if (name==S_length && nargs==1) return builtin_length(args[0]);
    if (name==S_nth && nargs==2) {
        int n=(int)force(args[0])->u.num;
        Val *p=force(args[1]);
        while (n>0 && p->tag==V_CONS) { p=force(p->u.cons.tl); n--; }
        if (p->tag==V_CONS) return p->u.cons.hd;
        die("nth: index out of range");
    }

    /* pair built-ins */
    if (name==S_fst && nargs==1) { Val *p=force(args[0]); if(p->tag!=V_PAIR) die("fst: not a pair"); return p->u.pair.fst; }
    if (name==S_snd && nargs==1) { Val *p=force(args[0]); if(p->tag!=V_PAIR) die("snd: not a pair"); return p->u.pair.snd; }

    /* array built-ins */
    if (name==S_array && nargs==1) {
        /* list → array */
        int len = 0;
        for (Val *p = force(args[0]); p->tag == V_CONS; p = force(p->u.cons.tl)) len++;
        Val **data = (Val**)malloc(len * sizeof(Val*));
        Val *p = force(args[0]);
        for (int i = 0; i < len; i++) { data[i] = p->u.cons.hd; p = force(p->u.cons.tl); }
        Val *v = val_alloc(); v->tag = V_ARR; v->u.arr.data = data; v->u.arr.len = len;
        return v;
    }
    if (name==S_aget && nargs==2) {
        int i = (int)force(args[0])->u.num;
        Val *a = force(args[1]);
        if (a->tag != V_ARR) die("aget: not an array");
        if (i < 0 || i >= ARRLEN(a)) die("aget: index out of range");
        return ARR(a)[i];
    }
    if (name==S_alen && nargs==1) {
        Val *a = force(args[0]);
        if (a->tag != V_ARR) die("alen: not an array");
        return vnum(ARRLEN(a));
    }
    if (name==S_aset && nargs==3) {
        /* aset i val arr → new array with arr[i]=val */
        int i = (int)force(args[0])->u.num;
        Val *v = args[1];
        Val *a = force(args[2]);
        if (a->tag != V_ARR) die("aset: not an array");
        if (i < 0 || i >= ARRLEN(a)) die("aset: index out of range");
        int len = ARRLEN(a);
        Val **data = (Val**)malloc(len * sizeof(Val*));
        for (int j = 0; j < len; j++) data[j] = ARR(a)[j];
        data[i] = v;
        Val *r = val_alloc(); r->tag = V_ARR; r->u.arr.data = data; r->u.arr.len = len;
        return r;
    }
    if (name==S_amake && nargs==2) {
        /* amake n val → array of n copies of val */
        int n = (int)force(args[0])->u.num;
        Val *v = args[1];
        if (n < 0) die("amake: negative size");
        Val **data = (Val**)malloc(n * sizeof(Val*));
        for (int i = 0; i < n; i++) data[i] = v;
        Val *r = val_alloc(); r->tag = V_ARR; r->u.arr.data = data; r->u.arr.len = n;
        return r;
    }

    if (name==S_tabulate && nargs==2) {
        /* tabulate n f → array of n elements where arr[i] = f(i) */
        int n = (int)force(args[0])->u.num;
        Val *fn = args[1];
        if (n < 0) die("tabulate: negative size");
        Val **data = (Val**)malloc(n * sizeof(Val*));
        Val *zero = vnum(0);
        for (int i = 0; i < n; i++) data[i] = zero;
        /* build array first so GC can reach elements via V_ARR */
        volatile Val *r = val_alloc(); r->tag = V_ARR; r->u.arr.data = data; r->u.arr.len = n;
        for (int i = 0; i < n; i++) {
            Val *idx = vnum(i);
            ARR(r)[i] = apply_val(fn, &idx, 1);
        }
        return (Val*)r;
    }

    if (name==S_amap && nargs==2) {
        /* amap f arr → new array where arr[i] = f(old[i]) */
        Val *fn = args[0];
        Val *a = force(args[1]);
        if (a->tag != V_ARR) die("amap: not an array");
        int n = ARRLEN(a);
        Val **data = (Val**)malloc(n * sizeof(Val*));
        Val *zero = vnum(0);
        for (int i = 0; i < n; i++) data[i] = zero;
        volatile Val *r = val_alloc(); r->tag = V_ARR; r->u.arr.data = data; r->u.arr.len = n;
        for (int i = 0; i < n; i++)
            ARR(r)[i] = apply_val(fn, &ARR(a)[i], 1);
        return (Val*)r;
    }

    /* graphics built-ins */
    if (name==S_gopen && nargs==1) {
        Val *p=force(args[0]); if(p->tag!=V_PAIR) die("gopen expects (w,h)");
        int w=(int)force(p->u.pair.fst)->u.num, h=(int)force(p->u.pair.snd)->u.num;
        if (w<1||w>GFX_MAX_W||h<1||h>GFX_MAX_H) die("gopen: size out of range");
        memset(gfx_buf,0,sizeof(gfx_buf));
        memset(&gfx_f,0,sizeof(gfx_f));
        gfx_f.title="hop"; gfx_f.buf=gfx_buf;
        *(int*)&gfx_f.width=w; *(int*)&gfx_f.height=h;
        fenster_open(&gfx_f); gfx_open=1; gfx_last_time=fenster_time();
        if (!audio_open) { fenster_audio_open(&gfx_audio); audio_open=1; }
        return vnum(0);
    }
    if (name==S_gclose && nargs==1) {
        if (audio_open) { fenster_audio_close(&gfx_audio); audio_open=0; }
        if (gfx_open) { fenster_close(&gfx_f); gfx_open=0; }
        return vnum(0);
    }
    if (name==S_gplot && nargs==1) {
        Val *p=force(args[0]); if(p->tag!=V_PAIR) die("gplot expects (x,(y,c))");
        int x=(int)force(p->u.pair.fst)->u.num;
        Val *p2=force(p->u.pair.snd); if(p2->tag!=V_PAIR) die("gplot expects (x,(y,c))");
        int y=(int)force(p2->u.pair.fst)->u.num;
        uint32_t c=(uint32_t)(long)force(p2->u.pair.snd)->u.num;
        gfx_plot(x,y,c);
        return vnum(0);
    }
    if (name==S_gclear && nargs==1) {
        uint32_t c=(uint32_t)(long)force(args[0])->u.num;
        for (int i=0;i<gfx_f.width*gfx_f.height;i++) gfx_buf[i]=c;
        return vnum(0);
    }
    if (name==S_gsave && nargs==1) {
        memcpy(gfx_snap,gfx_buf,sizeof(uint32_t)*gfx_f.width*gfx_f.height);
        return vnum(0);
    }
    if (name==S_grestore && nargs==1) {
        memcpy(gfx_buf,gfx_snap,sizeof(uint32_t)*gfx_f.width*gfx_f.height);
        return vnum(0);
    }
    if (name==S_gblit && nargs==1) {
        Val *a=force(args[0]);
        if (a->tag!=V_ARR) die("gblit: not an array");
        int n=ARRLEN(a), sz=gfx_f.width*gfx_f.height;
        if (n>sz) n=sz;
        for (int i=0;i<n;i++) gfx_buf[i]=(uint32_t)(long)force(ARR(a)[i])->u.num;
        return vnum(0);
    }
    if (name==S_gdrawcol && nargs==1) {
        /* gdrawcol (w, (h, (s, colorList))) → draw cells each with its own colour
           colorList: list of 0xRRGGBB values, one per cell (0 = empty/black) */
        Val *p=force(args[0]); int gw=(int)force(p->u.pair.fst)->u.num;
        p=force(p->u.pair.snd); int gh=(int)force(p->u.pair.fst)->u.num;
        p=force(p->u.pair.snd); int gs=(int)force(p->u.pair.fst)->u.num;
        Val *board=force(p->u.pair.snd);
        int pw=gfx_f.width, ph=gfx_f.height;
        for (int i=0;i<pw*ph;i++) gfx_buf[i]=0;
        int pad=gs>6?1:0;
        if (board->tag==V_ARR) {
            int n=gw*gh; if (n>ARRLEN(board)) n=ARRLEN(board);
            for (int i=0;i<n;i++) {
                uint32_t c=(uint32_t)(long)force(ARR(board)[i])->u.num;
                if (c) {
                    int cx=(i%gw)*gs, cy=(i/gw)*gs;
                    for (int dy=pad;dy<gs-pad;dy++)
                        for (int dx=pad;dx<gs-pad;dx++)
                            gfx_plot(cx+dx,cy+dy,c);
                }
            }
        } else {
            Val *cur=board;
            for (int i=0;i<gw*gh && cur->tag==V_CONS;i++,cur=force(cur->u.cons.tl)) {
                uint32_t c=(uint32_t)(long)force(cur->u.cons.hd)->u.num;
                if (c) {
                    int cx=(i%gw)*gs, cy=(i/gw)*gs;
                    for (int dy=pad;dy<gs-pad;dy++)
                        for (int dx=pad;dx<gs-pad;dx++)
                            gfx_plot(cx+dx,cy+dy,c);
                }
            }
        }
        return vnum(0);
    }
    if (name==S_gloop && nargs==1) {
        if (!gfx_open) return vnum(1);
        memcpy(gfx_prev_keys, gfx_f.keys, sizeof(gfx_f.keys));
        return vnum(fenster_loop(&gfx_f));
    }
    if (name==S_gkeyevent && nargs==1) {
        for (int i=1; i<256; i++) {
            if (gfx_f.keys[i] && !gfx_prev_keys[i]) {
                int k = i;
                /* letters: lowercase unless shift held (mod bit 1) */
                if (k >= 65 && k <= 90 && !(gfx_f.mod & 2)) k += 32;
                return vnum(k);
            }
        }
        return vnum(0);
    }
    if (name==S_gsync && nargs==1) {
        audio_feed();
        int64_t now=fenster_time();
        if (now-gfx_last_time < 1000/60) fenster_sleep(1000/60-(now-gfx_last_time));
        gfx_last_time=fenster_time();
        return vnum(0);
    }
    if (name==S_gwidth && nargs==1) { return vnum(gfx_open?gfx_f.width:0); }
    if (name==S_gheight && nargs==1) { return vnum(gfx_open?gfx_f.height:0); }
    if (name==S_gmouse && nargs==1) {
        return vpair(vnum(gfx_f.x),vnum(gfx_f.y));
    }
    if (name==S_gclick && nargs==1) {
        return vnum(gfx_f.mouse);
    }
    if (name==S_gkey && nargs==1) {
        int k=(int)force(args[0])->u.num;
        return vnum((k>=0&&k<256)?gfx_f.keys[k]:0);
    }
    if (name==S_gline && nargs==5) {
        int x0=(int)force(args[0])->u.num, y0=(int)force(args[1])->u.num;
        int x1=(int)force(args[2])->u.num, y1=(int)force(args[3])->u.num;
        uint32_t c=(uint32_t)(long)force(args[4])->u.num;
        gfx_line(x0,y0,x1,y1,c);
        return vnum(0);
    }
    if (name==S_gcircle && nargs==4) {
        int cx=(int)force(args[0])->u.num, cy=(int)force(args[1])->u.num;
        int r=(int)force(args[2])->u.num;
        uint32_t c=(uint32_t)(long)force(args[3])->u.num;
        gfx_circle(cx,cy,r,c);
        return vnum(0);
    }
    if (name==S_gtext && nargs==4) {
        int x=(int)force(args[0])->u.num, y=(int)force(args[1])->u.num;
        uint32_t c=(uint32_t)(long)force(args[2])->u.num;
        gfx_text(x,y,c,args[3]);
        return vnum(0);
    }
    if (name==S_gscale && nargs==1) {
        int s=(int)force(args[0])->u.num;
        if (s>=1 && s<=8) gfx_scale=s;
        return vnum(0);
    }
    if (name==S_gpen && nargs==1) {
        int w=(int)force(args[0])->u.num;
        if (w>=1 && w<=32) gfx_pen=w;
        return vnum(0);
    }
    if (name==S_gtitle && nargs==1) {
        /* extract Hope string to C string */
        static char tbuf[256];
        int ti=0;
        Val *p=force(args[0]);
        while (p->tag==V_CONS && ti<254) { tbuf[ti++]=(char)(int)force(p->u.cons.hd)->u.num; p=force(p->u.cons.tl); }
        tbuf[ti]='\0';
        gfx_f.title = tbuf;
        if (gfx_open) {
#if defined(__APPLE__)
            id ns = msg1(id, cls("NSString"), "stringWithUTF8String:", const char *, tbuf);
            msg1(void, gfx_f.wnd, "setTitle:", id, ns);
#elif defined(_WIN32)
            SetWindowTextA(gfx_f.hwnd, tbuf);
#else
            XStoreName(gfx_f.dpy, gfx_f.w, tbuf);
#endif
        }
        return vnum(0);
    }
    if (name==S_gbeep && nargs==2) {
        double freq = force(args[0])->u.num;
        int dur_ms = (int)force(args[1])->u.num;
        if (!audio_open) { fenster_audio_open(&gfx_audio); audio_open = 1; }
        beep_freq = freq;
        beep_phase = 0;
        beep_remain = FENSTER_SAMPLE_RATE * dur_ms / 1000;
        audio_feed();
        return vnum(0);
    }
    /* rand / srand */
    if (name==S_rand  && nargs==1) { (void)force(args[0]); return vnum((double)arc4random()); }
    if (name==S_srand && nargs==1) { srand((unsigned)force(args[0])->u.num); return vnum(0); }
    /* math.h built-ins — single argument (radians) */
    if (name==S_sin  && nargs==1) return vnum(sin  (force(args[0])->u.num));
    if (name==S_cos  && nargs==1) return vnum(cos  (force(args[0])->u.num));
    if (name==S_tan  && nargs==1) return vnum(tan  (force(args[0])->u.num));
    if (name==S_asin && nargs==1) return vnum(asin (force(args[0])->u.num));
    if (name==S_acos && nargs==1) return vnum(acos (force(args[0])->u.num));
    if (name==S_atan && nargs==1) return vnum(atan (force(args[0])->u.num));
    if (name==S_exp  && nargs==1) return vnum(exp  (force(args[0])->u.num));
    if (name==S_log  && nargs==1) return vnum(log  (force(args[0])->u.num));
    if (name==S_log10&& nargs==1) return vnum(log10(force(args[0])->u.num));
    if (name==S_sqrt && nargs==1) return vnum(sqrt (force(args[0])->u.num));
    if (name==S_floor&& nargs==1) return vnum(floor(force(args[0])->u.num));
    if (name==S_ceil && nargs==1) return vnum(ceil (force(args[0])->u.num));
    if (name==S_fabs && nargs==1) return vnum(fabs (force(args[0])->u.num));
    /* math.h built-ins — two arguments */
    if (name==S_atan2 && nargs==2) return vnum(atan2(force(args[0])->u.num, force(args[1])->u.num));
    if (name==S_pow   && nargs==2) return vnum(pow  (force(args[0])->u.num, force(args[1])->u.num));
    return NULL; /* not a built-in */
}

static Val *call_func(const char *name, Val **args, int nargs) {
    Val *r = try_call_builtin(name, args, nargs);
    if (r) return r;
    Func *f=find_func(name);
    if (!f) dief("undefined function: %s",name);
    for (Branch *b=f->branches;b;b=b->next) {
        if (b->nargs!=nargs) continue;
        int match=1; Env *env=NULL;
        for (int i=0;i<nargs;i++)
            if (!match_pat(b->pats[i],args[i],&env)) { match=0; break; }
        if (match) return eval(b->body,env);
    }
    dief("no matching clause for %s/%d",name,nargs);
    return NULL;
}

static Val *list_concat(Val *a, Val *b) {
    a=force(a);
    if (a->tag==V_NIL) return b;
    return vcons(a->u.cons.hd, list_concat(force(a->u.cons.tl), b));
}
static Val *eval_comp(Expr *out, Expr *guard, Expr **gens, int ngens, Env *env);
static Val *comp_iter(Expr *out, Expr *guard, Expr **gens, int ngens, Env *env, Val *range) {
    range=force(range);
    if (range->tag==V_NIL) return val_nil;
    Env *ne=env_alloc(); ne->name=gens[0]->name; ne->val=range->u.cons.hd; ne->next=env;
    Val *head=eval_comp(out, guard, gens+2, ngens-2, ne);
    Val *tail=comp_iter(out, guard, gens, ngens, env, range->u.cons.tl);
    return list_concat(head, tail);
}
static Val *eval_comp(Expr *out, Expr *guard, Expr **gens, int ngens, Env *env) {
    if (ngens==0) {
        if (force(eval(guard,env))->u.num!=0) return vcons(eval(out,env), val_nil);
        return val_nil;
    }
    return comp_iter(out, guard, gens, ngens, env, force(eval(gens[1],env)));
}

static Val *eval(Expr *e, Env *env) {
  for (;;) {
    tok_line = e->line; tok_col = e->col; src_file = e->file;
    switch (e->tag) {
    case E_NUM: return vnum(e->num);
    case E_NIL: return val_nil;
    case E_SEC: return vsec(e->op);
    case E_VAR: return env_lookup(e->name,env);
    case E_PAIR: { Val *a=eval(e->l,env); Val *b=eval(e->r,env); return vpair(a,b); }
    case E_APP: {
        Val *fn=eval(e->l,env);
        Val *args[MAX_ARGS];
        for (int i=0;i<e->nargs;i++) args[i]=eval(e->args[i],env);
        fn=force(fn);
        if (fn->tag==V_SEC && e->nargs==1)
            return apply_section(fn->u.sec_op,args[0]);
        if (fn->tag!=V_FUN) { die("cannot apply value"); return NULL; }
        /* try built-in first */
        Val *br = try_call_builtin(fn->u.name, args, e->nargs);
        if (br) return br;
        /* user-defined: TCO — find matching branch, rebind e/env, continue */
        Func *f=find_func(fn->u.name);
        if (!f) dief("undefined function: %s",fn->u.name);
        int matched=0;
        for (Branch *b=f->branches;b;b=b->next) {
            if (b->nargs!=e->nargs) continue;
            int ok=1; Env *ne=NULL;
            for (int i=0;i<e->nargs;i++)
                if (!match_pat(b->pats[i],args[i],&ne)) { ok=0; break; }
            if (ok) { e=b->body; env=ne; matched=1; break; }
        }
        if (!matched) dief("no matching clause for %s/%d",fn->u.name,e->nargs);
        continue; /* TCO: loop instead of recursive eval */
    }
    case E_BIN: {
        if (e->op==T_CONS)   return vcons(eval(e->l,env), mkthunk_e(e->r,env));
        if (e->op==T_DOTDOT) return builtin_range(eval(e->l,env),eval(e->r,env));
        if (e->op==T_BAR2)   return builtin_zip(eval(e->l,env),eval(e->r,env));
        if (e->op==T_APPEND) return builtin_append(eval(e->l,env),eval(e->r,env));
        if (e->op==T_AND) { Val *l=force(eval(e->l,env)); return l->u.num==0 ? vnum(0) : vnum(force(eval(e->r,env))->u.num!=0); }
        if (e->op==T_OR)  { Val *l=force(eval(e->l,env)); return l->u.num!=0 ? vnum(1) : vnum(force(eval(e->r,env))->u.num!=0); }
        Val *lv=force(eval(e->l,env)), *rv=force(eval(e->r,env));
        double l=lv->u.num, r=rv->u.num;
        switch (e->op) {
        case T_PLUS:  return vnum(l+r);
        case T_MINUS: return vnum(l-r);
        case T_STAR:  return vnum(l*r);
        case T_SLASH: if(r==0) die("div0"); return vnum(l/r);
        case T_MOD:   { long a=(long)l,b=(long)r; if(!b) die("mod0"); return vnum(a%b); }
        case T_LT: return vnum(l<r);
        case T_GT: return vnum(l>r);
        case T_EQ: return vnum(l==r);
        case T_LE: return vnum(l<=r);
        case T_GE: return vnum(l>=r);
        case T_NE: return vnum(l!=r);
        }
        die("unknown op");
    }
    case E_NEG: return vnum(-force(eval(e->l,env))->u.num);
    case E_NOT: return vnum(force(eval(e->l,env))->u.num!=0 ? 0 : 1);
    case E_IF:  /* TCO: jump to chosen branch */
        e = force(eval(e->cond,env))->u.num != 0 ? e->then_e : e->else_e;
        continue;
    case E_WHERE: {
        if (e->wpat) {
            Val *dv = eval(e->r, env);
            if (!match_pat(e->wpat, dv, &env)) die("where pattern match failed");
            e = e->l; continue;
        }
        if (e->name == S_underscore) {
            eval(e->r, env);   /* where _ = expr: eager (side effects) */
            e = e->l; continue;
        }
        /* lazy when body is if-expr (avoids evaluating branch-specific
           definitions before the branch is chosen); eager otherwise */
        Expr *b = e->l;
        while (b->tag == E_WHERE || b->tag == E_WHEREREC) b = b->l;
        if (b->tag == E_IF) {
            Val *thunk = mkthunk_e(e->r, env);
            Env *ne=env_alloc();
            ne->name=e->name; ne->val=thunk; ne->next=env;
            e = e->l; env = ne; continue;
        }
        Val *dv = eval(e->r, env);
        Env *ne=env_alloc();
        ne->name=e->name; ne->val=dv; ne->next=env;
        e = e->l; env = ne; continue;
    }
    case E_WHEREREC: {
        Val *thunk = mkthunk_e(e->r, NULL);
        Env *ne=env_alloc();
        ne->name=e->name; ne->val=thunk; ne->next=env;
        thunk->u.thunk.env = ne;
        e = e->l; env = ne; continue;
    }
    case E_COMP:
        return eval_comp(e->l, e->r, e->args, e->nargs, env);
    }
    die("unknown expr tag"); return NULL;
  }
}

/* ===== Driver ===== */
static void run_file(int lib_mode);

static void skip_to_semi(void) {
    while (tok!=T_SEMI && tok!=T_EOF) scan();
    if (tok==T_SEMI) scan();
}

/* raw skip: advance in the source character-by-character until ';', then rescan */
static void raw_skip_to_semi(void) {
    while (ch!=';' && ch!=EOF) readch();
    if (ch==';') readch();
    scan();
}

static void load_module(const char *name) {
    /* check if already loaded */
    for (int i=0; i<n_loaded; i++)
        if (!strcmp(loaded_modules[i], name)) return;
    if (n_loaded>=MAX_MODULES) die("too many modules");
    loaded_modules[n_loaded++] = strdup(name);

    /* resolve path: <src_dir>/<name>.hop */
    char path[2048];
    snprintf(path,sizeof path,"%s/%s.hop",src_dir,name);
    FILE *mf = fopen(path,"r");
    if (!mf) { fprintf(stderr,"%s(%d:%d) cannot find module '%s' (%s)\n",src_file,tok_line,tok_col,name,path); return; }

    LexState saved; lex_save(&saved);

    src=mf; pb_valid=0;
    src_line=1; src_col=0;
    src_file=strdup(path);
    readch(); scan();
    run_file(1);
    fclose(mf);

    lex_restore(&saved);
}

static int parse_def_pats(Pat **pats) {
    int n=0;
    if (tok==T_LPAREN) {
        scan();
        if (tok==T_RPAREN) { scan(); return 0; }
        pats[n++]=parse_pat();
        while (tok==T_COMMA) { scan(); pats[n++]=parse_pat(); }
        expect(T_RPAREN);
        if (n>=2) {
            Pat *pair=pats[n-1];
            for (int i=n-2;i>=0;i--) pair=p_pair(pats[i],pair);
            pats[0]=pair; n=1;
        }
    } else {
        while (tok!=T_IS && tok!=T_EOF && n<MAX_ARGS)
            pats[n++]=parse_pat_atom();
    }
    return n;
}

static void run_file(int lib_mode) {
    while (tok!=T_EOF) {
        /* in lib mode, recover from parse errors by skipping to next ';' */
        if (lib_mode) {
            err_recovery=1;
            if (setjmp(err_jmp)) { err_recovery=0; raw_skip_to_semi(); continue; }
        }

        /* uses directive: read module paths as raw characters */
        if (tok==T_ID && tok_id_interned == S_uses) {
            /* after 'uses', read paths raw (supports ./ ../ and .hop suffix) */
            int found_semi=0, end_line, end_col;
            while (ch!=';' && ch!='\n' && ch!=EOF) {
                /* skip spaces and commas (not newlines) */
                while (ch!=EOF && ch!='\n' && ch!=';' && (ch==' '||ch=='\t'||ch==',')) readch();
                if (ch==';' || ch=='\n' || ch==EOF) break;
                /* read one module path */
                char modname[1024]; int mi=0;
                while (ch!=EOF && !isspace(ch) && ch!=';' && ch!=',')
                    { modname[mi++]=ch; readch(); }
                modname[mi]='\0';
                end_line=src_line; end_col=src_col;
                /* strip .hop suffix if present */
                if (mi>4 && !strcmp(modname+mi-4,".hop")) modname[mi-4]='\0';
                load_module(modname);
            }
            if (ch==';') { found_semi=1; readch(); }
            scan();
            if (!found_semi)
                fprintf(stderr,"%s(%d:%d) expected %s after uses\n",src_file,end_line,end_col,tok_name(T_SEMI));
            continue;
        }

        /* skip declarations */
        if (tok==T_ID && (tok_id_interned==S_dec||
            tok_id_interned==S_typevar||tok_id_interned==S_infix||
            tok_id_interned==S_infixr||tok_id_interned==S_data||
            tok_id_interned==S_abstype||tok_id_interned==S_type||
            tok_id_interned==S_private)) { skip_to_semi(); continue; }

        if (tok==T_ID && tok_id_interned == S_write) {
            int write_line=tok_line, write_col=tok_col;
            scan();
            if (tok!=T_STR) die("write expects a format string");
            char fmt[4096]; strcpy(fmt, tok_str); scan();
            /* collect arguments */
            Expr *wargs[64]; int nw=0;
            while (tok!=T_SEMI && tok!=T_EOF && nw<64)
                wargs[nw++]=parse_atom();
            if (!lib_mode) {
                /* evaluate arguments */
                Val *wvals[64];
                for (int i=0;i<nw;i++) wvals[i]=eval(wargs[i],NULL);
                /* printf-style formatting */
                tok_line=write_line; tok_col=write_col;
                int ai=0;
                for (char *p=fmt; *p; p++) {
                    if (*p=='%' && *(p+1)) {
                        p++;
                        if (*p=='%') { putchar('%'); continue; }
                        if (ai>=nw) die("write: not enough arguments");
                        Val *v=force(wvals[ai++]);
                        switch (*p) {
                        case 'd':
                            if (v->tag!=V_NUM) die("write: %d expects a number");
                            printf("%ld",(long)v->u.num); break;
                        case 'f':
                            if (v->tag!=V_NUM) die("write: %f expects a number");
                            printf("%g",v->u.num); break;
                        case 's':
                            write_chars(v); break;
                        case 'c':
                            if (v->tag!=V_NUM) die("write: %c expects a number");
                            putchar((int)v->u.num); break;
                        case 'v':
                            print_val(v); break;
                        default:
                            putchar('%'); putchar(*p); break;
                        }
                    } else putchar(*p);
                }
            }
            expect(T_SEMI); continue;
        }

        if (tok==T_FUN || tok==T_VALOF) {
            int is_fun = (tok==T_FUN);
            int kw_line=tok_line, kw_col=tok_col;
            scan();
            if (tok!=T_ID) { skip_to_semi(); continue; }
            const char *fn = intern(tok_id); scan();
            if (is_fun && find_func(fn))
                { fprintf(stderr,"%s(%d:%d) warning: 'fun' redefines '%s' (use '---' for additional branches)\n",src_file,kw_line,kw_col,fn); }
            if (!is_fun && !find_func(fn))
                { fprintf(stderr,"%s(%d:%d) warning: '---' used for new function '%s' (use 'fun' for first definition)\n",src_file,kw_line,kw_col,fn); }
            Pat *pats[MAX_ARGS]; int na=parse_def_pats(pats);
            expect(T_IS);
            Expr *body=parse_expr();
            expect(T_SEMI);
            add_branch(fn,na,pats,body); continue;
        }

        if (tok==T_ID) {
            const char *nm = intern(tok_id); scan();
            if (tok==T_IS) {
                scan(); Expr *body=parse_expr(); expect(T_SEMI);
                add_branch(nm,0,NULL,body); continue;
            }
            pushback(); tok=T_ID; strcpy(tok_id,nm); tok_id_interned = nm;
        }

        Expr *e=parse_expr();
        expect(T_SEMI);
        if (!lib_mode) {
            Val *v=eval(e,NULL);
            /* suppress printing for graphics calls (gopen, gplot, ...) */
            int is_gfx = (e->tag==E_APP && e->l->tag==E_VAR &&
                          (builtin_flags(e->l->name) & BF_GFX));
            if (!is_gfx) { print_val(v); printf("\n"); }
        }
        continue;
    }
    err_recovery=0;
}

int main(int argc, char **argv) {
    intern_init();
    builtin_init();
    volatile int stack_anchor;
    gc_stack_base = (void*)&stack_anchor;
    gc_mark_extra_roots = gc_mark_func_cache;
    if (argc!=2) { fprintf(stderr,"usage: hop <file.hop>\n"); return 1; }

    /* compute source directory for resolving library paths */
    strncpy(src_dir, argv[1], sizeof(src_dir)-1);
    char *slash = strrchr(src_dir, '/');
    if (slash) *slash = '\0'; else strcpy(src_dir, ".");

    src_file = argv[1];
    src=fopen(argv[1],"r");
    if (!src) { perror(argv[1]); return 1; }
    readch(); scan();
    run_file(0); fclose(src); return 0;
}
