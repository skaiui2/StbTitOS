#include "inter.h"
#include "ir.h"
#include "parser.h"
#include "heap.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define COMPILER_DEBUG_ENABLED 0

static void compiler_debug(const char *fmt, ...)
{
#if COMPILER_DEBUG_ENABLED
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
#else
    (void)fmt;
#endif
}

static struct Type TYPE_INT_OBJ   = { TYPE_INT,   4 };
static struct Type TYPE_BOOL_OBJ  = { TYPE_BOOL,  1 };
static struct Type TYPE_BYTE_OBJ = { TYPE_CHAR, 1 };
static struct Type TYPE_SHORT_OBJ = { TYPE_SHORT, 2 };

struct Type *Type_Short = &TYPE_SHORT_OBJ;
struct Type *Type_Byte = &TYPE_BYTE_OBJ;
struct Type *Type_Int   = &TYPE_INT_OBJ;
struct Type *Type_Bool  = &TYPE_BOOL_OBJ;

static int temp_count = 1;
int new_temp(void)
{
    return temp_count++;
}

char *region_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = mg_region_alloc(longterm_region, n);
    memcpy(p, s, n);
    return p;
}

char *region_mg_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = mg_region_alloc(longterm_region, n);
    memcpy(p, s, n);
    return p;
}

static struct Type *type_max(struct Type *a, struct Type *b)
{
    if (!a || !b) return NULL;

    if (a->tag == TYPE_INT || b->tag == TYPE_INT)
        return Type_Int;
    if (a->tag == TYPE_CHAR && b->tag == TYPE_CHAR)
        return a;

    return NULL;
}

static int g_labels = 0;

struct Node *node_new(void)
{
    struct Node *n = mg_region_alloc(longterm_region,sizeof(struct Node));
    n->lexline  = 0;
    n->gen      = NULL;
    n->jumping  = NULL;
    n->tostring = NULL;
    return n;
}

void node_error(struct Node *self, const char *msg)
{
    fprintf(stderr, "near line %d: %s\n",
            self ? self->lexline : -1, msg);
    exit(1);
}

int node_newlabel(void)
{
    return ++g_labels;
}

void node_emitlabel(int i)
{
    printf("L%d:\n", i);

    struct IR ir = {0};
    ir.op    = IR_LABEL;
    ir.label = i;
    ir_emit(ir);
}

void node_emit(const char *fmt, ...)
{
#if COMPILER_DEBUG_ENABLED
    va_list ap;
    va_start(ap, fmt);
    printf("\t");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
#else
    (void)fmt;
#endif
}

static void node_emit_jumps(const char *test, int t, int f)
{
    if (t != 0 && f != 0) {
        node_emit("if %s goto L%d", test, t);
        node_emit("goto L%d", f);
    } else if (t != 0) {
        node_emit("if %s goto L%d", test, t);
    } else if (f != 0) {
        node_emit("iffalse %s goto L%d", test, f);
    }
}

static struct Node *expr_gen(struct Node *self);
static void         expr_jumping(struct Node *self, int t, int f);
static char        *expr_tostring(struct Node *self);

struct Expr *expr_new(struct lexer_token *tok, struct Type *type)
{
    struct Expr *e = mg_region_alloc(longterm_region,sizeof(struct Expr));
    e->op   = tok;
    e->type = type;

    e->temp_no = 0;

    e->base.gen      = (void *)expr_gen;
    e->base.jumping  = expr_jumping;
    e->base.tostring = expr_tostring;

    return e;
}

static struct Node *expr_gen(struct Node *self)
{
    struct Expr *e = (struct Expr *)self;

    if (e->temp_no == 0)
        e->temp_no = new_temp();

    if (self->tag == TAG_STRING) {
        struct StringLiteral *sl = (struct StringLiteral *)self;

        if (e->temp_no == 0)
            e->temp_no = new_temp();

        struct IR ir = {0};
        ir.op   = IR_MOVE;
        ir.dst  = e->temp_no;
        ir.src1 = sl->str_id;
        ir_emit(ir);
        return self;
    }

    if (self->tag == TAG_BUILTIN_CALL) {
        struct BuiltinCall *b = (struct BuiltinCall *)self;
        for (int i = 0; i < b->argc; i++)
            expr_gen((struct Node *)b->args[i]);

        struct IR ir = {0};
        ir.op        = IR_NATIVE_CALL;
        ir.dst       = e->temp_no;
        ir.native_id   = b->native_id;
        ir.argc      = b->argc;
        ir.arg_width = b->base.type->width;

        for (int i = 0; i < b->argc; i++)
            ir.args[i] = b->args[i]->temp_no;

        ir_emit(ir);
        return self;
    }

