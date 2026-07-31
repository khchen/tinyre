/*
 *====================================================================
 *
 *             TinyRe - A Tiny Regex Engine for Nim
 *                 Copyright (c) Chen Kai-Hung
 *
 *====================================================================
 *
 * TinyRe regular-expression compiler and Pike VM.
 *
 * Copyright (c) Chen Kai-Hung. All rights reserved.
 *
 * Portions of the implementation are derived from code by:
 * Copyright 2007-2009 Russ Cox. All rights reserved.
 * Copyright 2020-2021 Kyryl Melekhin. All rights reserved.
 *
 * TinyRe-specific additions include fixed-width hexadecimal escapes
 * (\xHH, \uHHHH, and \UHHHHHHHH), counted and lazy repetition, character
 * classes, case-insensitive matching, word-boundary assertions, and the C
 * wrapper used by tinyre.nim. The engine supports a byte-oriented mode and
 * an optional UTF-8 code-point mode; UTF-8 mode does not provide complete
 * Unicode property handling or complete Unicode case folding.
 *
 * In particular, \b is a word-boundary assertion. Use \x08 for a literal
 * backspace. Counted-repeat numbers are limited to 65535 and malformed
 * escapes or patterns are rejected by the compiler.
 *
 * The TinyRe license is in license.txt. The upstream attributions above are
 * retained for the derived portions; consult the original source for the
 * complete BSD-style license terms that apply to those portions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#if defined(_MSC_VER)
#include <malloc.h>
#define TINYRE_STACK_ALLOC _alloca
#elif defined(__GNUC__) || defined(__clang__)
#define TINYRE_STACK_ALLOC __builtin_alloca
#else
#include <alloca.h>
#define TINYRE_STACK_ALLOC alloca
#endif

/* Nominal UTF-8 width lookup keyed by the leading byte; non-leading bytes */
/* have width one. */
static const unsigned char utf8_length[256] = {
  /*  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
  /* 0 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /* 1 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /* 2 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /* 3 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /* 4 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /* 5 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /* 6 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /* 7 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /* 8 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /* 9 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /* A */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /* B */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /* C */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  /* D */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  /* E */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  /* F */ 4, 4, 4, 4, 4, 4, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1
};

static int uc_len(const char * s, int utf8) {
  /*
   * Return the width of the next input unit. In UTF-8 mode, a valid
   * expected continuation sequence is returned at its full width; an invalid
   * or truncated sequence is treated as one byte. Pattern parsing supplies a
   * NUL-terminated string; bounded input uses uc_len_bounded() instead.
   */
  if (!utf8) return 1;

  int width = utf8_length[(unsigned char)s[0]];
  if (width == 1) return 1;
  for (int i = 1; i < width; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == 0 || (c & 0xc0) != 0x80) return 1;
  }
  return width;
}

static int uc_len_bounded(const char *s, int available, int utf8) {
  /*
   * Like uc_len(), but never reads beyond `available` bytes. A truncated or
   * invalid UTF-8 sequence is deliberately treated as one byte so bounded
   * input such as a non-NUL-terminated buffer remains safe.
   */
  if (!utf8 || available <= 0) return 1;

  int width = utf8_length[(unsigned char)s[0]];
  if (width == 1 || width > available) return 1;
  for (int i = 1; i < width; i++) {
    unsigned char c = (unsigned char)s[i];
    if ((c & 0xc0) != 0x80) return 1;
  }
  return width;
}

static int uc_code(const char * s, int utf8) {
  /* Decode the next complete input unit to a code point or byte value. */
  int dst = (unsigned char)s[0];
  if (utf8) {
    int width = uc_len(s, utf8);
    if (width == 1) return dst;
    if (dst < 224) dst = ((dst & 0x1f) << 6) |
                           ((unsigned char)s[1] & 0x3f);
    else if (dst < 240) dst = ((dst & 0x0f) << 12) |
                               (((unsigned char)s[1] & 0x3f) << 6) |
                               ((unsigned char)s[2] & 0x3f);
    else dst = ((dst & 0x07) << 18) |
               (((unsigned char)s[1] & 0x3f) << 12) |
               (((unsigned char)s[2] & 0x3f) << 6) |
               ((unsigned char)s[3] & 0x3f);
  }
  return dst;
}

static int uc_code_bounded(const char *s, int available, int utf8) {
  /* Decode a bounded input unit without reading beyond `available` bytes. */
  if (!utf8) return (unsigned char)s[0];
  if (available <= 0) return 0;
  if (uc_len_bounded(s, available, utf8) == 1)
    return (unsigned char)s[0];
  return uc_code(s, utf8);
}

static int isword(const char *s)
{
  /*
   * Word-boundary matching is byte-oriented: alphanumeric bytes and '_' are
   * words, and every high-bit byte is treated as a word byte. This intentionally
   * does not provide complete Unicode word-property support.
   */
  int c = (unsigned char) s[0];
  return isalnum(c) || c == '_' || c > 127;
}

static int unicode_tolower(int c)
{
  /* Apply the deliberately small case-folding table supported by TinyRe. */
  if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
  if ((c >= 0x00C0 && c <= 0x00D6) ||
      (c >= 0x00D8 && c <= 0x00DE) ||
      (c >= 0x0391 && c <= 0x03A1) ||
      (c >= 0x03A3 && c <= 0x03AB) ||
      (c >= 0x0410 && c <= 0x042F))
    return c + 32;
  return c;
}

static int isasciiword(int c)
{
  /* Match the definition used by the \w and \W character classes. */
  return (c >= 0 && c <= UCHAR_MAX &&
          isalnum((unsigned char)c)) || c == '_';
}

typedef struct rcode rcode;
struct rcode
{
  int unilen;   /* Number of integers used by insts. */
  int len;      /* Number of VM instructions, including terminal storage. */
  int sub;      /* Capture count during compilation; capture-storage bytes after. */
  int presub;   /* Capture count during compilation; one rsub record size after. */
  int splits;   /* Number of positive split instructions in the VM program. */
  int sparsesz; /* Number of entries reserved by the split visited set. */
  int insts[];  /* Variable-length encoded VM instructions. */
};

enum
{
  /* Input-consuming instructions and the terminal MATCH thread marker. */
  CHAR = 1,
  CLASS,
  MATCH,
  ANY,
  /* Position assertions; these do not consume input. */
  WBEG,
  WEND,
  WB,
  NOTB,
  BOL,
  EOL,
  /* Capture and control-flow instructions. */
  SAVE,
  /* Instructions whose following integer is a relative offset. */
  JMP,
  /* Positive split opcodes must remain odd; compiled IDs increase by two. */
  SPLIT,
  RSPLIT, /* Reverse branch priority; emitted as the negated split ID. */
};

