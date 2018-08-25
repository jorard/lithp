#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef _WIN32
#define BUFFER_SIZE 2048
#include <string.h>

static char buffer[BUFFER_SIZE];

char* readline(char* prompt) {
    fputs(prompt, stdout);
    fgets(buffer, BUFFER_SIZE, stdin);
    char* cpy = malloc(strlen(buffer) + 1);
    strcpy(cpy, buffer);
    cpy[strlen(cpy) - 1] = '\0';

    return cpy;
}

void add_history(char* _) { }

#else
#include <editline/readline.h>
#include <editline/history.h>
#endif

#include "vendor/mpc.h"

/* struct / enum definitions */
typedef struct lval {
    int type;
    long num;
    char* err;
    char* sym;
    int count;
    struct lval** cell;
} lval;

enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR };

/* forward declarations */
void lval_print(lval* v);
lval* lval_eval(lval* v);

/* function definitions */
lval* lval_num(long x) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;

    return v;
}

lval* lval_err(char* message) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_ERR;
    v->err = malloc(strlen(message) + 1);
    strcpy(v->err, message);

    return v;
}

lval* lval_sym(char* symbol) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SYM;
    v->sym = malloc(strlen(symbol) + 1);
    strcpy(v->sym, symbol);

    return v;
}

lval* lval_sexpr(void) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;

    return v;
}

lval* lval_qexpr(void) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_QEXPR;
    v->count = 0;
    v->cell = NULL;

    return v;
}

void lval_del(lval* v) {
    switch (v->type) {
        case LVAL_NUM:
            break;
        case LVAL_ERR:
            free(v->err);
            break;
        case LVAL_SYM:
            free(v->sym);
            break;
        case LVAL_SEXPR:
        case LVAL_QEXPR:
            for (int i = 0; i < v->count; i++) {
                lval_del(v->cell[i]);
            }
            free(v->cell);
            break;
    }

    free(v);
}

lval* lval_read_num(mpc_ast_t* tree) {
    errno = 0;
    long x = strtol(tree->contents, NULL, 10);
    
    return errno != ERANGE
        ? lval_num(x)
        : lval_err("Invalid Number!");
}

lval* lval_read(mpc_ast_t* tree) {
    if (strstr(tree->tag, "number")) {
        return lval_read_num(tree);
    }

    if (strstr(tree->tag, "symbol")) {
        return lval_sym(tree->contents);
    }

    lval* expr;
    if (strstr(tree->tag, "qexpr")) {
        expr = lval_qexpr();
    } else {
        expr = lval_sexpr();
    }

    // lval* expr = strstr(tree->tag, "qexpr") != NULL
        // ? lval_qexpr()
        // : lval_sexpr();

    for (int i = 0; i < tree->children_num; i++) {
        char* contents = tree->children[i]->contents;
        bool isParens = strcmp(contents, "(") == 0 
                        || strcmp(contents, ")") == 0
                        || strcmp(contents, "{") == 0
                        || strcmp(contents, "}") == 0;
        bool isRegex = strcmp(tree->children[i]->tag, "regex") == 0;

        if (isParens || isRegex) {
            continue;
        }

        expr->count++;
        expr->cell = realloc(expr->cell, sizeof(lval*) * expr->count);
        expr->cell[expr->count - 1] = lval_read(tree->children[i]);
    }

    return expr;
}

void lval_expr_print(lval* v, char open, char close) {
    putchar(open);

    for (int i = 0; i < v->count; i++) {
        lval_print(v->cell[i]);

        if (i != (v->count - 1)) {
            putchar(' ');
        }
    }

    putchar(close);
}

void lval_print(lval* v) {
    switch (v->type) {
        case LVAL_NUM:
            printf("%li", v->num);
            break;
        case LVAL_ERR:
            printf("Error: %s", v->err);
            break;
        case LVAL_SYM:
            printf("%s", v->sym);
            break;
        case LVAL_SEXPR:
            lval_expr_print(v, '(', ')');
            break;
        case LVAL_QEXPR:
            lval_expr_print(v, '{', '}');
            break;
    }
}

void lval_println(lval* v) {
    lval_print(v);
    putchar('\n');
}

lval* lval_pop(lval* v, int i) {
    lval* value = v->cell[i];
    int new_size = sizeof(lval*) * (v->count - 1);
    memmove(&v->cell[i], &v->cell[i + 1], new_size);
    v->count--;
    v->cell = realloc(v->cell, new_size);

    return value;
}

lval* lval_take(lval* v, int i) {
    lval* value = lval_pop(v, i);
    lval_del(v);

    return value;
}