    if (self->tag == TAG_CTX) {
        struct CtxExpr *c = (struct CtxExpr *)self;
        struct IR ir = {0};
        ir.op   = IR_LOAD_CTX;
        ir.dst  = e->temp_no;
        ir.src1 = c->offset;
        ir.src2 = e->type->width;
        ir_emit(ir);
        return self;
    }

    if (self->tag == TAG_ACCESS) {
        struct Access *acc = (struct Access *)self;

        if (acc->index->base.tag != TAG_CONSTANT) {
            fprintf(stderr, "non-constant array index not supported in MVP\n");
            exit(1);
        }

        int idx = ((struct Constant *)acc->index)->int_val;
        int elem_offset = acc->slot + idx * acc->width;

        struct IR ir = {0};
        ir.op          = IR_LOAD;
        ir.dst         = e->temp_no;
        ir.array_base  = elem_offset;
        ir.array_index = 0;
        ir.array_width = acc->width;
        ir_emit(ir);
        return self;
    }

    if (self->tag == TAG_ID) {
        struct Id *id = (struct Id *)self;

        struct IR ir = {0};
        ir.op          = IR_LOAD;
        ir.dst         = e->temp_no;
        ir.array_base  = id->offset;
        ir.array_index = 0;
        ir.array_width = id->base.type->width;
        ir_emit(ir);
        return self;
    }

    if (self->tag == TAG_CONSTANT) {
        struct Constant *c = (struct Constant *)self;

        struct IR ir = {0};
        ir.op   = IR_MOVE;
        ir.dst  = e->temp_no;
        ir.src1 = c->int_val;
        ir_emit(ir);
        return self;
    }

    if (self->tag == TAG_ARITH) {
        struct Arith *a = (struct Arith *)self;

        expr_gen((struct Node *)a->e1);
        expr_gen((struct Node *)a->e2);

        struct IR ir = {0};
        switch (a->base.base.op->tag) {
            case PLUS:  ir.op = IR_ADD; break;
            case MINUS: ir.op = IR_SUB; break;
            case STAR:  ir.op = IR_MUL; break;
            case SLASH: ir.op = IR_DIV; break;
            default:    return self;
        }

        ir.dst  = e->temp_no;
        ir.src1 = a->e1->temp_no;
        ir.src2 = a->e2->temp_no;
        ir_emit(ir);
        return self;
    }

    if (self->tag == TAG_UNARY) {
        struct Unary *u = (struct Unary *)self;

        expr_gen((struct Node *)u->expr);

        struct IR ir = {0};
        if (u->base.base.op->tag == MINUS)
            ir.op = IR_NEG;
        else if (u->base.base.op->tag == NOT)
            ir.op = IR_NOT;
        else
            return self;

        ir.dst  = e->temp_no;
        ir.src1 = u->expr->temp_no;
        ir_emit(ir);
        return self;
    }

    return self;
}

static void expr_jumping(struct Node *self, int t, int f)
{
    char *s = self->tostring(self);
    node_emit_jumps(s, t, f);
}

static char *expr_tostring(struct Node *self)
{
    struct Expr *e = (struct Expr *)self;
    return token_to_string(e->op);
}

static struct Node *expr_gen(struct Node *self);

static struct Node *ctxexpr_gen(struct Node *self)
{
    struct CtxExpr *c = (struct CtxExpr *)self;
    struct Expr *e = &c->base;

    if (e->temp_no == 0)
        e->temp_no = new_temp();

    struct IR ir = {0};
    ir.op   = IR_LOAD_CTX;
    ir.dst  = e->temp_no;
    ir.src1 = c->offset;
    ir.src2 = e->type->width;

    ir_emit(ir);
    return self;
}

static char *ctxexpr_tostring(struct Node *self)
{
    struct CtxExpr *c = (struct CtxExpr *)self;

    char *buf = mg_region_alloc(longterm_region,32);
    snprintf(buf, 32, "ctx[%d]", c->offset);
    return buf;
}

struct Expr *ctx_load_expr_new(int offset)
{
    struct CtxExpr *c = mg_region_alloc(longterm_region,sizeof(struct CtxExpr));

    c->base.base.tag = TAG_CTX;
    c->base.op       = NULL;

    c->base.type     = Type_Byte;

    c->base.temp_no  = 0;
    c->offset        = offset;

    c->base.base.gen      = (void *)expr_gen;
    c->base.base.jumping  = NULL;
    c->base.base.tostring = ctxexpr_tostring;

    return &c->base;
}