typedef struct rsub rsub;
struct rsub
{
  int ref;             /* Number of threads sharing this capture record. */
  rsub *next_free;    /* Intrusive free-list link when ref reaches zero. */
  const char *sub[];  /* Start pointers followed by end pointers. */
};

typedef struct rthread rthread;
struct rthread
{
  int *pc;    /* Program counter for this thread. */
  rsub *sub;  /* Capture state owned by this thread. */
};

typedef struct counted_repeat counted_repeat;
struct counted_repeat {
  int minimum;         /* Required number of copies. */
  int maximum;         /* Maximum copies or COUNTED_REPEAT_UNBOUNDED. */
  int optional_opcode; /* SPLIT for greedy, RSPLIT for lazy repetition. */
  int loop_opcode;     /* RSPLIT for greedy, SPLIT for lazy repetition. */
};

typedef struct capture_frame capture_frame;
struct capture_frame {
  int capture_slot;             /* First SAVE slot, or zero for non-capture. */
  int outer_term_pc;            /* Atom boundary surrounding the group. */
  int outer_patch_pc;           /* Pending outer alternative label. */
  int outer_start_pc;           /* Start of the outer alternative. */
  int outer_alternative_count;  /* Number of outer labels already saved. */
};

typedef struct alternation_state alternation_state;
struct alternation_state {
  int start_pc;           /* Start of the current alternative block. */
  int term_pc;            /* End of the most recent atom. */
  int patch_pc;           /* Label whose branch target is still pending. */
  int alternative_count; /* Number of saved alternative labels. */
  int *patch_stack;       /* Fixed compiler stack for saved labels. */
};

typedef struct compile_state compile_state;
#define TINYRE_COMPILE_STACK_CAPACITY 4096 /* Bound parser stack depth. */
struct compile_state {
  int alternation_patch_stack[TINYRE_COMPILE_STACK_CAPACITY]; /* Label stack. */
  alternation_state alternation;                              /* Current block. */
  capture_frame capture_frames[TINYRE_COMPILE_STACK_CAPACITY]; /* Group stack. */
  int capture_depth;                                           /* Open groups. */
};

typedef struct compile_context compile_context;
struct compile_context {
  rcode *prog;        /* Program being measured or emitted. */
  int *code;          /* NULL during the size-only pass. */
  int cursor;         /* Current encoded-instruction offset. */
  int utf8;            /* Pattern decoding mode. */
  compile_state state; /* Alternation and capture compiler state. */
};

static inline void write_code(compile_context *ctx, int value)
{
  /* Measure-only compilation advances the cursor without writing memory. */
  if (ctx->code)
    ctx->code[ctx->cursor] = value;
  ctx->cursor++;
}

static inline void patch_code(compile_context *ctx, int at, int value)
{
  /* Patching is a no-op during the measure-only pass. */
  if (ctx->code)
    ctx->code[at] = value;
}

static inline int relative_offset(int at, int target) {
  /* Branch operands are measured from the instruction after the operand. */
  return target - at - 2;
}

static int re_classmatch(const int *pc, int c, int insensitive)
{
  /*
   * `pc` points to the class polarity flag after CLASS. Each following
   * entry is either a [first, last] range or [-1, escape-kind].
   */
  int is_positive = *pc++;
  int cnt = *pc++;
  while (cnt--) {
    if (*pc == -1) {
      switch(pc[1]) {
      case 'd': if (c <= UCHAR_MAX && isdigit((unsigned char)c)) return is_positive; break;
      case 'D': if (c > UCHAR_MAX || !isdigit((unsigned char)c)) return is_positive; break;
      case 's': if (c <= UCHAR_MAX && isspace((unsigned char)c)) return is_positive; break;
      case 'S': if (c > UCHAR_MAX || !isspace((unsigned char)c)) return is_positive; break;
      case 'w': if (isasciiword(c)) return is_positive; break;
      case 'W': if (!isasciiword(c)) return is_positive; break;
      }
    } else if (!insensitive) {
      if (c >= *pc && c <= pc[1]) return is_positive;
    } else {
      c = unicode_tolower(c);
      if (c >= unicode_tolower(*pc) && c <= unicode_tolower(pc[1])) return is_positive;
    }
    pc += 2;
  }
  return !is_positive;
}

static int hex_value(int x) {
  return x >= '0' && x <= '9' ? x - '0' : x - 'a' + 10;
}

static int parse_hex_codepoint(const char *re, int hex_digits) {
  /* Parse a fixed-width escape; return -1 for malformed or unrepresentable values. */
  int i;
  unsigned int result = 0;
  for (i = 1; i <= hex_digits; i++) {
    unsigned char c = (unsigned char)re[i];
    if (c == 0 || !isxdigit(c)) return -1;
    result <<= 4;
    result |= (unsigned int)hex_value(tolower(c));
  }
  return result > INT_MAX ? -1 : (int)result;
}

static int parse_repeat_number(const char **re_ptr, int *out) {
  /* Parse one decimal repeat bound; values above 65535 are rejected. */
  const char *re = *re_ptr;
  int result = 0;

  if (!isdigit((unsigned char)*re)) return -1;
  while (isdigit((unsigned char)*re)) {
    result = result * 10 + *re++ - '0';
    if (result > 65535) return -1;
  }

  *out = result;
  *re_ptr = re;
  return 0;
}

#define IS_ESCAPE_CLASS_KIND(kind) \
  ((kind) == 'd' || (kind) == 'D' || (kind) == 'w' || \
   (kind) == 'W' || (kind) == 's' || (kind) == 'S')

/*
 * Parse one class member. Escape classes use a negative sentinel so callers
 * can emit them separately and reject them as range endpoints.
 */
static int parse_class_token(const char **re_ptr, int utf8) {
  const char *re = *re_ptr;
  int ch, hex_digits;
  switch (*re) {
    case 0:
      return -1;

    case '\\':
      re++;
      *re_ptr = re + 1;
      if (!*re) return -1;
      /* Escape classes use a negative sentinel and cannot end a range. */
      if (IS_ESCAPE_CLASS_KIND(*re))
        return -(*re);
      switch (*re) {
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case 'f': return '\f';
        case 'v': return '\v';
        /*
         * '\\' is handled by the fallback path so an escaped backslash stays
         * a literal class member.
         */

        case 'x': hex_digits = 2; goto _hex;
        case 'u': hex_digits = 4; goto _hex;
        case 'U': hex_digits = 8; _hex:
          ch = parse_hex_codepoint(re, hex_digits);
          if (ch < 0) return -1;
          *re_ptr = re + hex_digits + 1;
          return ch;
      }
      /* Fall through: an unlisted escape is treated as a literal character. */

    default:
      *re_ptr = re + uc_len(re, utf8);
      return uc_code(re, utf8);
  }
}

/* Emit a CHAR instruction for one byte or decoded code point. */
static void emit_char_atom(compile_context *ctx, int ch) {
  write_code(ctx, CHAR);
  write_code(ctx, ch);
}

