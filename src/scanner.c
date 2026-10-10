#include "typescript_scanner.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Inherited TypeScript externals occupy 0..9. Keep aligned with grammar.js.
enum AstroToken {
    FRONTMATTER_START = 10, FRONTMATTER_END, START_TAG_NAME, END_TAG_NAME,
    SELF_CLOSING_DELIMITER, IMPLICIT_END_TAG, FRAGMENT_OPEN, FRAGMENT_CLOSE,
    RAW_ATTRIBUTE, RAW_TEXT, RETURN_ASI,
};
enum TagMode { NORMAL, RAW, RCDATA, PLAINTEXT, VOID };

// Store the complete tag stack in the serialization budget. Never silently
// truncate it: excessive depth/name length produces a parse error instead of
// incorrect incremental state. Each entry is length, mode, name, length.
#define TAG_CAPACITY (TREE_SITTER_SERIALIZATION_BUFFER_SIZE - 3)
typedef struct {
    uint16_t length;
    bool frontmatter;
    uint8_t tags[TAG_CAPACITY];
} Scanner;

static unsigned top_at(const Scanner *s) { return s->length - s->tags[s->length - 1] - 3; }
static uint8_t top_mode(const Scanner *s) { return s->length ? s->tags[top_at(s) + 1] : NORMAL; }
static void pop(Scanner *s) { s->length = (uint16_t)top_at(s); }
static bool tag_is_at(const Scanner *s, unsigned at, const char *name) {
    size_t n = strlen(name);
    return s->tags[at] == n && memcmp(s->tags + at + 2, name, n) == 0;
}
static bool tag_is(const Scanner *s, const char *name) { return s->length && tag_is_at(s, top_at(s), name); }
static bool top_html(const Scanner *s) {
    if (!s->length || !s->tags[top_at(s)]) return false;
    uint8_t c = s->tags[top_at(s) + 2];
    return c >= 'a' && c <= 'z';
}
static bool in_list(const char *name, const char *list) {
    size_t n = strlen(name);
    for (const char *p = list; *p;) {
        const char *end = strchr(p, '|');
        size_t size = end ? (size_t)(end - p) : strlen(p);
        if (n == size && memcmp(name, p, n) == 0) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
}
static uint8_t mode_for(const char *name) {
    if (in_list(name, "area|base|br|col|embed|hr|img|input|link|meta|param|source|track|wbr")) return VOID;
    if (in_list(name, "script|style|math|iframe|noembed|noframes|xmp")) return RAW;
    if (in_list(name, "title|textarea")) return RCDATA;
    return strcmp(name, "plaintext") == 0 ? PLAINTEXT : NORMAL;
}
static bool push(Scanner *s, const char *name) {
    size_t n = strlen(name);
    if (n > UINT8_MAX || s->length + n + 3 > TAG_CAPACITY) return false;
    uint8_t *entry = s->tags + s->length;
    entry[0] = (uint8_t)n;
    entry[1] = mode_for(name);
    memcpy(entry + 2, name, n);
    entry[n + 2] = (uint8_t)n;
    s->length += (uint16_t)(n + 3);
    return true;
}
static bool name_start(int32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}
static bool name_char(int32_t c) {
    return name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == ':' || c == '.';
}
static bool read_name(TSLexer *lexer, char name[256]) {
    unsigned n = 0;
    if (!name_start(lexer->lookahead)) return false;
    do {
        if (n == 255) return false;
        name[n++] = (char)lexer->lookahead;
        advance(lexer);
    } while (name_char(lexer->lookahead));
    name[n] = 0;
    return true;
}
static bool emit(TSLexer *lexer, TSSymbol symbol) {
    lexer->result_symbol = symbol;
    lexer->mark_end(lexer);
    return true;
}
static bool is_line_terminator(int32_t c) {
    return c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029;
}
static bool scan_return_asi(TSLexer *lexer, bool *scanned_slash) {
    *scanned_slash = false;
    lexer->result_symbol = RETURN_ASI;
    lexer->mark_end(lexer);
    for (;;) {
        if (is_line_terminator(lexer->lookahead)) return true;
        if (iswspace(lexer->lookahead) || lexer->lookahead == 0xfeff) {
            skip(lexer);
            continue;
        }
        if (lexer->lookahead != '/') return false;
        advance(lexer);
        *scanned_slash = true;
        if (lexer->lookahead == '/') {
            advance(lexer);
            while (!lexer->eof(lexer) && !is_line_terminator(lexer->lookahead)) advance(lexer);
            return is_line_terminator(lexer->lookahead);
        }
        if (lexer->lookahead != '*') return false;
        advance(lexer);
        while (!lexer->eof(lexer)) {
            if (is_line_terminator(lexer->lookahead)) return true;
            if (lexer->lookahead == '*') {
                advance(lexer);
                if (lexer->lookahead == '/') {
                    advance(lexer);
                    break;
                }
            } else advance(lexer);
        }
        if (lexer->eof(lexer)) return false;
    }
}
static bool take(TSLexer *lexer, const char *text) {
    for (; *text; text++) {
        if (lexer->lookahead != *text) return false;
        advance(lexer);
    }
    return true;
}

static bool scan_name(Scanner *s, TSLexer *lexer, bool closing) {
    char name[256];
    if (!read_name(lexer, name)) return false;
    if (lexer->lookahead != '/' && lexer->lookahead != '>' && !iswspace(lexer->lookahead)) return false;
    if (closing) {
        if (!tag_is(s, name)) return false;
        pop(s);
    } else if (s->frontmatter || !push(s, name)) return false;
    return emit(lexer, closing ? END_TAG_NAME : START_TAG_NAME);
}

static bool closes_optional(const Scanner *s, const char *next) {
    if (tag_is(s, "li")) return strcmp(next, "li") == 0;
    if (tag_is(s, "dt") || tag_is(s, "dd")) return in_list(next, "dt|dd");
    if (tag_is(s, "p")) return in_list(next, "address|article|aside|blockquote|details|div|dl|fieldset|figcaption|figure|footer|form|h1|h2|h3|h4|h5|h6|header|hgroup|hr|main|menu|nav|ol|p|pre|section|table|ul");
    if (tag_is(s, "td") || tag_is(s, "th")) return in_list(next, "td|th|tr");
    if (tag_is(s, "tr")) return strcmp(next, "tr") == 0;
    if (tag_is(s, "thead") || tag_is(s, "tbody") || tag_is(s, "tfoot")) return in_list(next, "thead|tbody|tfoot");
    if (tag_is(s, "option")) return in_list(next, "option|optgroup");
    if (tag_is(s, "optgroup")) return strcmp(next, "optgroup") == 0;
    if (tag_is(s, "rt") || tag_is(s, "rp")) return in_list(next, "rt|rp");
    if (tag_is(s, "head")) return strcmp(next, "body") == 0;
    return false;
}
static bool has_ancestor(const Scanner *s, const char *name) {
    unsigned end = s->length;
    while (end) {
        unsigned at = end - s->tags[end - 1] - 3;
        if (tag_is_at(s, at, name)) return true;
        end = at;
    }
    return false;
}

static bool scan_raw(const Scanner *s, TSLexer *lexer) {
    uint8_t mode = top_mode(s);
    bool content = false;
    while (!lexer->eof(lexer)) {
        lexer->mark_end(lexer);
        if (mode == RCDATA && lexer->lookahead == '{') break;
        if (mode != PLAINTEXT && lexer->lookahead == '<') {
            advance(lexer);
            if (lexer->lookahead == '/') {
                advance(lexer);
                char name[256];
                if (read_name(lexer, name) && tag_is(s, name) &&
                    (lexer->lookahead == '>' || iswspace(lexer->lookahead))) {
                    lexer->result_symbol = RAW_TEXT;
                    return content;
                }
            }
        } else advance(lexer);
        content = true;
    }
    lexer->mark_end(lexer);
    lexer->result_symbol = RAW_TEXT;
    return content;
}

// Called after '<!' has been consumed. HTML comments are multiline, unlike
// JavaScript's legacy HTML-comment token, so Astro owns this delimiter.
static bool scan_html_comment(TSLexer *lexer) {
    if (!take(lexer, "--")) return false;
    unsigned dashes = 0;
    while (!lexer->eof(lexer)) {
        int32_t c = lexer->lookahead;
        advance(lexer);
        if (c == '>' && dashes >= 2) return emit(lexer, HTML_COMMENT);
        dashes = c == '-' ? dashes + 1 : 0;
    }
    return false;
}

// Only yield to the internal entity token for a syntactically complete
// reference. Stray ampersands remain text rather than triggering recovery.
static bool scan_entity(TSLexer *lexer) {
    advance(lexer); // '&'
    unsigned count = 0, limit = 30;
    bool numeric = lexer->lookahead == '#', hex = false;
    if (numeric) {
        advance(lexer);
        hex = lexer->lookahead == 'x' || lexer->lookahead == 'X';
        if (hex) advance(lexer);
        limit = hex ? 6 : 5;
    }
    while ((numeric && (hex ? ((lexer->lookahead >= '0' && lexer->lookahead <= '9') ||
                         (lexer->lookahead >= 'a' && lexer->lookahead <= 'f') ||
                         (lexer->lookahead >= 'A' && lexer->lookahead <= 'F')) :
                        (lexer->lookahead >= '0' && lexer->lookahead <= '9'))) ||
           (!numeric && ((lexer->lookahead >= 'a' && lexer->lookahead <= 'z') ||
                         (lexer->lookahead >= 'A' && lexer->lookahead <= 'Z')))) {
        count++;
        advance(lexer);
    }
    return count && count <= limit && lexer->lookahead == ';';
}

static bool scan_text(Scanner *s, TSLexer *lexer, const bool *valid) {
    bool content = false;
    lexer->result_symbol = JSX_TEXT;
    for (;;) {
        lexer->mark_end(lexer);
        if (lexer->eof(lexer) || lexer->lookahead == '{') return content;
        if (valid[FRONTMATTER_START] && lexer->lookahead == '-') {
            if (take(lexer, "---")) return content;
        } else if (lexer->lookahead == '<') {
            advance(lexer);
            if (lexer->lookahead == '>') {
                if (content) return true;
                if (!valid[FRAGMENT_OPEN] || !push(s, "")) return false;
                advance(lexer);
                return emit(lexer, FRAGMENT_OPEN);
            }
            bool closing = lexer->lookahead == '/';
            if (closing) advance(lexer);
            if (closing && lexer->lookahead == '>') {
                if (content) return true;
                if (valid[IMPLICIT_END_TAG] && top_html(s)) {
                    pop(s); lexer->result_symbol = IMPLICIT_END_TAG; return true;
                }
                if (!valid[FRAGMENT_CLOSE] || !tag_is(s, "")) return false;
                pop(s); advance(lexer);
                return emit(lexer, FRAGMENT_CLOSE);
            }
            if (name_start(lexer->lookahead)) {
                if (content) return true;
                char name[256];
                if (!read_name(lexer, name)) return false;
                if (valid[IMPLICIT_END_TAG] && top_html(s) &&
                    ((closing && !tag_is(s, name) && has_ancestor(s, name)) ||
                    (!closing && closes_optional(s, name)))) {
                    pop(s); lexer->result_symbol = IMPLICIT_END_TAG; return true;
                }
                return false;
            }
            if (!closing && lexer->lookahead == '!') {
                if (content) return true;
                advance(lexer);
                return valid[HTML_COMMENT] && scan_html_comment(lexer);
            }
        } else if (lexer->lookahead == '&') {
            if (scan_entity(lexer)) return content;
        } else advance(lexer);
        content = true;
    }
}

void *tree_sitter_astro_external_scanner_create(void) { return calloc(1, sizeof(Scanner)); }
void tree_sitter_astro_external_scanner_destroy(void *payload) { free(payload); }
unsigned tree_sitter_astro_external_scanner_serialize(void *payload, char *buffer) {
    const Scanner *s = payload;
    buffer[0] = (char)s->frontmatter;
    buffer[1] = (char)(s->length & 255);
    buffer[2] = (char)(s->length >> 8);
    memcpy(buffer + 3, s->tags, s->length);
    return s->length + 3;
}
void tree_sitter_astro_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    Scanner *s = payload;
    memset(s, 0, sizeof(*s));
    if (length < 3) return;
    unsigned n = (uint8_t)buffer[1] | ((unsigned)(uint8_t)buffer[2] << 8);
    if (n > TAG_CAPACITY || n + 3 != length) return;
    // Validate entry boundaries before allowing top_at() to index the stack.
    for (unsigned at = 0; at < n;) {
        unsigned size = (uint8_t)buffer[at + 3] + 3;
        if (at + size > n || (uint8_t)buffer[at + size + 2] != size - 3) return;
        at += size;
    }
    s->frontmatter = buffer[0] != 0;
    s->length = (uint16_t)n;
    memcpy(s->tags, buffer + 3, n);
}