/* Return */
static void return_gen(struct Node *self, int b, int a)
{
    struct Return *r = (struct Return *)self;

    expr_gen((struct Node *)r->expr);

    struct IR ir = {0};
    ir.op   = IR_RET;
    ir.src1 = r->expr->temp_no;

    compiler_debug("[IR] EMIT RET t%d\n", ir.src1);
    ir_emit(ir);
}

struct Return *return_new(struct Expr *expr)
{
    struct Return *r = mg_region_alloc(longterm_region,sizeof(struct Return));
    r->expr = expr;

    r->base.base.tag = TAG_RETURN;
    r->base.base.gen = return_gen;
    return r;
}

static char *builtin_tostring(struct Node *self)
{
    struct BuiltinCall *b = (struct BuiltinCall *)self;
    char *name = token_to_string(b->base.op);

    if (b->argc == 0) {
        size_t len = strlen(name) + 3;
        char *buf = mg_region_alloc(longterm_region, len);
        snprintf(buf, len, "%s()", name);
        return buf;
    }

    char *arg = b->args[0]->base.tostring((struct Node *)b->args[0]);
    size_t len = strlen(name) + strlen(arg) + 4;
    char *buf = mg_region_alloc(longterm_region, len);
    snprintf(buf, len, "%s(%s)", name, arg);
    return buf;
}

struct BuiltinCall *builtin_call_new(const char *name,
                                     int native_id,
                                     int argc,
                                     struct Expr **args)
{
    struct BuiltinCall *b = mg_region_alloc(longterm_region, sizeof(*b));

    b->base.base.tag      = TAG_BUILTIN_CALL;
    b->base.base.gen      = (void *)expr_gen;
    b->base.base.jumping  = expr_jumping;
    b->base.base.tostring = builtin_tostring;

    b->base.temp_no = 0;

    struct lexer_token *tok = mg_region_alloc(longterm_region, sizeof(*tok));
    tok->tag    = ID;
    tok->lexeme = region_mg_strdup(name);   

    b->base.op   = tok;

    b->base.type = Type_Int;

    b->native_id = native_id;

    b->argc = argc;
    for (int i = 0; i < argc; i++)
        b->args[i] = args[i];

    return b;
}

struct Stmt *Stmt_Null      = NULL;
struct Stmt *Stmt_Enclosing = NULL;

struct Stmt *stmt_new(void)
{
    struct Stmt *s = mg_region_alloc(longterm_region,sizeof(struct Stmt));
    s->after = 0;
    s->base.gen      = NULL;
    s->base.jumping  = NULL;
    s->base.tostring = NULL;
    return s;
}

void init_stmt_singletons(void)
{
    Stmt_Null = mg_region_alloc(longterm_region, sizeof(struct Stmt));
    memset(Stmt_Null, 0, sizeof(struct Stmt));
    Stmt_Enclosing = Stmt_Null;
}

static char *ctx_ptr_tostring(struct Node *self)
{
    struct CtxPtrExpr *p = (struct CtxPtrExpr *)self;
    char *buf = mg_region_alloc(longterm_region,32);
    snprintf(buf, 32, "&ctx[%d]", p->base_offset);
    return buf;
}

struct CtxPtrExpr *ctx_ptr_new(int base_offset, struct Type *ty)
{
    struct CtxPtrExpr *p = mg_region_alloc(longterm_region,sizeof(*p));

    p->base.base.tag      = TAG_CTX_PTR;
    p->base.base.gen      = (void *)expr_gen;
    p->base.base.jumping  = expr_jumping;
    p->base.base.tostring = ctx_ptr_tostring;

    p->base.op      = NULL;
    p->base.temp_no = 0;
    p->base.type    = ty;

    p->base_offset = base_offset;
    p->st          = NULL;

    return p;
}

struct Constant *Constant_true  = NULL;
struct Constant *Constant_false = NULL;

static char *constant_tostring(struct Node *self);
static void  constant_jumping(struct Node *self, int t, int f);

struct Constant *constant_new(struct lexer_token *tok, struct Type *type)
{
    struct Constant *c = mg_region_alloc(longterm_region,sizeof(struct Constant));

    c->base.op   = tok;
    c->base.type = type;
    c->base.base.tag = TAG_CONSTANT;

    if (tok->tag == NUM) {
        c->int_val = tok->int_val;
    }

    c->base.base.gen      = (void *)expr_gen;
    c->base.base.jumping  = constant_jumping;
    c->base.base.tostring = constant_tostring;

    return c;
}

struct Constant *constant_int(int value)
{
    struct lexer_token *tok = mg_region_alloc(longterm_region,sizeof(struct lexer_token));
    tok->tag     = NUM;
    tok->int_val = value;
    tok->lexeme  = NULL;
    return constant_new(tok, Type_Int);
}