/* Emit a CLASS instruction containing one built-in escape class. */
static void emit_escape_class_atom(compile_context *ctx, int kind) {
  write_code(ctx, CLASS);
  write_code(ctx, 1);
  write_code(ctx, 1);
  write_code(ctx, -1);
  write_code(ctx, kind);
}

/* Translate the supported control-character escape names. */
static int escaped_control_char(int kind) {
  switch (kind) {
  case 'n': return '\n';
  case 'r': return '\r';
  case 't': return '\t';
  case 'f': return '\f';
  case 'v': return '\v';
  }
  return -1;
}

static int emit_escape_atom(const char **re_ptr, compile_context *ctx) {
  /* Emit one escaped atom and leave `re_ptr` at its final escape character. */
  const char *re = *re_ptr + 1;
  int ch, hex_digits;

  if (!*re) return -1; /* Trailing backslash */

  if (IS_ESCAPE_CLASS_KIND(*re)) {
    emit_escape_class_atom(ctx, *re);
    *re_ptr = re;
    return 0;
  }

  switch (*re) {
  case '<': case '>': case 'B': case 'b':
    switch (*re) {
    case '<': write_code(ctx, WBEG); break;
    case '>': write_code(ctx, WEND); break;
    case 'B': write_code(ctx, NOTB); break;
    case 'b': write_code(ctx, WB); break;
    }
    break;
  case 'n': case 'r': case 't': case 'f': case 'v':
    emit_char_atom(ctx, escaped_control_char(*re));
    break;
  case 'x': hex_digits = 2; goto _hex;
  case 'u': hex_digits = 4; goto _hex;
  case 'U': hex_digits = 8; _hex:
    ch = parse_hex_codepoint(re, hex_digits);
    if (ch < 0) return -1;
    re += hex_digits;
    emit_char_atom(ctx, ch);
    break;
  default:
    emit_char_atom(ctx, uc_code(re, ctx->utf8));
    break;
  }

  *re_ptr = re;
  return 0;
}

static int emit_class_range(compile_context *ctx, const char **re_ptr, int first) {
  /* Emit one literal or [first, last] range in the CLASS payload. */
  const char *re = *re_ptr;
  int last = first;

  if (*re == '-' && re[1] != ']') {
    re++;
    last = parse_class_token(&re, ctx->utf8);
    if (last < 0) return -1; /* Do not allow \d\D\s\S\w\W as range ends. */
  }

  write_code(ctx, first);
  write_code(ctx, last);
  *re_ptr = re;
  return 0;
}

static int emit_char_class_atom(const char **re_ptr, compile_context *ctx) {
  /* Emit CLASS, polarity, range count, then pairs or built-in class entries. */
  const char *re = *re_ptr + 1;
  int is_negated = (*re == '^');
  int range_count = 0;
  int range_count_pc;

  write_code(ctx, CLASS);
  write_code(ctx, !is_negated);
  if (is_negated) re++;
  range_count_pc = ctx->cursor++;

  while (*re != ']') {
    int class_token = parse_class_token(&re, ctx->utf8);
    if (class_token == -1) return -1;

    if (class_token < 0) {
      write_code(ctx, -1);
      write_code(ctx, -class_token);
    } else if (emit_class_range(ctx, &re, class_token) < 0) {
      return -1;
    }
    range_count++;
  }

  patch_code(ctx, range_count_pc, range_count);
  *re_ptr = re;
  return 0;
}

static inline void copy_atom_to_pc(compile_context *ctx, int term, int size) {
  /* Copy the encoded atom beginning at `term` into the current cursor. */
  if (ctx->code)
    memcpy(&ctx->code[ctx->cursor], &ctx->code[term], size * sizeof(int));
  ctx->cursor += size;
}

static inline void emit_branch_at_pc(compile_context *ctx, int opcode, int target) {
  /* Emit a two-integer branch whose target is an instruction address. */
  int at = ctx->cursor;
  write_code(ctx, opcode);
  write_code(ctx, relative_offset(at, target));
}

static inline void insert_branch_before_atom(compile_context *ctx, int term, int opcode) {
  /* Make room before an atom so ?, *, and {0,...} can wrap it. */
  if (ctx->code)
    memmove(ctx->code + term + 2, ctx->code + term,
            (ctx->cursor - term) * sizeof(int));
  ctx->cursor += 2;
  patch_code(ctx, term, opcode);
  patch_code(ctx, term + 1, relative_offset(term, ctx->cursor));
}

static inline void emit_optional_repeat_copies(compile_context *ctx, int term,
                                               int atom_size, int optional_copies,
                                               int branch_opcode) {
  /* Emit optional copies in priority order for greedy or lazy repetition. */
  for (int i = optional_copies; i > 0; i--) {
    int copy_span = atom_size + 2;
    int skip_target;
    int offset_pc;
    write_code(ctx, branch_opcode);
    offset_pc = ctx->cursor++;
    skip_target = offset_pc + copy_span * i;
    patch_code(ctx, offset_pc, relative_offset(offset_pc, skip_target));
    copy_atom_to_pc(ctx, term, atom_size);
  }
}

#define COUNTED_REPEAT_UNBOUNDED -1 /* No finite upper bound was specified. */

static inline int emit_counted_repeat(compile_context *ctx, int term,
                                      const counted_repeat *repeat) {
  /* Expand a counted repeat by copying the already-emitted atom. */
  int atom_size = ctx->cursor - term;
  if (atom_size <= 0) return -1;
  int required_copies = repeat->minimum - 1;

  for (int i = 0; i < required_copies; i++)
    copy_atom_to_pc(ctx, term, atom_size);

  if (repeat->maximum == COUNTED_REPEAT_UNBOUNDED) {
    emit_branch_at_pc(ctx, repeat->loop_opcode, ctx->cursor - atom_size);
  } else if (repeat->maximum > 0) {
    /* For compatibility, max < min keeps the required copies and adds none. */
    int zero_min_skips_original = repeat->minimum == 0 ? 1 : 0;
    int optional_copies = repeat->maximum - repeat->minimum - zero_min_skips_original;
    emit_optional_repeat_copies(ctx, term, atom_size, optional_copies,
                                repeat->optional_opcode);
  }

  if (repeat->minimum == 0) {
    insert_branch_before_atom(ctx, term,
                              repeat->maximum == 0 ? JMP : repeat->optional_opcode);
    return ctx->cursor;
  }

  return term;
}