bool tree_sitter_astro_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid) {
    Scanner *s = payload;
    if (valid[ERROR_RECOVERY]) return false;
    if (valid[RETURN_ASI]) {
        bool scanned_slash = false;
        if (scan_return_asi(lexer, &scanned_slash)) return true;
        // A failed slash probe consumed input. Let Tree-sitter retry from the start.
        if (scanned_slash) return false;
    }
    if (valid[IMPLICIT_END_TAG] && s->length &&
        (top_mode(s) == VOID || (lexer->eof(lexer) && top_html(s)))) {
        pop(s); lexer->result_symbol = IMPLICIT_END_TAG; return true;
    }
    if (valid[RAW_TEXT] && s->length &&
        (top_mode(s) == RAW || top_mode(s) == RCDATA || top_mode(s) == PLAINTEXT)) {
        return scan_raw(s, lexer);
    }
    if (valid[FRONTMATTER_START] || valid[FRONTMATTER_END]) {
        while (iswspace(lexer->lookahead) || lexer->lookahead == 0xfeff) skip(lexer);
        if (lexer->lookahead == '-') {
            if (!take(lexer, "---"))
                return valid[FRONTMATTER_START] && emit(lexer, JSX_TEXT);
            s->frontmatter = !s->frontmatter;
            return emit(lexer, s->frontmatter ? FRONTMATTER_START : FRONTMATTER_END);
        }
        if (s->frontmatter) return false;
    }
    // A closing fence may follow a statement without a physical semicolon.
    // Do not consume whitespace here: the upstream scanner needs newlines
    // for ordinary automatic semicolon insertion.
    if (s->frontmatter && valid[AUTOMATIC_SEMICOLON] && lexer->lookahead == '-') {
        lexer->mark_end(lexer);
        if (!take(lexer, "---")) return false;
        lexer->result_symbol = AUTOMATIC_SEMICOLON;
        return true;
    }
    if (valid[START_TAG_NAME] || valid[END_TAG_NAME] ||
        valid[SELF_CLOSING_DELIMITER] || valid[RAW_ATTRIBUTE]) {
        while (iswspace(lexer->lookahead)) skip(lexer);
    }
    if (valid[START_TAG_NAME] || valid[END_TAG_NAME]) return scan_name(s, lexer, valid[END_TAG_NAME]);
    if (valid[SELF_CLOSING_DELIMITER] && lexer->lookahead == '/') {
        if (!take(lexer, "/>" ) || !s->length) return false;
        pop(s); return emit(lexer, SELF_CLOSING_DELIMITER);
    }
    if (valid[RAW_ATTRIBUTE] && lexer->lookahead == 'i') {
        if (!take(lexer, "is:raw") || !s->length) return false;
        if (lexer->lookahead != '=' && lexer->lookahead != '/' && lexer->lookahead != '>' &&
            !iswspace(lexer->lookahead) && !lexer->eof(lexer)) return false;
        s->tags[top_at(s) + 1] = RAW;
        return emit(lexer, RAW_ATTRIBUTE);
    }
    if (!s->frontmatter && (valid[JSX_TEXT] || valid[FRAGMENT_CLOSE])) return scan_text(s, lexer, valid);
    // Give the inherited TypeScript ASI scanner first refusal before
    // recognizing a fragment in expression position.
    if (!s->frontmatter && valid[FRAGMENT_OPEN]) {
        bool comment = false;
        bool asi = valid[AUTOMATIC_SEMICOLON] || valid[FUNCTION_SIGNATURE_AUTOMATIC_SEMICOLON];
        if (asi) {
            if (scan_automatic_semicolon(lexer, valid, &comment)) return true;
            if (comment) return false;
        } else {
            while (iswspace(lexer->lookahead)) skip(lexer);
        }
        if (lexer->lookahead == '<') {
            advance(lexer);
            if (lexer->lookahead == '>') {
                if (!push(s, "")) return false;
                advance(lexer);
                return emit(lexer, FRAGMENT_OPEN);
            }
            if (lexer->lookahead != '!' || !valid[HTML_COMMENT]) return false;
            advance(lexer);
            return scan_html_comment(lexer);
        }
        if (asi) return valid[TERNARY_QMARK] && lexer->lookahead == '?' && scan_ternary_qmark(lexer);
    }
    if (!s->frontmatter && valid[HTML_COMMENT] && !valid[AUTOMATIC_SEMICOLON] &&
        !valid[FUNCTION_SIGNATURE_AUTOMATIC_SEMICOLON] && !valid[TEMPLATE_CHARS] &&
        !valid[ESCAPE_SEQUENCE] && !valid[REGEX_PATTERN]) {
        while (iswspace(lexer->lookahead)) skip(lexer);
        if (lexer->lookahead == '<')
            return take(lexer, "<!") && scan_html_comment(lexer);
    }
    return scan_typescript(lexer, valid);
}