void init_constant_singletons(void)
{
    static struct lexer_token tok_true  = { TRUE,  0, .lexeme = "true" };
    static struct lexer_token tok_false = { FALSE, 0, .lexeme = "false" };

    Constant_true  = mg_region_alloc(longterm_region, sizeof(struct Constant));
    Constant_false = mg_region_alloc(longterm_region, sizeof(struct Constant));

    Constant_true->base.op   = &tok_true;
    Constant_true->base.type = Type_Bool;
    Constant_true->base.base.tag = TAG_CONSTANT;
    Constant_true->int_val = 1;
    Constant_true->base.base.gen      = (void *)expr_gen;
    Constant_true->base.base.jumping  = constant_jumping;
    Constant_true->base.base.tostring = constant_tostring;

    Constant_false->base.op   = &tok_false;
    Constant_false->base.type = Type_Bool;
    Constant_false->base.base.tag = TAG_CONSTANT;
    Constant_false->int_val = 0;
    Constant_false->base.base.gen      = (void *)expr_gen;
    Constant_false->base.base.jumping  = constant_jumping;
    Constant_false->base.base.tostring = constant_tostring;
}

static char *constant_tostring(struct Node *self)
{
    struct Constant *c = (struct Constant *)self;
    return token_to_string(c->base.op);
}

static void constant_jumping(struct Node *self, int t, int f)
{
    struct Constant *c = (struct Constant *)self;

    if (c == Constant_true && t != 0) {
        node_emit("goto L%d", t);
    } else if (c == Constant_false && f != 0) {
        node_emit("goto L%d", f);
    } else {
        char *s = self->tostring(self);
        node_emit_jumps(s, t, f);
    }
}

static char *op_tostring(struct Node *self)
{
    struct Op *o = (struct Op *)self;
    return token_to_string(o->base.op);
}

struct Op *op_new(struct lexer_token *tok, struct Type *type)
{
    struct Op *o = mg_region_alloc(longterm_region,sizeof(struct Op));
    o->base.op   = tok;
    o->base.type = type;

    o->base.base.gen      = (void *)expr_gen;
    o->base.base.jumping  = expr_jumping;
    o->base.base.tostring = op_tostring;

    return o;
}

static char *arith_tostring(struct Node *self)
{
    struct Arith *a = (struct Arith *)self;

    char *s1 = a->e1->base.tostring((struct Node *)a->e1);
    char *s2 = a->e2->base.tostring((struct Node *)a->e2);
    char *op = token_to_string(a->base.base.op);

    size_t len = strlen(s1) + strlen(op) + strlen(s2) + 10;
    char *buf = mg_region_alloc(longterm_region,len);
    snprintf(buf, len, "%s %s %s", s1, op, s2);
    return buf;
}

struct Arith *arith_new(struct lexer_token *tok, struct Expr *e1, struct Expr *e2)
{
    struct Arith *a = mg_region_alloc(longterm_region,sizeof(struct Arith));

    a->base.base.op   = tok;
    a->base.base.type = type_max(e1->type, e2->type);

    if (!a->base.base.type)
        node_error((struct Node *)a, "type error");

    a->e1 = e1;
    a->e2 = e2;

    a->base.base.base.tag = TAG_ARITH;

    a->base.base.base.gen      = (void *)expr_gen;
    a->base.base.base.jumping  = expr_jumping;
    a->base.base.base.tostring = arith_tostring;

    return a;
}

static char *bitand_tostring(struct Node *self)
{
    struct BitAnd *b = (struct BitAnd *)self;

    char *s1 = b->e1->base.tostring((struct Node *)b->e1);
    char *s2 = b->e2->base.tostring((struct Node *)b->e2);
    char *op = token_to_string(b->base.base.op);

    size_t len = strlen(s1) + strlen(op) + strlen(s2) + 10;
    char *buf = mg_region_alloc(longterm_region,len);
    snprintf(buf, len, "%s %s %s", s1, op, s2);
    return buf;
}

struct BitAnd *bitand_new(struct lexer_token *tok, struct Expr *e1, struct Expr *e2)
{
    struct BitAnd *b = mg_region_alloc(longterm_region,sizeof(struct BitAnd));

    b->base.base.op   = tok;
    b->base.base.type = type_max(e1->type, e2->type);

    if (!b->base.base.type)
        node_error((struct Node *)b, "type error");

    b->e1 = e1;
    b->e2 = e2;

    b->base.base.base.gen      = (void *)expr_gen;
    b->base.base.base.jumping  = expr_jumping;
    b->base.base.base.tostring = arith_tostring;