static int parse_counted_repeat(const char **re_ptr, counted_repeat *repeat) {
  /* Parse {minimum}, {minimum,maximum}, and their lazy suffix. */
  const char *re = *re_ptr + 1;

  repeat->minimum = 0;
  repeat->maximum = 0;
  repeat->optional_opcode = SPLIT;
  repeat->loop_opcode = RSPLIT;

  if (parse_repeat_number(&re, &repeat->minimum) < 0) return -1;

  if (*re == '}') {
    repeat->maximum = repeat->minimum;
  } else if (*re == ',') {
    re++;
    if (*re == '}') {
      repeat->maximum = COUNTED_REPEAT_UNBOUNDED;
    } else if (parse_repeat_number(&re, &repeat->maximum) < 0 || *re != '}') {
      return -1;
    }
  } else {
    return -1;
  }

  if (re[1] == '?') {
    repeat->optional_opcode = RSPLIT;
    repeat->loop_opcode = SPLIT;
    re++;
  }

  *re_ptr = re;
  return 0;
}

static inline void init_compile_state(compile_state *state, int start_pc) {
  /* Start a compiler state whose first atom begins at `start_pc`. */
  state->alternation.start_pc = start_pc;
  state->alternation.term_pc = start_pc;
  state->alternation.patch_pc = 0;
  state->alternation.alternative_count = 0;
  state->alternation.patch_stack = state->alternation_patch_stack;
  state->capture_depth = 0;
}

static void patch_alternatives(compile_context *ctx, alternation_state *alternation,
                               int base_count) {
  /* Resolve saved branch labels after all alternatives have been emitted. */
  int saved_count = alternation->alternative_count;

  if (alternation->patch_pc)
    patch_code(ctx, alternation->patch_pc,
               relative_offset(alternation->patch_pc, ctx->cursor) + 1);
  for (; alternation->alternative_count > base_count;
       alternation->alternative_count--) {
    if (alternation->patch_pc) {
      /* Each earlier alternative insertion shifts later saved labels by 2 ints. */
      int stack_index = base_count + saved_count - alternation->alternative_count;
      int shifted_label = alternation->patch_stack[stack_index] +
                          (alternation->alternative_count - base_count) * 2;
      patch_code(ctx, shifted_label,
                 relative_offset(shifted_label, ctx->cursor) + 1);
    }
  }
}

static inline void restore_outer_alternation(alternation_state *alternation,
                                             const capture_frame *frame) {
  /* Restore the alternation state that surrounded a capture group. */
  alternation->start_pc = frame->outer_start_pc;
  alternation->patch_pc = frame->outer_patch_pc;
  alternation->term_pc = frame->outer_term_pc;
}

static int emit_alternation(compile_context *ctx, alternation_state *alternation) {
  /* Insert a SPLIT/JMP pair and defer both branch targets until closure. */
  if (alternation->patch_pc) {
    if (alternation->alternative_count >= TINYRE_COMPILE_STACK_CAPACITY)
      return -1;
    alternation->patch_stack[alternation->alternative_count++] =
      alternation->patch_pc;
  }
  if (ctx->code)
    memmove(ctx->code + alternation->start_pc + 2,
            ctx->code + alternation->start_pc,
            (ctx->cursor - alternation->start_pc) * sizeof(int));
  ctx->cursor += 2;
  write_code(ctx, JMP);
  alternation->patch_pc = ctx->cursor;
  ctx->cursor++;
  patch_code(ctx, alternation->start_pc, SPLIT);
  patch_code(ctx, alternation->start_pc + 1,
             relative_offset(alternation->start_pc, ctx->cursor));
  alternation->term_pc = ctx->cursor;
  return 0;
}

static int open_capture_group(const char **re_ptr, compile_context *ctx) {
  /* Enter a capturing or non-capturing group and save outer compiler state. */
  const char *re = *re_ptr;
  compile_state *state = &ctx->state;
  alternation_state *alternation = &state->alternation;
  int group_term_pc = ctx->cursor;
  int capture_slot = 0;

  if (state->capture_depth >= TINYRE_COMPILE_STACK_CAPACITY)
    return -1;
  if (re[1] == '?') {
    re += 2;
    if (*re != ':')
      return -1;
  } else {
    capture_slot = ++ctx->prog->sub;
    write_code(ctx, SAVE);
    write_code(ctx, capture_slot);
  }
  alternation->term_pc = group_term_pc;
  state->capture_frames[state->capture_depth++] =
    (capture_frame){capture_slot, group_term_pc, alternation->patch_pc,
                    alternation->start_pc, alternation->alternative_count};
  alternation->patch_pc = 0;
  alternation->start_pc = ctx->cursor;
  *re_ptr = re;
  return 0;
}

static int close_capture_group(compile_context *ctx) {
  /* Close the group, patch its alternatives, and save its end when capturing. */
  compile_state *state = &ctx->state;
  alternation_state *alternation = &state->alternation;
  capture_frame frame;

  if (state->capture_depth <= 0) return -1;
  frame = state->capture_frames[--state->capture_depth];

  /* The group's alternatives must be closed before outer state is restored. */
  patch_alternatives(ctx, alternation, frame.outer_alternative_count);
  restore_outer_alternation(alternation, &frame);
  if (frame.capture_slot) {
    write_code(ctx, SAVE);
    write_code(ctx, frame.capture_slot + ctx->prog->presub + 1);
  }
  return 0;
}

static inline int consume_lazy_repeat_suffix(const char **re_ptr) {
  /* Consume the optional '?' that changes greedy branch priority to lazy. */
  if ((*re_ptr)[1] == '?') {
    (*re_ptr)++;
    return 1;
  }
  return 0;
}

static int emit_question_repeat(compile_context *ctx, const char **re_ptr, int *term_pc) {
  /* Wrap the preceding atom in a zero-or-one branch. */
  const char *re = *re_ptr;
  int branch_opcode;

  if (ctx->cursor == *term_pc) return -1;
  branch_opcode = consume_lazy_repeat_suffix(&re) ? RSPLIT : SPLIT;
  insert_branch_before_atom(ctx, *term_pc, branch_opcode);
  *term_pc = ctx->cursor;
  *re_ptr = re;
  return 0;
}

static int emit_star_repeat(compile_context *ctx, const char **re_ptr, int *term_pc) {
  /* Wrap the preceding atom in a looping zero-or-more branch. */
  const char *re = *re_ptr;
  int branch_opcode;

  if (ctx->cursor == *term_pc) return -1;
  branch_opcode = consume_lazy_repeat_suffix(&re) ? RSPLIT : SPLIT;
  if (ctx->code)
    memmove(ctx->code + *term_pc + 2, ctx->code + *term_pc,
            (ctx->cursor - *term_pc) * sizeof(int));
  ctx->cursor += 2;
  emit_branch_at_pc(ctx, JMP, *term_pc);
  patch_code(ctx, *term_pc, branch_opcode);
  patch_code(ctx, *term_pc + 1, relative_offset(*term_pc, ctx->cursor));
  *term_pc = ctx->cursor;
  *re_ptr = re;
  return 0;
}