lval* builtin_op(lval* a, char* operator) {
    for (int i = 0; i < a->count; i++) {
        if (a->cell[i]->type != LVAL_NUM) {
            lval_del(a);
            
            return lval_err("Cannot operate on a non-number!");
        }
    }

    lval* x = lval_pop(a, 0);

    // unary negation
    if ((strcmp(operator, "-") == 0) && a->count == 0) {
        x->num = -x->num;
    }

    while (a->count > 0) {
        lval* y = lval_pop(a, 0);

        if (strcmp(operator, "+") == 0) {
            x->num += y->num;
        }

        if (strcmp(operator, "-") == 0) {
            x->num -= y->num;
        }

        if (strcmp(operator, "*") == 0) {
            x->num *= y->num;
        }

        if (strcmp(operator, "/") == 0) {
            if (y->num == 0) {
                lval_del(x);
                lval_del(y);
                x = lval_err("Division by Zero!");
                break;
            }

            x->num /= y->num;
        }

        lval_del(y);
    }

    lval_del(a);
    
    return x;
}

lval* lval_eval_sexpr(lval* v) {
    for (int i = 0; i < v->count; i++) {
        v->cell[i] = lval_eval(v->cell[i]);
    }

    for (int i = 0; i < v->count; i++) {
        if (v->cell[i]->type == LVAL_ERR) {
            return lval_take(v, i);
        }
    }

    if (v->count == 0) {
        return v;
    }

    if (v->count == 1) {
        return lval_take(v, 0);
    }

    lval* f = lval_pop(v, 0);
    if (f->type != LVAL_SYM) {
        lval_del(f);
        lval_del(v);
        return lval_err("S-expression does not start with a symbol!");
    }

    lval* result = builtin_op(v, f->sym);
    lval_del(f);

    return result;
}

lval* lval_eval(lval* v) {
    if (v->type == LVAL_SEXPR) {
        return lval_eval_sexpr(v);
    }

    return v;
}

int node_count(mpc_ast_t* tree) {
    if (tree->children_num <= 0) {
        return 1;
    }

    int total = 1;
    for (int i = 0; i < tree->children_num; i++) {
        total = total + node_count(tree->children[i]);
    }

    return total;
}

lval* evaluate_operator(lval* x, char* operator, lval* y) {
    if (x->type == LVAL_ERR) {
        return x;
    }

    if (y->type == LVAL_ERR) {
        return y;
    }

    if (strcmp(operator, "+") == 0) {
        return lval_num(x->num + y->num);
    }
    if (strcmp(operator, "-") == 0) {
        return lval_num(x->num - y->num);
    }
    if (strcmp(operator, "*") == 0) {
        return lval_num(x->num * y->num);
    }
    if (strcmp(operator, "/") == 0) {
        return y->num == 0
            ? lval_err("Divide by Zero!")
            : lval_num(x->num / y->num);
    }

    return lval_err("Invalid Operator!");
}

lval* evaluate(mpc_ast_t* tree) {
    if (strstr(tree->tag, "number")) {
        return lval_read_num(tree);
    }

    char* operator = tree->children[1]->contents;
    
    lval* x = evaluate(tree->children[2]);

    int i = 3;
    while (strstr(tree->children[i]->tag, "expr")) {
        x = evaluate_operator(x, operator, evaluate(tree->children[i]));
        i++;
    }

    return x;
}

int main(int argc, char** argv) {
    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Symbol = mpc_new("symbol");
    mpc_parser_t* Sexpr = mpc_new("sexpr");
    mpc_parser_t* Qexpr = mpc_new("qexpr");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Lithp = mpc_new("lithp");

    mpca_lang(MPCA_LANG_DEFAULT,"                                   \
        number      : /-?[0-9]+/ ;                                  \
        symbol      : '+' | '-' | '*' | '/'                         \
                    | \"list\" | \"head\" | \"tail\" |              \
                    | \"join\" | \"eval\" ;                         \
        sexpr       : '(' <expr>* ')' ;                             \
        qexpr       : '{' <expr>* '}' ;                             \
        expr        : <number> | <symbol> | <sexpr> | <qexpr> ;     \
        lithp       : /^/ <expr>* /$/ ;                             \
    ", Number, Symbol, Sexpr, Qexpr, Expr, Lithp);

    while (1) {
        char* input = readline("lithp >> ");
        add_history(input);

        mpc_result_t tree;

        if (mpc_parse("<stdin>", input, Lithp, &tree)) {
            lval* result = lval_eval(lval_read(tree.output));
            lval_println(result);
            // lval* x = lval_read(tree.output);
            // lval_println(x);
            lval_del(result);

            mpc_ast_delete(tree.output);
        } else {
            mpc_err_print(tree.error);
            mpc_err_delete(tree.error);
        }

        free(input);
    }

    mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lithp);

    return 0;
}
