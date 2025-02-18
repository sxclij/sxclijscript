#include <fcntl.h>
#include <stdbool.h>
#include <sys/resource.h>
#include <unistd.h>

#define STACK_SZ (128 * 1024 * 1024)
#define SRC "test/04"
#define MEM_SZ (1 << 20)
#define COMP_SZ (1 << 20)
#define BUF_SZ (1 << 10)
#define GLOB_SZ (1 << 8)
#define STK_SZ (1 << 10)

enum op {
    OP_NULL,
    OP_NOP,
    OP_PUSH_CONST,
    OP_PUSH_VARADDR,
    OP_TEST01,
    OP_TEST02,
    OP_TEST03,
    OP_GLOBAL_GET,
    OP_GLOBAL_SET,
    OP_CALL,
    OP_RETURN,
    OP_JMP,
    OP_JZE,
    OP_OR,
    OP_AND,
    OP_EQ,
    OP_NE,
    OP_LT,
    OP_GT,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_SVC,
    OP_LABEL,
    OP_LABEL_FNEND,
};

enum global {
    GLOBAL_NULL = 0,
    GLOBAL_IP = 1,
    GLOBAL_SP = 2,
    GLOBAL_BP = 3,
    GLOBAL_IO = 4,
};

struct token {
    const char* data;
    int size;
};

struct node {
    enum op op;
    struct token* token;
    int val;
};

struct label {
    struct token* token;
    int arg_size;
    int inst_index;
};

union mem {
    enum op op;
    int val;
};

void parse_expr(struct token** token_ptr, struct node** node_ptr, struct label* labels, int* lab_size, int lab_break, int lab_cont);

bool is_num(const char* str) {
    char ch = str[0];
    return ((ch >= '0' && ch <= '9') || ch == '-');
}

bool token_eq(struct token* a, struct token* b) {
    if (a->size != b->size)
        return false;
    for (int i = 0; i < a->size; i++) {
        if (a->data[i] != b->data[i])
            return false;
    }
    return true;
}

bool token_eq_str(struct token* a, const char* b) {
    for (int i = 0; i < a->size; i++) {
        if (a->data[i] != b[i] || b[i] == '\0')
            return false;
    }
    if (b[a->size] != '\0')
        return false;
    return true;
}

int token_to_int(struct token* token) {
    bool neg = token->data[0] == '-';
    int i = neg ? 1 : 0;
    int ret = 0;
    for (; i < token->size; i++) {
        ret = ret * 10 + token->data[i] - '0';
    }
    return neg ? -ret : ret;
}

void read_file(char* dst) {
    int fd = open(SRC, O_RDONLY);
    int n = read(fd, dst, COMP_SZ);
    dst[n] = '\0';
    close(fd);
}

void tokenize(const char* src, struct token* tokens) {
    struct token* t = tokens;
    *t = (struct token){src, 0};
    for (const char* p = src; *p != '\0'; p++) {
        if (*p == ' ' || *p == '\n') {
            if (t->size != 0)
                t++;
        } else if (*p == '(' || *p == ')' || *p == ',' ||
                   *p == '.' || *p == '*' || *p == '&') {
            if (t->size != 0)
                t++;
            *(t++) = (struct token){p, 1};
        } else {
            if (t->size == 0)
                t->data = p;
            t->size++;
        }
    }
    if (t->size != 0)
        t++;
    *t = (struct token){NULL, 0};
}

void push_node(struct node** node_ptr, enum op op, struct token* token, int val) {
    **node_ptr = (struct node){.op = op, .token = token, .val = val};
    (*node_ptr)++;
}