static int emit_plus_repeat(compile_context *ctx, const char **re_ptr, int *term_pc) {
  /* Add a loop after the preceding atom for one-or-more repetition. */
  const char *re = *re_ptr;
  int loop_opcode;

  if (ctx->cursor == *term_pc) return -1;
  loop_opcode = consume_lazy_repeat_suffix(&re) ? SPLIT : RSPLIT;
  emit_branch_at_pc(ctx, loop_opcode, *term_pc);
  *term_pc = ctx->cursor;
  *re_ptr = re;
  return 0;
}

static inline int compile_pattern_token(const char **cursor_ptr,
                                        compile_context *ctx) {
  /* Compile one token and leave the cursor on its final source character. */
  const char *cursor = *cursor_ptr;
  compile_state *state = &ctx->state;

  switch (*cursor) {
  case '\\':
    state->alternation.term_pc = ctx->cursor;
    if (emit_escape_atom(&cursor, ctx) < 0) return -1;
    break;
  default:
    state->alternation.term_pc = ctx->cursor;
    emit_char_atom(ctx, uc_code(cursor, ctx->utf8));
    break;
  case '.':
    state->alternation.term_pc = ctx->cursor;
    write_code(ctx, ANY);
    break;
  case '[':
    state->alternation.term_pc = ctx->cursor;
    if (emit_char_class_atom(&cursor, ctx) < 0) return -1;
    break;
  case '(':
    if (open_capture_group(&cursor, ctx) < 0) return -1;
    break;
  case ')':
    if (close_capture_group(ctx) < 0) return -1;
    break;
  case '{': {
    counted_repeat repeat;
    int term_pc;
    if (parse_counted_repeat(&cursor, &repeat) < 0) return -1;
    term_pc = emit_counted_repeat(ctx, state->alternation.term_pc, &repeat);
    if (term_pc < 0) return -1;
    state->alternation.term_pc = term_pc;
    break;
  }
  case '?':
    if (emit_question_repeat(ctx, &cursor, &state->alternation.term_pc) < 0)
      return -1;
    break;
  case '*':
    if (emit_star_repeat(ctx, &cursor, &state->alternation.term_pc) < 0)
      return -1;
    break;
  case '+':
    if (emit_plus_repeat(ctx, &cursor, &state->alternation.term_pc) < 0)
      return -1;
    break;
  case '|':
    if (emit_alternation(ctx, &state->alternation) < 0) return -1;
    break;
  case '^': case '$':
    write_code(ctx, *cursor == '^' ? BOL : EOL);
    state->alternation.term_pc = ctx->cursor;
    break;
  }

  *cursor_ptr = cursor;
  return 0;
}

static int compile_regex_program(const char *pattern, rcode *prog, int measure_only, int utf8)
{
  /* Compile once for size measurement or again into the allocated program. */
  const char *cursor = pattern;
  compile_context ctx;
  int char_len;

  ctx.prog = prog;
  ctx.code = measure_only ? NULL : prog->insts;
  ctx.cursor = prog->unilen;
  ctx.utf8 = utf8;
  init_compile_state(&ctx.state, ctx.cursor);

  while (*cursor) {
    if (compile_pattern_token(&cursor, &ctx) < 0) return -1;
    char_len = uc_len(cursor, utf8);
    cursor += char_len;
  }
  if (ctx.state.capture_depth) return -1;
  patch_alternatives(&ctx, &ctx.state.alternation, 0);
  prog->unilen = ctx.cursor;
  return 0;
}

int re_sizecode(const char *re, int *nsub, int utf8)
{
  /* Return encoded program size and capture count without allocating code. */
  rcode dummyprog;
  dummyprog.unilen = 3; /* Reserve the final SAVE, slot, and MATCH integers. */
  dummyprog.sub = 0;

  int res = compile_regex_program(re, &dummyprog, 1, utf8);
  if (res < 0) return res;
  *nsub = dummyprog.sub;
  return dummyprog.unilen;
}

int re_comp(rcode *prog, const char *re, int nsubs, int utf8)
{
  /* Emit the VM program, append SAVE/MATCH, and prepare runtime metadata. */
  prog->len = 0;
  prog->unilen = 0;
  prog->sub = 0;
  prog->presub = nsubs;
  prog->splits = 0;

  int res = compile_regex_program(re, prog, 0, utf8);
  if (res < 0) return res;
  int instruction_count = 0;
  int next_split_opcode = SPLIT;
  for (int pc = 0; pc < prog->unilen;) {
    int opcode = prog->insts[pc];
    int width = 1;

    switch (opcode) {
    case CLASS:
      width = prog->insts[pc + 2] * 2 + 3;
      instruction_count++;
      break;
    case SPLIT:
      prog->insts[pc] = next_split_opcode;
      next_split_opcode += 2;
      width = 2;
      instruction_count++;
      break;
    case RSPLIT:
      prog->insts[pc] = -next_split_opcode;
      next_split_opcode += 2;
      width = 2;
      instruction_count++;
      break;
    case JMP:
    case SAVE:
    case CHAR:
      width = 2;
      instruction_count++;
      break;
    case ANY:
      instruction_count++;
      break;
    }
    pc += width;
  }
  prog->insts[prog->unilen++] = SAVE;
  prog->insts[prog->unilen++] = prog->sub + 1;
  prog->insts[prog->unilen++] = MATCH;
  prog->splits = (next_split_opcode - SPLIT) / 2;
  prog->len = instruction_count + 2;
  prog->presub = sizeof(rsub)+(sizeof(char*) * (nsubs + 1) * 2);
  prog->sub = prog->presub * (prog->len - prog->splits + 3);
  prog->sparsesz = next_split_opcode;
  return 0;
}

typedef enum vm_save_mode vm_save_mode;
enum vm_save_mode {
  VM_SAVE_NEXT,
  VM_SAVE_CURRENT
};

typedef struct vm_context vm_context;
struct vm_context {
  rcode *prog;                    /* Compiled program. */
  const char *input_start;        /* Start of the current search window. */
  const char *input_end;          /* One past the final byte in that window. */
  const char *bol_start;          /* Absolute buffer position accepted by ^. */
  const char *input;              /* Current code-point start. */
  const char *next_input;         /* Current code-point end. */
  const char *continuation;       /* Byte immediately before input_start. */
  int input_length;               /* Search-window length in bytes. */
  int insensitive;                /* Whether case-insensitive matching is on. */
  int utf8;                       /* Whether input units are decoded as UTF-8. */
  int last;                       /* Whether next_input is at the input end. */
  int match_pc;                   /* Synthetic terminal PC for retained matches. */
  int capture_count;              /* Number of capture pointers, including group 0. */
  int capture_bytes;              /* Bytes copied when a full capture state is cloned. */
  int capture_record_size;        /* Bytes occupied by one rsub record. */
  int capture_storage_offset;     /* Next slot in the reusable capture ring. */
  int **deferred_pcs;              /* Deferred epsilon-branch program counters. */
  rsub **deferred_captures;        /* Capture states paired with deferred PCs. */
  int deferred_count;              /* Number of deferred epsilon branches. */
  unsigned int *sparse_dense;      /* Sparse/dense set for visited split IDs. */
  unsigned int sparse_count;       /* Number of split IDs visited this step. */
  rthread *current_threads;        /* Threads ready for the current input unit. */
  rthread *next_threads;           /* Threads ready for the next input unit. */
  int current_count;               /* Number of current threads. */
  int next_count;                  /* Number of next threads. */
  char *capture_storage;           /* Stack storage for reference-counted captures. */
  rsub *free_captures;             /* Reusable capture records. */
  rsub *matched;                   /* Highest-priority match retained so far. */
  const char **capture_output;     /* Output pointers into the caller's input. */
};

