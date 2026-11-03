// the external scanner, which lexes the way cheri's lexer does where tree-sitter's own lexer can't, since that lexer only tries the tokens the parser can take next, so it would split an operator cheri reads whole, reading f(x) <- 1 as f(x) < -1 and --a as - -a, and has no way to see the spacing rule, and which also sees the two things cheri decides by looking a token ahead: whether a ( follows an attribute's name, and whether one follows an if where an expression starts

#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"

#include <stdbool.h>

// what kind of operator an entry is, which decides whether it needs spacing, as in src/lexer/operators.hpp
enum Category { Flow, Arithmetic, Bitwise, Logical, Comparison, Equality, Pipe, Compound, Extra };

// no spelling is longer than five symbols, the longest being a wrapped operator's compound, like <+><-
#define MAX_SPELLING 5

// one operator: its spelling, its category, its binary level, which is 0 when it has none, whether it gets a compound form, and its external token when it's one the scanner lexes, or -1
typedef struct {
    char spelling[MAX_SPELLING + 1];
    enum Category category;
    int level;
    bool isCompoundable;
    int token;
} Operator;

// the table holds 213 operators, the 28 built-ins, 104 extra operators, and 81 compounds, and the number of them the scanner lexes itself, the ones that could split, where every scanner builds its own table, since editors make parsers on several threads at once
#define MAX_OPERATORS 256
typedef struct {
    Operator entries[MAX_OPERATORS];
    int size;
    int splittable;
} Table;

// the external tokens after the operators that could split, in the order grammar.js lists them: the invalid operator, which no rule takes, the empty token that says an attribute's name has no arguments after it, and four tokens that are never lexed, whose only use is to say where a yield's target can come next, where what can't be finished is, where an expression can start, and where a signature's first -> goes, which makes tree-sitter call the scanner there, so it sees an operator name touching it
enum Token { InvalidOperator, NoArguments, YieldTargetPosition, NeverLexed, ExpressionStart, SignatureArrow };

// the scanner only reads the operator characters, which are ASCII, so it measures, compares, and copies spellings itself instead of pulling in a libc that a WebAssembly build of the grammar might not have
static int length(const char *text) {
    int n = 0;
    while (text[n] != 0) n++;
    return n;
}

// checks whether two spellings are the same
static bool isSame(const char *a, const char *b) {
    int i = 0;
    while (a[i] != 0 && a[i] == b[i]) i++;
    return a[i] == b[i];
}

// checks whether a spelling holds a two-symbol flow token, <- -> or -|, which a wrapped operator can't
static bool hasFlowToken(const char *text) {
    for (int i = 0; text[i] != 0 && text[i + 1] != 0; i++) {
        char a = text[i];
        char b = text[i + 1];
        if ((a == '<' && b == '-') || (a == '-' && b == '>') || (a == '-' && b == '|')) return true;
    }
    return false;
}

// adds an entry to the table, in the order operatorTable in src/lexer/operators.hpp adds it, which is also the order of the externals in grammar.js
static void add(Table *table, const char *spelling, enum Category category, int level, bool isCompoundable) {
    Operator *entry = &table->entries[table->size++];
    int n = length(spelling);
    for (int i = 0; i <= n; i++) entry->spelling[i] = spelling[i];
    entry->category = category;
    entry->level = level;
    entry->isCompoundable = isCompoundable;
    entry->token = -1;
}

// finds an operator by its spelling, or -1 when there's none
static int lookup(const Table *table, const char *spelling) {
    for (int i = 0; i < table->size; i++) {
        if (isSame(table->entries[i].spelling, spelling)) return i;
    }
    return -1;
}