    return b;
}

static char *bitor_tostring(struct Node *self)
{
    struct BitOr *b = (struct BitOr *)self;

    char *s1 = b->e1->base.tostring((struct Node *)b->e1);
    char *s2 = b->e2->base.tostring((struct Node *)b->e2);
    char *op = token_to_string(b->base.base.op);

    size_t len = strlen(s1) + strlen(op) + strlen(s2) + 10;
    char *buf = mg_region_alloc(longterm_region,len);
    snprintf(buf, len, "%s %s %s", s1, op, s2);
    return buf;
}

struct BitOr *bitor_new(struct lexer_token *tok, struct Expr *e1, struct Expr *e2)
{
    struct BitOr *b = mg_region_alloc(longterm_region,sizeof(struct BitOr));

    b->base.base.op   = tok;
    b->base.base.type = type_max(e1->type, e2->type);

    if (!b->base.base.type)
        node_error((struct Node *)b, "type error");

    b->e1 = e1;
    b->e2 = e2;

    b->base.base.base.gen      = (void *)expr_gen;
    b->base.base.base.jumping  = expr_jumping;
    b->base.base.base.tostring = bitor_tostring;

    return b;
}

static char *logical_tostring(struct Node *self)
{
    struct Logical *l = (struct Logical *)self;

    char *s1 = l->e1->base.tostring((struct Node *)l->e1);
    char *s2 = l->e2->base.tostring((struct Node *)l->e2);
    char *op = token_to_string(l->base.op);

    size_t len = strlen(s1) + strlen(op) + strlen(s2) + 10;
    char *buf = mg_region_alloc(longterm_region,len);
    snprintf(buf, len, "%s %s %s", s1, op, s2);
    return buf;
}

struct Logical *logical_new(struct lexer_token *tok, struct Expr *e1, struct Expr *e2)
{
    struct Logical *l = mg_region_alloc(longterm_region,sizeof(struct Logical));

    l->base.op   = tok;
    l->base.type = Type_Bool;

    l->e1 = e1;
    l->e2 = e2;

    l->base.base.tag = TAG_LOGICAL;

    l->base.base.gen      = (void *)expr_gen;
    l->base.base.jumping  = expr_jumping;
    l->base.base.tostring = logical_tostring;

    return l;
}

static void and_jumping(struct Node *self, int t, int f)
{
    struct And *a = (struct And *)self;

    int label = f != 0 ? f : node_newlabel();

    a->base.e1->base.jumping((struct Node*)a->base.e1, 0, label);

    a->base.e2->base.jumping((struct Node*)a->base.e2, t, f);

    if (f == 0)
        node_emitlabel(label);
}

struct And *and_new(struct lexer_token *tok, struct Expr *e1, struct Expr *e2)
{
    struct And *a = mg_region_alloc(longterm_region,sizeof(struct And));

    a->base.base.op   = tok;
    a->base.base.type = Type_Bool;

    a->base.e1 = e1;
    a->base.e2 = e2;

    a->base.base.base.gen      = (void *)expr_gen;
    a->base.base.base.tostring = logical_tostring;
    a->base.base.base.jumping  = and_jumping;

    return a;
}

static void or_jumping(struct Node *self, int t, int f)
{
    struct Or *o = (struct Or *)self;

    int label = t != 0 ? t : node_newlabel();

    o->base.e1->base.jumping((struct Node*)o->base.e1, label, 0);

    o->base.e2->base.jumping((struct Node*)o->base.e2, t, f);

    if (t == 0)
        node_emitlabel(label);
}

struct Or *or_new(struct lexer_token *tok, struct Expr *e1, struct Expr *e2)
{
    struct Or *o = mg_region_alloc(longterm_region,sizeof(struct Or));

    o->base.base.op   = tok;
    o->base.base.type = Type_Bool;

    o->base.e1 = e1;
    o->base.e2 = e2;

    o->base.base.base.gen      = (void *)expr_gen;
    o->base.base.base.tostring = logical_tostring;
    o->base.base.base.jumping  = or_jumping;

    return o;
}

static void not_jumping(struct Node *self, int t, int f)
{
    struct Not *n = (struct Not *)self;
    n->base.e1->base.jumping((struct Node*)n->base.e1, f, t);
}


static char *not_tostring(struct Node *self)
{
    struct Not *n = (struct Not *)self;

    char *op = token_to_string(n->base.base.op);
    char *s  = n->base.e1->base.tostring((struct Node *)n->base.e1);

    size_t len = strlen(op) + strlen(s) + 5;
    char *buf = mg_region_alloc(longterm_region,len);
    snprintf(buf, len, "%s %s", op, s);
    return buf;
}