void parse_primary(struct token** token_ptr, struct node** node_ptr, struct label* labels, int* lab_size, int lab_break, int lab_cont) {
    if (token_eq_str(*token_ptr, "(")) {
        (*token_ptr)++;
        while (!token_eq_str(*token_ptr, ")")) {
            parse_expr(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
            if (token_eq_str(*token_ptr, ","))
                (*token_ptr)++;
        }
        (*token_ptr)++;
    } else if (is_num((*token_ptr)->data)) {
        push_node(node_ptr, OP_PUSH_CONST, *token_ptr, 0);
        (*token_ptr)++;
    } else {
        push_node(node_ptr, OP_PUSH_VARADDR, *token_ptr, 0);
        push_node(node_ptr, OP_GLOBAL_GET, NULL, 0);
        (*token_ptr)++;
    }
}

void parse_postfix(struct token** token_ptr, struct node** node_ptr, struct label* labels, int* lab_size, int lab_break, int lab_cont) {
    struct token* start = *token_ptr;
    if (token_eq_str((*token_ptr) + 1, "(")) {
        (*token_ptr)++;
        parse_primary(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
        if (token_eq_str(start, "return"))
            push_node(node_ptr, OP_RETURN, NULL, 0);
        else if (token_eq_str(start, "svc"))
            push_node(node_ptr, OP_SVC, NULL, 0);
        else
            push_node(node_ptr, OP_CALL, start, 0);
    } else {
        parse_primary(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
    }
}

void parse_unary(struct token** token_ptr, struct node** node_ptr, struct label* labels, int* lab_size, int lab_break, int lab_cont) {
    if (token_eq_str(*token_ptr, "&")) {
        (*token_ptr)++;
        push_node(node_ptr, OP_PUSH_VARADDR, *token_ptr, 0);
        (*token_ptr)++;
    } else if (token_eq_str(*token_ptr, "*")) {
        (*token_ptr)++;
        parse_postfix(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
        push_node(node_ptr, OP_GLOBAL_GET, NULL, 0);
    } else {
        parse_postfix(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
    }
}

void parse_mul(struct token** token_ptr, struct node** node_ptr, struct label* labels, int* lab_size, int lab_break, int lab_cont) {
    parse_unary(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
    while (true) {
        if (token_eq_str(*token_ptr, "*")) {
            (*token_ptr)++;
            parse_unary(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
            push_node(node_ptr, OP_MUL, NULL, 0);
        } else if (token_eq_str(*token_ptr, "/")) {
            (*token_ptr)++;
            parse_unary(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
            push_node(node_ptr, OP_DIV, NULL, 0);
        } else if (token_eq_str(*token_ptr, "%")) {
            (*token_ptr)++;
            parse_unary(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
            push_node(node_ptr, OP_MOD, NULL, 0);
        } else {
            break;
        }
    }
}

void parse_add(struct token** token_ptr, struct node** node_ptr, struct label* labels, int* lab_size, int lab_break, int lab_cont) {
    parse_mul(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
    while (true) {
        if (token_eq_str(*token_ptr, "+")) {
            (*token_ptr)++;
            parse_mul(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
            push_node(node_ptr, OP_ADD, NULL, 0);
        } else if (token_eq_str(*token_ptr, "-")) {
            (*token_ptr)++;
            parse_mul(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
            push_node(node_ptr, OP_SUB, NULL, 0);
        } else {
            break;
        }
    }
}

void parse_rel(struct token** token_ptr, struct node** node_ptr, struct label* labels, int* lab_size, int lab_break, int lab_cont) {
    parse_add(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
    while (true) {
        if (token_eq_str(*token_ptr, "<")) {
            (*token_ptr)++;
            parse_add(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
            push_node(node_ptr, OP_LT, NULL, 0);
        } else if (token_eq_str(*token_ptr, ">")) {
            (*token_ptr)++;
            parse_add(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
            push_node(node_ptr, OP_GT, NULL, 0);
        } else {
            break;
        }
    }
}

void parse_eq(struct token** token_ptr, struct node** node_ptr, struct label* labels, int* lab_size, int lab_break, int lab_cont) {
    parse_rel(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
    while (true) {
        if (token_eq_str(*token_ptr, "==")) {
            (*token_ptr)++;
            parse_rel(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
            push_node(node_ptr, OP_EQ, NULL, 0);
        } else if (token_eq_str(*token_ptr, "!=")) {
            (*token_ptr)++;
            parse_rel(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
            push_node(node_ptr, OP_NE, NULL, 0);
        } else {
            break;
        }
    }
}

void parse_and(struct token** token_ptr, struct node** node_ptr, struct label* labels, int* lab_size, int lab_break, int lab_cont) {
    parse_eq(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
    while (token_eq_str(*token_ptr, "&") && token_eq_str((*token_ptr) + 1, "&")) {
        *token_ptr += 2;
        parse_eq(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
        push_node(node_ptr, OP_AND, NULL, 0);
    }
}

void parse_or(struct token** token_ptr, struct node** node_ptr, struct label* labels, int* lab_size, int lab_break, int lab_cont) {
    parse_and(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
    while (token_eq_str(*token_ptr, "||")) {
        (*token_ptr)++;
        parse_and(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
        push_node(node_ptr, OP_OR, NULL, 0);
    }
}

void parse_assign(struct token** token_ptr, struct node** node_ptr, struct label* labels, int* lab_size, int lab_break, int lab_cont) {
    parse_or(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
    while (token_eq_str(*token_ptr, "=")) {
        (*token_ptr)++;
        parse_or(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
        push_node(node_ptr, OP_GLOBAL_SET, NULL, 0);
    }
}

void parse_expr(struct token** token_ptr, struct node** node_ptr, struct label* labels, int* lab_size, int lab_break, int lab_cont) {
    if (token_eq_str(*token_ptr, "if")) {
        int lab_if = (*lab_size)++;
        int lab_else = (*lab_size)++;
        (*token_ptr)++;
        parse_expr(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
        push_node(node_ptr, OP_JZE, NULL, lab_if);
        parse_expr(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
        if (token_eq_str(*token_ptr, "else")) {
            (*token_ptr)++;
            push_node(node_ptr, OP_JMP, NULL, lab_else);
            push_node(node_ptr, OP_LABEL, NULL, lab_if);
            parse_expr(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
            push_node(node_ptr, OP_LABEL, NULL, lab_else);
        } else {
            push_node(node_ptr, OP_LABEL, NULL, lab_if);
        }
    } else if (token_eq_str(*token_ptr, "loop")) {
        int lab_start = (*lab_size)++;
        int lab_end = (*lab_size)++;
        (*token_ptr)++;
        push_node(node_ptr, OP_LABEL, NULL, lab_start);
        parse_expr(token_ptr, node_ptr, labels, lab_size, lab_end, lab_start);
        push_node(node_ptr, OP_JMP, NULL, lab_start);
        push_node(node_ptr, OP_LABEL, NULL, lab_end);
    } else if (token_eq_str(*token_ptr, "break")) {
        (*token_ptr)++;
        push_node(node_ptr, OP_JMP, NULL, lab_break);
    } else if (token_eq_str(*token_ptr, "continue")) {
        (*token_ptr)++;
        push_node(node_ptr, OP_JMP, NULL, lab_cont);
    } else {
        parse_assign(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
    }
}

void parse_fn(struct token** token_ptr, struct node** node_ptr, struct label* labels, int* lab_size, int lab_break, int lab_cont) {
    if (token_eq_str(*token_ptr, "fn")) {
        int lab_fn = (*lab_size)++;
        int arg_size = 0;
        (*token_ptr)++;
        labels[lab_fn].token = *token_ptr;
        (*token_ptr) += 2;
        while (!token_eq_str(*token_ptr, ")")) {
            push_node(node_ptr, OP_PUSH_VARADDR, *token_ptr, 0);
            (*token_ptr)++;
            arg_size++;
            if (token_eq_str(*token_ptr, ","))
                (*token_ptr)++;
        }
        struct node* arg_itr = (*node_ptr) - 1;
        for (int i = 0; i < arg_size; i++) {
            arg_itr->val = -4 - i;
            arg_itr--;
        }
        (*token_ptr)++;
        push_node(node_ptr, OP_LABEL, NULL, lab_fn);
        push_node(node_ptr, OP_PUSH_VARADDR, NULL, -2);
        push_node(node_ptr, OP_PUSH_VARADDR, NULL, -2);
        push_node(node_ptr, OP_GLOBAL_GET, NULL, 0);
        push_node(node_ptr, OP_PUSH_CONST, NULL, arg_size);
        push_node(node_ptr, OP_SUB, NULL, 0);
        push_node(node_ptr, OP_GLOBAL_SET, NULL, 0);
        parse_expr(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
        push_node(node_ptr, OP_RETURN, NULL, 0);
        push_node(node_ptr, OP_LABEL_FNEND, NULL, 0);
    } else {
        parse_expr(token_ptr, node_ptr, labels, lab_size, lab_break, lab_cont);
    }
}

void parse_tokens(struct token* tokens, struct node* nodes, struct label* labels, int* lab_size) {
    struct token* token_ptr = tokens;
    struct node* node_ptr = nodes;
    while (token_ptr->data != NULL) {
        parse_fn(&token_ptr, &node_ptr, labels, lab_size, -1, -1);
    }
}

void analyze_push(struct node* nodes, struct token** locals, int* offsets) {
    int off = 0;
    int local_count = 0;
    for (struct node* n = nodes; n->op != OP_NULL; n++) {
        if (n->op == OP_LABEL_FNEND) {
            off = 0;
            local_count = 0;
            continue;
        }
        if (n->token == NULL)
            continue;
        if (n->op == OP_PUSH_CONST) {
            n->val = token_to_int(n->token);
        } else if (n->op == OP_PUSH_VARADDR) {
            for (int i = 0;; i++) {
                if (i == local_count) {
                    locals[local_count] = n->token;
                    if (n->val != 0)
                        offsets[local_count++] = n->val;
                    else
                        offsets[local_count++] = off;
                    n->val = off++;
                    break;
                }
                if (token_eq(locals[i], n->token)) {
                    n->val = offsets[i];
                    break;
                }
            }
        }
    }
}

int find_label(struct label* labels, int lab_size, struct node* n) {
    for (int i = 0; i < lab_size; i++) {
        if (labels[i].token == NULL)
            continue;
        if (token_eq(labels[i].token, n->token))
            return i;
    }
    return -1;
}

void to_instructions(union mem* mem, struct node* nodes, struct label* labels, int* lab_size) {
    union mem* iptr = mem + GLOB_SZ;
    for (struct node* n = nodes; n->op != OP_NULL; n++) {
        if (n->op == OP_LABEL) {
            labels[n->val].inst_index = iptr - mem;
        } else if (n->op == OP_PUSH_CONST || n->op == OP_PUSH_VARADDR || n->op == OP_JMP || n->op == OP_JZE) {
            *(iptr++) = (union mem){.op = n->op};
            *(iptr++) = (union mem){.val = n->val};
        } else if (n->op == OP_CALL) {
            *(iptr++) = (union mem){.op = n->op};
            *(iptr++) = (union mem){.val = find_label(labels, *lab_size, n)};
        } else if (n->op == OP_NOP) {
            continue;
        } else {
            *(iptr++) = (union mem){.op = n->op};
        }
    }
    mem[GLOBAL_IP].val = GLOB_SZ;
    mem[GLOBAL_BP].val = iptr - mem;
    mem[GLOBAL_SP].val = (iptr - mem) + STK_SZ;
}

void analyze_script(union mem* mem, struct node* nodes, struct token** locals, int* offsets, struct label* labels, int* lab_size) {
    analyze_push(nodes, locals, offsets);
    to_instructions(mem, nodes, labels, lab_size);
}

void link_instructions(union mem* mem, struct label* labels) {
    for (union mem* inst = mem + GLOB_SZ; inst->op != OP_NULL; inst++) {
        if (inst->op == OP_JMP || inst->op == OP_JZE || inst->op == OP_CALL) {
            inst++;
            inst->val = labels[inst->val].inst_index;
        } else if (inst->op == OP_PUSH_CONST || inst->op == OP_PUSH_VARADDR) {
            inst++;
        }
    }
}

void out_push(char* buf, int* size, char ch) {
    buf[(*size)++] = ch;
}

void out_memory(union mem* mem, char* buf) {
    int fd = open("Scratch.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    int size = 0;
    for (int i = 1; i < 200000; i++) {
        int x = mem[i].val;
        int m = 1000000000;
        if (x == 0) {
            out_push(buf, &size, '0');
            out_push(buf, &size, '\n');
            continue;
        }
        if (x < 0) {
            out_push(buf, &size, '-');
            x = -x;
        }
        while (x / m == 0)
            m /= 10;
        while (m != 0) {
            char ch = (x / m % 10) + '0';
            out_push(buf, &size, ch);
            m /= 10;
        }
        out_push(buf, &size, '\n');
    }
    write(fd, buf, size);
    close(fd);
}

void run_script(union mem* mem) {
    int a1;
    while (mem[mem[GLOBAL_IP].val].op != OP_NULL) {
        switch (mem[mem[GLOBAL_IP].val].op) {
            case OP_NULL:
                break;
            case OP_NOP:
                break;
            case OP_PUSH_CONST:
                (mem[GLOBAL_IP].val)++;
                mem[(mem[GLOBAL_SP].val)++].val = mem[mem[GLOBAL_IP].val].val;
                break;
            case OP_PUSH_VARADDR:
                (mem[GLOBAL_IP].val)++;
                mem[(mem[GLOBAL_SP].val)++].val = mem[GLOBAL_BP].val + mem[mem[GLOBAL_IP].val].val;
                break;
            case OP_TEST01:
                break;
            case OP_TEST02:
                break;
            case OP_TEST03:
                break;
            case OP_GLOBAL_GET:
                mem[mem[GLOBAL_SP].val - 1].val = mem[mem[mem[GLOBAL_SP].val - 1].val].val;
                break;
            case OP_GLOBAL_SET:
                mem[mem[mem[GLOBAL_SP].val - 2].val].val = mem[mem[GLOBAL_SP].val - 1].val;
                mem[GLOBAL_SP].val -= 2;
                break;
            case OP_CALL:
                mem[(mem[GLOBAL_SP].val) + 0].val = mem[GLOBAL_IP].val + 1;
                mem[(mem[GLOBAL_SP].val) + 1].val = mem[GLOBAL_SP].val;
                mem[(mem[GLOBAL_SP].val) + 2].val = mem[GLOBAL_BP].val;
                mem[GLOBAL_IP].val = mem[mem[GLOBAL_IP].val + 1].val - 1;
                mem[GLOBAL_BP].val = mem[GLOBAL_SP].val + 3;
                mem[GLOBAL_SP].val += STK_SZ;
                break;
            case OP_RETURN:
                a1 = mem[mem[GLOBAL_SP].val - 1].val;
                mem[GLOBAL_IP].val = mem[mem[GLOBAL_BP].val - 3].val;
                mem[GLOBAL_SP].val = mem[mem[GLOBAL_BP].val - 2].val;
                mem[GLOBAL_BP].val = mem[mem[GLOBAL_BP].val - 1].val;
                mem[mem[GLOBAL_SP].val].val = a1;
                mem[GLOBAL_SP].val++;
                break;
            case OP_JMP:
                mem[GLOBAL_IP].val = mem[mem[GLOBAL_IP].val + 1].val - 1;
                break;
            case OP_JZE:
                if (mem[mem[GLOBAL_SP].val - 1].val == 0)
                    mem[GLOBAL_IP].val = mem[mem[GLOBAL_IP].val + 1].val - 1;
                else
                    mem[GLOBAL_IP].val += 1;
                mem[GLOBAL_SP].val -= 1;
                break;
            case OP_OR:
                mem[mem[GLOBAL_SP].val - 2].val |= mem[mem[GLOBAL_SP].val - 1].val;
                mem[GLOBAL_SP].val -= 1;
                break;
            case OP_AND:
                mem[mem[GLOBAL_SP].val - 2].val &= mem[mem[GLOBAL_SP].val - 1].val;
                mem[GLOBAL_SP].val -= 1;
                break;
            case OP_EQ:
                mem[mem[GLOBAL_SP].val - 2].val =
                    (mem[mem[GLOBAL_SP].val - 2].val == mem[mem[GLOBAL_SP].val - 1].val);
                mem[GLOBAL_SP].val -= 1;
                break;
            case OP_NE:
                mem[mem[GLOBAL_SP].val - 2].val =
                    (mem[mem[GLOBAL_SP].val - 2].val != mem[mem[GLOBAL_SP].val - 1].val);
                mem[GLOBAL_SP].val -= 1;
                break;
            case OP_LT:
                mem[mem[GLOBAL_SP].val - 2].val =
                    (mem[mem[GLOBAL_SP].val - 2].val < mem[mem[GLOBAL_SP].val - 1].val);
                mem[GLOBAL_SP].val -= 1;
                break;
            case OP_GT:
                mem[mem[GLOBAL_SP].val - 2].val =
                    (mem[mem[GLOBAL_SP].val - 2].val > mem[mem[GLOBAL_SP].val - 1].val);
                mem[GLOBAL_SP].val -= 1;
                break;
            case OP_ADD:
                mem[mem[GLOBAL_SP].val - 2].val += mem[mem[GLOBAL_SP].val - 1].val;
                mem[GLOBAL_SP].val -= 1;
                break;
            case OP_SUB:
                mem[mem[GLOBAL_SP].val - 2].val -= mem[mem[GLOBAL_SP].val - 1].val;
                mem[GLOBAL_SP].val -= 1;
                break;
            case OP_MUL:
                mem[mem[GLOBAL_SP].val - 2].val *= mem[mem[GLOBAL_SP].val - 1].val;
                mem[GLOBAL_SP].val -= 1;
                break;
            case OP_DIV:
                mem[mem[GLOBAL_SP].val - 2].val /= mem[mem[GLOBAL_SP].val - 1].val;
                mem[GLOBAL_SP].val -= 1;
                break;
            case OP_MOD:
                mem[mem[GLOBAL_SP].val - 2].val %= mem[mem[GLOBAL_SP].val - 1].val;
                mem[GLOBAL_SP].val -= 1;
                break;
            case OP_SVC:
                a1 = mem[GLOBAL_IO].val;
                if (a1 == 0) {
                    read(STDIN_FILENO, &mem[mem[GLOBAL_SP].val - 1].val, 1);
                } else if (a1 == 1) {
                    write(STDOUT_FILENO, &mem[mem[GLOBAL_SP].val - 1].val, 1);
                } else if (a1 == 2) {
                    usleep(mem[mem[GLOBAL_SP].val - 1].val * 1000);
                }
                break;
            default:
                break;
        }
        (mem[GLOBAL_IP].val)++;
    }
}

void init_script(union mem* mem) {
    char src[COMP_SZ];
    char buf[COMP_SZ];
    struct token tokens[COMP_SZ / sizeof(struct token)];
    struct node nodes[COMP_SZ / sizeof(struct node)];
    struct label labels[COMP_SZ / sizeof(struct label)];
    struct token* locals[COMP_SZ / sizeof(struct token)];
    int offsets[COMP_SZ / sizeof(int)];
    int lab_size = 0;

    read_file(src);
    tokenize(src, tokens);
    parse_tokens(tokens, nodes, labels, &lab_size);
    analyze_script(mem, nodes, locals, offsets, labels, &lab_size);
    link_instructions(mem, labels);
    out_memory(mem, buf);
}

void run_vm(void) {
    static union mem mem[MEM_SZ];
    init_script(mem);
    run_script(mem);
}

void init_rlimit(void) {
    struct rlimit rlim = {.rlim_cur = STACK_SZ, .rlim_max = STACK_SZ};
    setrlimit(RLIMIT_STACK, &rlim);
}

int main(void) {
    init_rlimit();
    run_vm();
    return 0;
}