// builds the table the way operators.hpp builds it: hand-listed built-ins and named extra operators, then wrapped extra operators, then every compound
static void buildTable(Table *table) {
    add(table, "<-", Flow, 1, false);
    add(table, "->", Flow, 0, false);
    add(table, "-|", Flow, 0, false);
    add(table, "|>", Pipe, 16, false);
    add(table, "<|", Pipe, 17, false);
    add(table, "+", Arithmetic, 5, true);
    add(table, "-", Arithmetic, 5, true);
    add(table, "*", Arithmetic, 4, true);
    add(table, "/", Arithmetic, 4, true);
    add(table, "%", Arithmetic, 4, true);
    add(table, "&", Bitwise, 7, true);
    add(table, "|", Bitwise, 9, true);
    add(table, "^", Bitwise, 8, true);
    add(table, "~", Bitwise, 0, false);
    add(table, "<<", Bitwise, 6, true);
    add(table, ">>", Bitwise, 6, true);
    add(table, "&&", Logical, 12, true);
    add(table, "||", Logical, 14, true);
    add(table, "^^", Logical, 13, true);
    add(table, "!", Logical, 0, false);
    add(table, "<", Comparison, 10, false);
    add(table, "<=", Comparison, 10, false);
    add(table, ">", Comparison, 10, false);
    add(table, ">=", Comparison, 10, false);
    add(table, "=", Equality, 11, false);
    add(table, "!=", Equality, 11, false);
    add(table, "==", Equality, 11, false);
    add(table, "!==", Equality, 11, false);
    add(table, "**", Extra, 2, true);
    add(table, "***", Extra, 2, true);
    add(table, "//", Extra, 4, true);
    add(table, "%%", Extra, 4, true);
    add(table, "++", Extra, 5, true);
    add(table, "+++", Extra, 5, true);
    add(table, "--", Extra, 5, true);
    add(table, "<<<", Extra, 6, true);
    add(table, ">>>", Extra, 6, true);
    add(table, "&&&", Extra, 12, true);
    add(table, "^^^", Extra, 13, true);
    add(table, "|||", Extra, 14, true);
    add(table, "<=>", Extra, 10, false);
    add(table, ">>=", Extra, 15, false);
    add(table, "=<<", Extra, 15, false);
    add(table, "$", Extra, 15, false);
    add(table, "<$>", Extra, 15, false);
    add(table, "<~>", Extra, 15, false);
    add(table, "~>", Extra, 15, false);
    add(table, "<~", Extra, 15, false);
    // every single-symbol binary operator inside each wrapper, skipping anything with a flow token in it and anything already named, where an inner arithmetic or bitwise operator makes the wrapped one compoundable
    static const char wrappers[8][2] = {{'<', '>'}, {'<', '<'}, {'>', '>'}, {'|', '|'}, {'|', '>'}, {'<', '|'}, {'|', '<'}, {'>', '|'}};
    int inners[MAX_OPERATORS];
    int innerCount = 0;
    for (int i = 0; i < table->size; i++) {
        if (length(table->entries[i].spelling) == 1 && table->entries[i].level > 0) inners[innerCount++] = i;
    }
    for (int w = 0; w < 8; w++) {
        for (int k = 0; k < innerCount; k++) {
            const Operator *inner = &table->entries[inners[k]];
            char spelling[MAX_SPELLING + 1] = {wrappers[w][0], inner->spelling[0], wrappers[w][1], 0};
            if (hasFlowToken(spelling) || lookup(table, spelling) >= 0) continue;
            add(table, spelling, Extra, inner->level, inner->category == Arithmetic || inner->category == Bitwise);
        }
    }
    // the compounds, X<- for every entry above that gets one, in the order those entries were added
    int count = table->size;
    for (int i = 0; i < count; i++) {
        if (!table->entries[i].isCompoundable) continue;
        char spelling[MAX_SPELLING + 1];
        int n = length(table->entries[i].spelling);
        for (int j = 0; j < n; j++) spelling[j] = table->entries[i].spelling[j];
        spelling[n] = '<';
        spelling[n + 1] = '-';
        spelling[n + 2] = 0;
        add(table, spelling, Compound, 0, false);
    }
    // the operators that could split, which start with another operator followed by - ! or ~, the characters a prefix operator starts with, numbered in table order the way grammar.js picks them
    for (int i = 0; i < table->size; i++) {
        const char *spelling = table->entries[i].spelling;
        int n = length(spelling);
        for (int cut = 1; cut < n; cut++) {
            char prefix[MAX_SPELLING + 1];
            for (int j = 0; j < cut; j++) prefix[j] = spelling[j];
            prefix[cut] = 0;
            char next = spelling[cut];
            if (lookup(table, prefix) >= 0 && (next == '-' || next == '!' || next == '~')) {
                table->entries[i].token = table->splittable++;
                break;
            }
        }
    }
}

// flow tokens, pipes, compounds, and extra operators can't touch another operator without whitespace in between
static bool needsSpacing(const Table *table, int index) {
    enum Category category = table->entries[index].category;
    return category == Flow || category == Pipe || category == Compound || category == Extra;
}

// the characters operators are made of, every one of which is an operator on its own, so a run of them always lexes as operators
static bool isOperatorCharacter(int32_t c) {
    static const char characters[] = "<>-|+*/%&^~!=$";
    for (int i = 0; characters[i] != 0; i++) {
        if (c == characters[i]) return true;
    }
    return false;
}