struct Not *not_new(struct lexer_token *tok, struct Expr *x)
{
    struct Not *n = mg_region_alloc(longterm_region,sizeof(struct Not));

    n->base.base.op   = tok;
    n->base.base.type = Type_Bool;

    n->base.e1 = x;
    n->base.e2 = x;

    n->base.base.base.gen      = (void *)expr_gen;
    n->base.base.base.jumping  = not_jumping;
    n->base.base.base.tostring = not_tostring;

    return n;
}

static void rel_jumping(struct Node *self, int t, int f)
{
    struct Rel *r = (struct Rel *)self;

    expr_gen((struct Node *)r->base.e1);
    expr_gen((struct Node *)r->base.e2);

    if (f != 0) {
        struct IR ir = {0};
        ir.op    = IR_IF_FALSE;
        ir.label = f;

        switch (r->relop) {
            case AST_LT:
                ir.src1  = r->base.e2->temp_no;
                ir.src2  = r->base.e1->temp_no;
                ir.relop = IR_GT;
                break;

            case AST_LE:
                ir.src1  = r->base.e2->temp_no;
                ir.src2  = r->base.e1->temp_no;
                ir.relop = IR_GE;
                break;

            case AST_GT:
                ir.src1  = r->base.e1->temp_no;
                ir.src2  = r->base.e2->temp_no;
                ir.relop = IR_GT;
                break;

            case AST_GE:
                ir.src1  = r->base.e1->temp_no;
                ir.src2  = r->base.e2->temp_no;
                ir.relop = IR_GE;
                break;

            case AST_EQ:
                ir.src1  = r->base.e1->temp_no;
                ir.src2  = r->base.e2->temp_no;
                ir.relop = IR_EQ;
                break;

            case AST_NE:
                ir.src1  = r->base.e1->temp_no;
                ir.src2  = r->base.e2->temp_no;
                ir.relop = IR_NE;
                break;
        }

        ir_emit(ir);
    }

    char *s1 = r->base.e1->base.tostring((struct Node *)r->base.e1);
    char *s2 = r->base.e2->base.tostring((struct Node *)r->base.e2);
    char *op = token_to_string(r->base.base.op);

    size_t len = strlen(s1) + strlen(op) + strlen(s2) + 5;
    char *test = mg_region_alloc(longterm_region, len);
    snprintf(test, len, "%s %s %s", s1, op, s2);

    node_emit_jumps(test, t, f);
}

struct Rel *rel_new(struct lexer_token *tok, struct Expr *e1, struct Expr *e2)
{
    struct Rel *r = mg_region_alloc(longterm_region,sizeof(struct Rel));

    r->base.base.op   = tok;
    r->base.base.type = Type_Bool;

    r->base.e1 = e1;
    r->base.e2 = e2;

    switch (tok->tag) {
        case LT:  r->relop = AST_LT; break;
        case LE:  r->relop = AST_LE; break;
        case GT:  r->relop = AST_GT; break;
        case GE:  r->relop = AST_GE; break;
        case EQ:  r->relop = AST_EQ; break;
        case NE:  r->relop = AST_NE; break;
        default:
            node_error((struct Node*)r, "unknown relational operator");
    }

    r->base.base.base.tag      = TAG_REL;
    r->base.base.base.gen      = (void *)expr_gen;
    r->base.base.base.tostring = logical_tostring;
    r->base.base.base.jumping  = rel_jumping;

    return r;
}

/* STRING */
char *string_pool[STRING_POOL_SIZE];
int string_pool_count = 0;

int intern_string(const char *s)
{
    int id = string_pool_count++;

    size_t n = strlen(s) + 1;
    char *p = mg_region_alloc(string_region, n);
    memcpy(p, s, n);

    string_pool[id] = p;
    return id;
}

void string_count_stats()
{
    printf("string_pool_count:%u\r\n", string_pool_count);
}

static char *unescape_c_string(const char *s)
{
    size_t len = strlen(s);
    char *out = mg_region_alloc(longterm_region,len + 1);
    if (!out) return NULL;

    char *w = out;
    const char *r = s;

    while (*r) {
        if (*r == '\\') {
            r++;
            switch (*r) {
                case 'n':
                    *w++ = '\n';
                    r++;
                    break;
                case 't':
                    *w++ = '\t';
                    r++;
                    break;
                case '\\':
                    *w++ = '\\';
                    r++;
                    break;
                case '\"':
                    *w++ = '\"';
                    r++;
                    break;
                case '\0':
                    goto done;
                default:
                    *w++ = '\\';
                    *w++ = *r++;
                    break;
            }
        } else {
            *w++ = *r++;
        }
    }

    done:
    *w = '\0';
    return out;
}