static inline int vm_is_word_at(const vm_context *vm, const char *s)
{
  /* Do not inspect the byte at or beyond input_end as a word. */
  return s < vm->input_end && isword(s);
}

static inline int vm_is_consuming(int opcode)
{
  /* MATCH is included because it is stored as a terminal thread marker. */
  return opcode == CHAR || opcode == CLASS ||
         opcode == MATCH || opcode == ANY;
}

static inline int vm_is_positive_split(int opcode)
{
  /* Positive split IDs are odd and occupy the nonnegative opcode space. */
  return opcode >= SPLIT;
}

static inline int vm_is_negative_split(int opcode)
{
  /* Lazy split IDs are the negated positive IDs. */
  return opcode <= -SPLIT;
}

static inline void vm_release_capture(vm_context *vm, rsub *capture)
{
  /* Return a capture record to the ring once its last thread releases it. */
  if (--capture->ref == 0) {
    capture->next_free = vm->free_captures;
    vm->free_captures = capture;
  }
}

static inline rsub *vm_take_capture(vm_context *vm)
{
  /* Obtain a record from the free list or the circular stack storage. */
  rsub *capture;
  if (vm->free_captures) {
    capture = vm->free_captures;
    vm->free_captures = capture->next_free;
  } else {
    if (vm->capture_storage_offset == vm->prog->sub)
      vm->capture_storage_offset = 0;
    capture = (rsub*)&vm->capture_storage[vm->capture_storage_offset];
    vm->capture_storage_offset += vm->capture_record_size;
  }
  capture->ref = 1;
  return capture;
}

static inline rsub *vm_clone_capture(vm_context *vm, rsub *capture, int bytes)
{
  /* Copy a shared capture state before a SAVE instruction mutates it. */
  rsub *copy;
  vm_release_capture(vm, capture);
  copy = vm_take_capture(vm);
  memcpy(copy->sub, capture->sub, bytes);
  return copy;
}

static inline int vm_restore_deferred(vm_context *vm, int **pc, rsub **capture)
{
  /* Resume the most recently deferred branch during epsilon expansion. */
  if (!vm->deferred_count)
    return 0;
  vm->deferred_count--;
  *pc = vm->deferred_pcs[vm->deferred_count];
  *capture = vm->deferred_captures[vm->deferred_count];
  return 1;
}

static inline int vm_fail_thread(vm_context *vm, int **pc, rsub **capture)
{
  /* Release the failed thread and continue with its deferred branch. */
  vm_release_capture(vm, *capture);
  return vm_restore_deferred(vm, pc, capture);
}

static inline void vm_append_thread(rthread *threads, int *count,
                                    int *pc, rsub *capture)
{
  /* Append a live thread while preserving Pike VM priority order. */
  threads[*count].pc = pc;
  threads[*count].sub = capture;
  (*count)++;
}

static inline int vm_mark_split(vm_context *vm, int opcode)
{
  /* Visit each split ID once per input step to prevent epsilon cycles. */
  if (vm->sparse_dense[opcode] < vm->sparse_count &&
      vm->sparse_dense[vm->sparse_dense[opcode] * 2] ==
        (unsigned int)opcode)
    return 0;
  vm->sparse_dense[opcode] = vm->sparse_count;
  vm->sparse_dense[vm->sparse_count++ * 2] = opcode;
  return 1;
}

static inline void vm_expand_split(vm_context *vm, rthread *target_threads,
                                   int *target_count, int **pc_ptr,
                                   rsub *capture, int reverse)
{
  /* Follow one split branch now and defer the other according to priority. */
  int *pc = *pc_ptr + 2;
  int deferred_index = vm->deferred_count;

  if (reverse) {
    vm->deferred_pcs[deferred_index] = pc;
    pc += pc[-1];
  } else {
    vm->deferred_pcs[deferred_index] = pc + pc[-1];
  }
  capture->ref++;
  if (vm_is_consuming(*pc)) {
    vm_append_thread(target_threads, target_count, pc, capture);
    pc = vm->deferred_pcs[deferred_index];
  } else {
    vm->deferred_captures[vm->deferred_count++] = capture;
  }
  *pc_ptr = pc;
}

static inline void vm_discard_threads(vm_context *vm, rthread *threads,
                                      int first, int count)
{
  /* Release captures owned by lower-priority threads after a match wins. */
  for (; first < count; first++) {
    if (threads[first].sub)
      vm_release_capture(vm, threads[first].sub);
  }
}

static inline void vm_save_next(vm_context *vm, int *pc, rsub **capture_ptr)
{
  /* Save a capture boundary for a thread that will consume the next unit. */
  rsub *capture = *capture_ptr;
  if (capture->ref > 1) {
    capture = vm_clone_capture(vm, capture, vm->capture_bytes);
    *capture_ptr = capture;
  }
  capture->sub[pc[1]] = vm->next_input;
}

static inline void vm_save_current(vm_context *vm, int *pc, rsub **capture_ptr)
{
  /* Save a capture boundary while expanding the current input position. */
  rsub *capture = *capture_ptr;
  if (pc[1] > vm->capture_count / 2 && capture->ref > 1) {
    int bytes = vm->free_captures ? vm->capture_bytes / 2 : vm->capture_bytes;
    capture = vm_clone_capture(vm, capture, bytes);
    *capture_ptr = capture;
  }
  capture->sub[pc[1]] = vm->next_input;
}