// cheri's whitespace, which is what isspace accepts
static bool isSpace(int32_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// the longest operator spelled at the start of text, as maximal munch takes it, with its length in spelled
static int munch(const Table *table, const char *text, int available, int *spelled) {
    char spelling[MAX_SPELLING + 1];
    for (int n = available < MAX_SPELLING ? available : MAX_SPELLING; n > 0; n--) {
        for (int i = 0; i < n; i++) spelling[i] = text[i];
        spelling[n] = 0;
        int index = lookup(table, spelling);
        if (index >= 0) {
            *spelled = n;
            return index;
        }
    }
    *spelled = 0;
    return -1;
}

// makes a scanner, whose only state is its operator table
void *tree_sitter_cherimoya_external_scanner_create(void) {
    Table *table = ts_calloc(1, sizeof(Table));
    if (table != NULL) buildTable(table);
    return table;
}

// frees a scanner's table
void tree_sitter_cherimoya_external_scanner_destroy(void *payload) {
    ts_free(payload);
}

// a scanner keeps nothing between tokens but its table, which never changes, so there's nothing to save
unsigned tree_sitter_cherimoya_external_scanner_serialize(void *payload, char *buffer) {
    (void)payload;
    (void)buffer;
    return 0;
}

// and nothing to restore, since nothing was saved
void tree_sitter_cherimoya_external_scanner_deserialize(void *payload, const char *buffer, unsigned size) {
    (void)payload;
    (void)buffer;
    (void)size;
}

// a letter, digit, or _, which is what a word goes on with
static bool isWordCharacter(int32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// looks past whitespace and comments without making them part of the token, to see what comes next
static void lookPastSpace(TSLexer *lexer) {
    while (isSpace(lexer->lookahead) || lexer->lookahead == '#') {
        if (lexer->lookahead == '#') {
            while (lexer->lookahead != '\n' && !lexer->eof(lexer)) lexer->advance(lexer, false);
        } else {
            lexer->advance(lexer, false);
        }
    }
}

// checks for the name if followed by (, which cheri reports wherever an expression starts so it can point at match, since there is no if, and which the scanner sees there because a prefix operator could start an expression too, where the token ends after if
static bool isIfCall(TSLexer *lexer) {
    if (lexer->lookahead != 'i') return false;
    lexer->advance(lexer, false);
    if (lexer->lookahead != 'f') return false;
    lexer->advance(lexer, false);
    if (isWordCharacter(lexer->lookahead)) return false;
    lexer->mark_end(lexer);
    lookPastSpace(lexer);
    return lexer->lookahead == '(';
}

// lexes the empty token after an attribute's name when no ( follows it, reports if ( where an expression starts and a yield's target can't, and otherwise reads the run of operator characters at the cursor and ends the token at the longest operator it starts with, which is the invalid operator if it touches another operator when either needs spacing, and is lexed here if it could split, as the invalid operator when the parser can't take it, while any other operator is left to tree-sitter's lexer, which takes the same one, since a shorter one in its place never parses, where error recovery, which marks every token valid, even the invalid operator, only gets operators
bool tree_sitter_cherimoya_external_scanner_scan(void *payload, TSLexer *lexer, const bool *validSymbols) {
    const Table *table = payload;
    if (table == NULL) return false;
    int invalid = table->splittable + InvalidOperator;
    bool isRecovering = validSymbols[invalid];
    while (isSpace(lexer->lookahead)) lexer->advance(lexer, true);
    if (!isRecovering && validSymbols[table->splittable + NoArguments]) {
        lexer->mark_end(lexer);
        lookPastSpace(lexer);
        if (lexer->lookahead == '(') return false;
        lexer->result_symbol = table->splittable + NoArguments;
        return true;
    }
    if (lexer->lookahead == 'i') {
        bool isExpression = validSymbols[table->splittable + ExpressionStart] && !validSymbols[table->splittable + YieldTargetPosition];
        if (isRecovering || !isExpression || !isIfCall(lexer)) return false;
        lexer->result_symbol = invalid;
        return true;
    }
    if (!isOperatorCharacter(lexer->lookahead)) return false;
    // the run is read far enough to hold the longest operator and the one touching it, and the token ends where its own operator does
    char run[2 * MAX_SPELLING + 1];
    int runLength = 0;
    while (runLength < 2 * MAX_SPELLING && isOperatorCharacter(lexer->lookahead)) {
        run[runLength++] = (char)lexer->lookahead;
        run[runLength] = 0;
        lexer->advance(lexer, false);
        if (runLength <= MAX_SPELLING && lookup(table, run) >= 0) lexer->mark_end(lexer);
    }
    int spelled;
    int index = munch(table, run, runLength, &spelled);
    if (runLength > spelled) {
        int nextSpelled;
        int next = munch(table, run + spelled, runLength - spelled, &nextSpelled);
        if (needsSpacing(table, index) || needsSpacing(table, next)) {
            lexer->result_symbol = invalid;
            return true;
        }
    }
    int token = table->entries[index].token;
    if (token < 0) return false;
    lexer->result_symbol = validSymbols[token] ? token : invalid;
    return true;
}