struct Expr *string_literal_new(const char *s)
{
    struct StringLiteral *sl = mg_region_alloc(longterm_region,sizeof(*sl));

    sl->base.base.tag = TAG_STRING;
    sl->base.op       = NULL;
    sl->base.type     = Type_Int;
    sl->base.temp_no  = 0;

    char *unescaped = unescape_c_string(s);
    sl->str_id = intern_string(unescaped);

    return (struct Expr *)sl;
}

static char *access_tostring(struct Node *self)
{
    struct Access *a = (struct Access *)self;

    char *arr = a->array->base.tostring((struct Node *)a->array);
    char *idx = a->index->base.tostring((struct Node *)a->index);

    size_t len = strlen(arr) + strlen(idx) + 10;
    char *buf = mg_region_alloc(longterm_region,len);
    snprintf(buf, len, "%s [ %s ]", arr, idx);
    return buf;
}

struct Access *access_new(struct Expr *array, struct Expr *index, struct Type *type)
{
    struct Access *a = mg_region_alloc(longterm_region,sizeof(struct Access));

    a->base.base.op   = NULL;
    a->base.base.type = type;

    a->array = array;
    a->index = index;

    struct Id *id = (struct Id *)array;
    a->slot = id->offset;
    a->width = type->width;

    a->base.base.base.tag = TAG_ACCESS;
    a->base.base.base.gen      = (void *)expr_gen;
    a->base.base.base.jumping  = expr_jumping;
    a->base.base.base.tostring = access_tostring;

    return a;
}

struct Id *id_new_from_name(const char *name, struct Type *ty, int offset)
{
    struct lexer_token *tok = mg_region_alloc(longterm_region,sizeof(*tok));
    if (!tok)
        printf("out of memory in id_new_from_name");

    memset(tok, 0, sizeof(*tok));
    tok->tag = ID;
    tok->lexeme = region_strdup(name);
    if (!tok->lexeme)
        printf("out of memory in id_new_from_name lexeme");

    return id_new(tok, ty, offset);
}

static char *id_tostring(struct Node *self)
{
    struct Id *i = (struct Id *)self;
    return token_to_string(i->base.op);
}

struct Id *id_new(struct lexer_token *word, struct Type *type, int offset)
{
    struct Id *id = mg_region_alloc(longterm_region,sizeof(*id));

    id->base.op   = word;
    id->base.type = type;
    id->offset    = offset;

    id->base.base.tag      = TAG_ID;
    id->base.base.gen      = (void *)expr_gen;
    id->base.base.jumping  = expr_jumping;
    id->base.base.tostring = id_tostring;

    id->is_ctx_ptr = 0;

    if (word && word->lexeme) {
        if (strcmp(word->lexeme, "ctx") == 0)
            id->is_ctx_ptr = 1;
    }

    return id;
}

static void seq_gen(struct Node *self, int b, int a)
{
    struct Seq *s = (struct Seq *)self;

    if (s->s1 == Stmt_Null)
        s->s2->base.gen((struct Node *)s->s2, b, a);
    else if (s->s2 == Stmt_Null)
        s->s1->base.gen((struct Node *)s->s1, b, a);
    else {
        int label = node_newlabel();
        s->s1->base.gen((struct Node *)s->s1, b, label);
        node_emitlabel(label);
        s->s2->base.gen((struct Node *)s->s2, label, a);
    }
}

struct Seq *seq_new(struct Stmt *s1, struct Stmt *s2)
{
    struct Seq *s = mg_region_alloc(longterm_region,sizeof(struct Seq));
    s->s1 = s1;
    s->s2 = s2;

    s->base.base.tag = TAG_SEQ;
    s->base.base.gen = seq_gen;
    return s;
}

static void if_gen(struct Node *self, int b, int a)
{
    struct If *i = (struct If *)self;

    int Lthen = node_newlabel();
    int Lelse = node_newlabel();
    int Lend  = node_newlabel();

    // if (!cond) goto Lelse
    i->expr->base.jumping((struct Node *)i->expr, 0, Lelse);

    // then:
    node_emitlabel(Lthen);
    i->stmt->base.gen((struct Node *)i->stmt, Lthen, a);

    // goto end
    struct IR ir = {0};
    ir.op    = IR_GOTO;
    ir.label = Lend;
    ir_emit(ir);

    // else:
    node_emitlabel(Lelse);

    // end:
    node_emitlabel(Lend);
}

struct If *if_new(struct Expr *expr, struct Stmt *stmt)
{
    struct If *i = mg_region_alloc(longterm_region,sizeof(struct If));