static void vm_expand(vm_context *vm, rthread *target_threads,
                      int *target_count, int **pc_ptr,
                      rsub **capture_ptr, vm_save_mode save_mode)
{
  /*
   * Expand the epsilon closure from one program counter. Thread order is
   * significant: it implements greedy/lazy and alternation priority while
   * the deferred stack prevents recursive C calls and epsilon loops.
   */
  int *pc = *pc_ptr;
  rsub *capture = *capture_ptr;

  for (;;) {
    int opcode = *pc;
    if (vm_is_consuming(opcode)) {
      vm_append_thread(target_threads, target_count, pc, capture);
      if (vm_restore_deferred(vm, &pc, &capture))
        continue;
      return;
    }

    if (vm_is_positive_split(opcode)) {
      if (!vm_mark_split(vm, opcode)) {
        if (!vm_fail_thread(vm, &pc, &capture))
          return;
        continue;
      }
      vm_expand_split(vm, target_threads, target_count,
                      &pc, capture, 0);
      continue;
    }

    if (opcode == SAVE) {
      if (save_mode == VM_SAVE_CURRENT)
        vm_save_current(vm, pc, &capture);
      else
        vm_save_next(vm, pc, &capture);
      pc += 2;
      continue;
    }

    if (opcode == NOTB) {
      int fails = ((vm->input == vm->input_start &&
                    vm->next_input == vm->input_start) &&
                   (vm->continuation ?
                     isword(vm->continuation) !=
                       vm_is_word_at(vm, vm->input) :
                     vm_is_word_at(vm, vm->input))) ||
                  vm_is_word_at(vm, vm->next_input) !=
                    vm_is_word_at(vm, vm->input);
      if (fails) {
        if (!vm_fail_thread(vm, &pc, &capture))
          return;
        continue;
      }
      pc++;
      continue;
    }

    if (opcode == WB) {
      int matches = ((vm->input == vm->input_start &&
                      vm->next_input == vm->input_start) &&
                     (vm->continuation ?
                       isword(vm->continuation) !=
                         vm_is_word_at(vm, vm->input) :
                       vm_is_word_at(vm, vm->input))) ||
                    vm_is_word_at(vm, vm->next_input) !=
                      vm_is_word_at(vm, vm->input);
      if (!matches) {
        if (!vm_fail_thread(vm, &pc, &capture))
          return;
        continue;
      }
      pc++;
      continue;
    }

    if (opcode == WBEG) {
      /* At a search-window start, continuation is the preceding byte. */
      int previous_is_word =
        (vm->input == vm->input_start &&
         vm->next_input == vm->input_start) ?
          (vm->continuation ? isword(vm->continuation) : 0) :
          vm_is_word_at(vm, vm->input);
      int fails = previous_is_word ||
                  !vm_is_word_at(vm, vm->next_input);
      if (fails) {
        if (!vm_fail_thread(vm, &pc, &capture))
          return;
        continue;
      }
      pc++;
      continue;
    }

    if (vm_is_negative_split(opcode)) {
      opcode = -opcode;
      if (!vm_mark_split(vm, opcode)) {
        if (!vm_fail_thread(vm, &pc, &capture))
          return;
        continue;
      }
      vm_expand_split(vm, target_threads, target_count,
                      &pc, capture, 1);
      continue;
    }

    if (opcode == WEND) {
      int current_is_word =
        (vm->last &&
         vm->input == vm->input_start &&
         vm->next_input == vm->input_start) ?
          (vm->continuation ? isword(vm->continuation) : 0) :
          vm_is_word_at(vm, vm->input);
      if (!current_is_word ||
          vm_is_word_at(vm, vm->next_input)) {
        if (!vm_fail_thread(vm, &pc, &capture))
          return;
        continue;
      }
      pc++;
      continue;
    }

    if (opcode == EOL) {
      if (!vm->last) {
        if (!vm_fail_thread(vm, &pc, &capture))
          return;
        continue;
      }
      pc++;
      continue;
    }

    if (opcode == JMP) {
      pc += 2 + pc[1];
      continue;
    }

    if (opcode == BOL) {
      if (vm->input != vm->bol_start || vm->next_input != vm->bol_start) {
        if (!vm_fail_thread(vm, &pc, &capture))
          return;
        continue;
      }
      pc++;
      continue;
    }

    if (vm->next_input != vm->input_start) {
      if (!vm_fail_thread(vm, &pc, &capture))
        return;
      continue;
    }
    pc++;
  }
}

static inline int vm_record_match(vm_context *vm, int *pc, rsub *capture)
{
  /* Retain the highest-priority capture state and enqueue its terminal PC. */
  if (pc != &vm->match_pc) {
    if (vm->matched)
      vm_release_capture(vm, vm->matched);
    vm->matched = capture;
  }
  vm->next_threads[vm->next_count].pc = &vm->match_pc;
  vm->next_threads[vm->next_count].sub = NULL;
  vm->next_count++;
  return vm->input == vm->next_input || vm->next_count == 1;
}

static inline int vm_finish_match(vm_context *vm)
{
  /* Copy the selected capture boundaries to the caller's output array. */
  int output_index;
  int capture_index;
  for (output_index = 0, capture_index = 0;
       output_index < vm->capture_count;
       output_index += 2, capture_index++) {
    vm->capture_output[output_index] = vm->matched->sub[capture_index];
    vm->capture_output[output_index + 1] =
      vm->matched->sub[vm->capture_count / 2 + capture_index];
  }
  return 1;
}

/*
 * Run the Pike VM over a bounded byte range. `continuation` is the byte just
 * before input_start for word-boundary checks; `bol_start` is the absolute
 * position that ^ may accept. Captures returned through capture_output point
 * into the caller-owned input and remain valid only while that input does.
 */
int re_pikevm(rcode *prog, const char *input_start, int input_length,
              const char **capture_output, int capture_count,
              int insensitive, int utf8, const char *continuation,
              const char *bol_start)
{
  vm_context vm;
  int input_width;
  int input_codepoint;
  int *program_counter;
  rsub *capture;
  rthread *thread_swap;

  vm.prog = prog;
  vm.input_start = input_start;
  vm.input_end = input_start + input_length;
  vm.bol_start = bol_start ? bol_start : input_start;
  vm.input = input_start;
  vm.next_input = input_start;
  vm.continuation = continuation;
  vm.input_length = input_length;
  vm.insensitive = insensitive;
  vm.utf8 = utf8;
  vm.last = input_length == 0;
  vm.match_pc = MATCH;
  vm.capture_count = capture_count;
  vm.capture_bytes = capture_count * sizeof(char*);
  vm.capture_record_size = prog->presub;
  vm.capture_storage_offset = 0;
  vm.deferred_pcs = TINYRE_STACK_ALLOC(sizeof(*vm.deferred_pcs) * prog->splits);
  vm.deferred_captures = TINYRE_STACK_ALLOC(sizeof(*vm.deferred_captures) * prog->splits);
  vm.deferred_count = 0;
  vm.sparse_dense = TINYRE_STACK_ALLOC(sizeof(*vm.sparse_dense) * prog->sparsesz);
  vm.sparse_count = 0;
  vm.current_threads = TINYRE_STACK_ALLOC(sizeof(*vm.current_threads) * prog->len);
  vm.next_threads = TINYRE_STACK_ALLOC(sizeof(*vm.next_threads) * prog->len);
  vm.current_count = 0;
  vm.next_count = 0;
  vm.capture_storage = TINYRE_STACK_ALLOC(prog->sub);
  vm.free_captures = NULL;
  vm.matched = NULL;
  vm.capture_output = capture_output;

  capture = vm_take_capture(&vm);
  memset(capture->sub, 0, vm.capture_bytes);
  capture->sub[0] = vm.next_input;
  program_counter = prog->insts;
  vm_expand(&vm, vm.current_threads, &vm.current_count,
            &program_counter, &capture, VM_SAVE_CURRENT);

  for (;;) {
    vm.input = vm.next_input;
    if (vm.last) {
      input_width = 0;
      input_codepoint = 0;
    } else {
      int remaining = (int)(vm.input_end - vm.input);
      input_width = uc_len_bounded(vm.input, remaining, vm.utf8);
      input_codepoint = uc_code_bounded(vm.input, remaining, vm.utf8);
    }
    vm.next_input = vm.input + input_width;
    if (vm.next_input >= vm.input_start + vm.input_length)
      vm.last = 1;

    vm.next_count = 0;
    vm.sparse_count = 0;
    for (int thread_index = 0; thread_index < vm.current_count; thread_index++) {
      int *pc = vm.current_threads[thread_index].pc;
      rsub *thread_capture = vm.current_threads[thread_index].sub;
      int opcode = *pc;

      if (opcode == CHAR) {
        int expected = *(pc + 1);
        int actual = vm.insensitive ? unicode_tolower(input_codepoint) : input_codepoint;
        if (vm.insensitive)
          expected = unicode_tolower(expected);
        if (actual != expected) {
          vm_release_capture(&vm, thread_capture);
          continue;
        }
        pc += 2;
      } else if (opcode == CLASS) {
        if (!re_classmatch(pc + 1, input_codepoint, vm.insensitive)) {
          vm_release_capture(&vm, thread_capture);
          continue;
        }
        pc += *(pc + 2) * 2 + 3;
      } else if (opcode == MATCH) {
        if (vm_record_match(&vm, pc, thread_capture))
          return vm_finish_match(&vm);
        vm_discard_threads(&vm, vm.current_threads,
                           thread_index + 1, vm.current_count);
        break;
      } else {
        pc++;
      }

      vm_expand(&vm, vm.next_threads, &vm.next_count,
                &pc, &thread_capture, VM_SAVE_NEXT);
    }

    if (vm.input == vm.next_input)
      break;

    thread_swap = vm.current_threads;
    vm.current_threads = vm.next_threads;
    vm.next_threads = thread_swap;
    vm.current_count = vm.next_count;

    capture = vm_take_capture(&vm);
    memset(capture->sub, 0, vm.capture_bytes);
    capture->sub[0] = vm.next_input;
    program_counter = prog->insts;
    vm_expand(&vm, vm.current_threads, &vm.current_count,
              &program_counter, &capture, VM_SAVE_CURRENT);
  }
  return 0;
}

typedef struct RE RE;
struct RE {
  const char **capture_output; /* Capture pointers returned by the last match. */
  char *program_storage;       /* Inline rcode allocation after the pointer array. */
  int capture_count;            /* Number of start/end pointers, including group 0. */
  int capture_groups;           /* Number of user capture groups. */
  int insensitive;              /* Case-insensitive flag. */
  int utf8;                     /* UTF-8 code-point mode flag. */
  int allocation_size;           /* Total bytes allocated for this RE object. */
};

/* Compile a pattern and allocate one self-contained RE object. */
RE* re_compile(const char *pattern, int insensitive, int utf8) {
  int sub_els;
  int sz = re_sizecode(pattern, &sub_els, utf8) * sizeof(int);
  if (sz < 0) return NULL;
  int count = (sub_els + 1) * 2;
  int captures_size = count * sizeof(char*);
  int buffer_size = sizeof(rcode) + sz;

  RE* re = (RE*) malloc(sizeof(RE) + captures_size + buffer_size);
  if(!re) return NULL;

  re->capture_groups = sub_els;
  re->capture_output = (const char**) (((char*)re) + sizeof(RE));
  re->program_storage = (char*) re + sizeof(RE) + captures_size;
  re->capture_count = count;
  re->insensitive = insensitive;
  re->utf8 = utf8;
  re->allocation_size = sizeof(RE) + captures_size + buffer_size;

  if (re_comp((rcode *)re->program_storage, pattern, sub_els, utf8)) {
    free(re);
    return NULL;
  }
  return re;
}

/* Deep-copy an RE object, including its inline program and capture metadata. */
RE* re_dup(RE* re) {
  if (!re || re->allocation_size == 0) return NULL;
  RE* newre = malloc(re->allocation_size);
  if (!newre) return NULL;
  memcpy(newre, re, re->allocation_size);
  newre->capture_output =
      (const char**) ((char*)newre + sizeof(RE));
  newre->program_storage =
      (char*)newre + sizeof(RE) + newre->capture_count * sizeof(char*);
  return newre;
}

/* Return the two matching-mode flags stored in an RE object. */
void re_flags(RE* re, int* insensitive, int* utf8) {
  *insensitive = re->insensitive;
  *utf8 = re->utf8;
}

/* Return the number of start/end pointers written for one match. */
int re_max_matches(RE* re) {
  return re->capture_count;
}

/* Return one input-unit width using the RE's UTF-8 mode. */
int re_uc_len(RE* re, const char * s) {
  return uc_len(s, re->utf8);
}

/* Return one bounded input-unit width without reading beyond `length`. */
int re_uc_len_bounded(RE* re, const char * s, int length) {
  return uc_len_bounded(s, length, re->utf8);
}

/* Report whether the compiled program contains only its terminal SAVE/MATCH. */
int re_is_empty(RE* re) {
  return ((rcode *)re->program_storage)->unilen == 3;
}

/* Release an RE object allocated by re_compile or re_dup. */
void re_free(RE* re) {
  free(re);
}

/*
 * Match from `string` over exactly `len` bytes. The returned capture pointers
 * refer to that caller-owned range; `cont` and `bol_start` provide context
 * outside the range for word-boundary and beginning-of-buffer assertions.
 */
const char** re_match_from(RE* re, const char* string, int len,
                           const char* cont, const char* bol_start) {
  if (re == NULL) return NULL;

  memset(re->capture_output, 0, re->capture_count * sizeof(char*));
  int sz = re_pikevm((rcode *)re->program_storage, string, len,
                     re->capture_output, re->capture_count,
                     re->insensitive, re->utf8, cont, bol_start);

  if (!sz) return NULL;
  return re->capture_output;
}

/* Match with the beginning-of-buffer position equal to `string`. */
const char** re_match(RE* re, const char* string, int len, const char* cont) {
  return re_match_from(re, string, len, cont, string);
}