    i->expr = expr;
    i->stmt = stmt;

    i->base.base.tag = TAG_IF;
    i->base.base.gen = if_gen;

    return i;
}

static void else_gen(struct Node *self, int b, int a)
{
    struct Else *e = (struct Else *)self;

    int Lthen = node_newlabel();
    int Lelse = node_newlabel();
    int Lend  = node_newlabel();

    e->expr->base.jumping((struct Node *)e->expr, 0, Lelse);

    node_emitlabel(Lthen);
    e->stmt1->base.gen((struct Node *)e->stmt1, Lthen, a);

    struct IR ir = {0};
    ir.op    = IR_GOTO;
    ir.label = Lend;
    ir_emit(ir);

    node_emitlabel(Lelse);
    e->stmt2->base.gen((struct Node *)e->stmt2, Lelse, a);

    node_emitlabel(Lend);
}

struct Else *else_new(struct Expr *expr, struct Stmt *s1, struct Stmt *s2)
{
    struct Else *e = mg_region_alloc(longterm_region,sizeof(struct Else));

    e->expr  = expr;
    e->stmt1 = s1;
    e->stmt2 = s2;

    e->base.base.tag = TAG_ELSE;
    e->base.base.gen = else_gen;

    return e;
}

static void set_gen(struct Node *self, int b, int a)
{
    struct Set *s = (struct Set *)self;

    expr_gen((struct Node *)s->expr);

    struct IR ir = {0};
    ir.op          = IR_STORE;
    ir.array_base  = s->id->offset;
    ir.array_index = 0;
    ir.array_width = s->id->base.type->width;
    ir.src1        = s->expr->temp_no;
    ir_emit(ir);

    char *lhs = s->id->base.base.tostring((struct Node *)s->id);
    char *rhs = s->expr->base.tostring((struct Node *)s->expr);
    node_emit("%s = %s", lhs, rhs);
}

struct Set *set_new(struct Id *id, struct Expr *expr)
{
    struct Set *s = mg_region_alloc(longterm_region,sizeof(struct Set));

    s->id   = id;
    s->expr = expr;

    s->base.base.tag = TAG_SET;
    s->base.base.gen = set_gen;

    return s;
}

static void setelem_gen(struct Node *self, int b, int a)
{
    struct SetElem *s = (struct SetElem *)self;

    if (s->index->base.tag != TAG_CONSTANT) {
        fprintf(stderr, "Error: non-constant array index not supported in MVP SetElem.\n");
        exit(1);
    }

    struct Constant *cidx = (struct Constant *)s->index;
    int idx = cidx->int_val;
    int elem_offset = s->slot + idx * s->width;

    expr_gen((struct Node *)s->expr);

    char *arr = s->array->base.base.tostring((struct Node *)s->array);
    char *idx_str = s->index->base.tostring((struct Node *)s->index);
    char *val = s->expr->base.tostring((struct Node *)s->expr);

    node_emit("%s [ %s ] = %s", arr, idx_str, val);

    struct IR ir = {0};
    ir.op          = IR_STORE;
    ir.array_base  = elem_offset;
    ir.array_index = 0;
    ir.array_width = s->width;
    ir.src1        = s->expr->temp_no;
    ir_emit(ir);
}

struct SetElem *setelem_new(struct Access *x, struct Expr *y)
{
    struct SetElem *s = mg_region_alloc(longterm_region,sizeof(struct SetElem));

    s->array = (struct Id *)x->array;
    s->index = x->index;
    s->expr  = y;

    s->slot = x->slot;
    s->width = x->width;

    s->base.base.tag = TAG_SETELEM;
    s->base.base.gen = setelem_gen;

    return s;
}

static char *unary_tostring(struct Node *self)
{
    struct Unary *u = (struct Unary *)self;

    char *op = token_to_string(u->base.base.op);
    char *s  = u->expr->base.tostring((struct Node *)u->expr);

    size_t len = strlen(op) + strlen(s) + 5;
    char *buf = mg_region_alloc(longterm_region,len);
    snprintf(buf, len, "%s %s", op, s);
    return buf;
}

struct Unary *unary_new(struct lexer_token *tok, struct Expr *expr)
{
    struct Unary *u = mg_region_alloc(longterm_region,sizeof(struct Unary));

    u->base.base.op   = tok;
    u->base.base.type = type_max(Type_Int, expr->type);

    u->expr = expr;

    u->base.base.base.tag = TAG_UNARY;

    u->base.base.base.gen      = (void *)expr_gen;
    u->base.base.base.jumping  = expr_jumping;
    u->base.base.base.tostring = unary_tostring;

    return u;
}