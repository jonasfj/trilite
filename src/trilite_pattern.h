#ifndef TRILITE_TRILITE_PATTERN_H
#define TRILITE_TRILITE_PATTERN_H

/** TriLite Pattern Interface
 * This file declares the interface required to implement a custom match pattern
 * format. Each match trilite pattern have a prefix and an associated parser
 * that returns a trilite_pattern object as declared in this file.
 *
 * Examples of match patterns and associated are "substr:" and "regexp:" which
 * are parsed as substring and regular expression queries, respectively.
 */

#include <sqlite3ext.h>

typedef struct trilite_pattern_format trilite_pattern_format;
typedef struct trilite_pattern trilite_pattern;
typedef struct trilite_expression trilite_expression;
typedef sqlite3_int64 trilite_trigram;


/** TriLite pattern invocation context
 * This structure keeps track of what context the pattern methods are invoked in
 * as there are different error resolution procedures depending on whether or
 * not a function is invoked as scalar function or used for filtering results.
 */
typedef struct trilite_context trilite_context;


/** TriLite match pattern format
 * To create a custom match pattern format, you must create a (static const)
 * instance of this struct, implement the declared methods and register it with
 * trilite using trilite_register_format()
 */
struct trilite_pattern_format{
  /** Pattern prefix used in MATCH clause
   * For example the 'substr' prefix is used below:
   *    SELECT * FROM table WHERE text MATCH 'substr:pattern';
   * Prefixes must be unique and may not contain colon ':'
   */
  const unsigned char   *zPrefix;

  /** Parse match pattern
   * This method shall parse zPattern with a length of nPattern. The pattern
   * does not include the prefix, nor the ':' used as separator.
   * This function returns SQLITE_OK, on success and returns the result as
   * *ppPattern.
   *
   * On error this function does not return SQLITE_OK, and sets and error using
   * trilite_error(pCtx, ...)
   * This method is called in the pCtx context, this context is ONLY valid
   * for the duration of this invocation.
   */
  int (*xParse)(
    trilite_context     *pCtx,      /** Context of this invocation */
    int                 nPattern,   /** Length of zPattern in bytes */
    const unsigned char *zPattern,  /** Pattern for parsing */
    trilite_pattern     **ppPattern /** OUT: Parsed pattern */
  );

  /** Extract trigram expression from pattern
   * Given a substring pattern like 'abcd' it's easy to extract an expression
   * which says trigram 'abc' and 'bcd' must be present.
   * For regular expressions and other patterns the trigram expressions may be
   * more complicated. trilite_expressions can be constructed using the
   * trilite_expr_% methods declared in the bottom of this file.
   * Please take care and construct the trilite_expression with as little
   * recursion depth as possible.
   *
   * This method return SQLITE_OK on success and sets *ppExpr to an expression.
   * On error an error code (not SQLITE_OK) is returned and an error message
   * communicated using trilite_error(pCtx, ...)
   */
  int (*xExtractTrigrams)(
    trilite_context     *pCtx,      /** Context of this invocation */
    trilite_pattern     *pPattern,  /** Pattern created by xParse */
    trilite_expression  **ppExpr    /** OUT: Extracted trigram expression */
  );

  /** Match pattern against text
   * Set *bMatch true if a match is found in zText, and false if no match is
   * found. This method does the actual string matching and is used as scalar
   * function to remove false positives that matches the trigram expression.
   * 
   * This method return SQLITE_OK on success and sets *bMatch to true or false.
   * On error an error code (not SQLITE_OK) is returned and an error message
   * communicated using trilite_error(pCtx, ...)
   */
  int (*xMatch)(
    trilite_context     *pCtx,      /** Context of this invocation */
    trilite_pattern     *pPattern,  /** Pattern created by xParse */
    int                 nText,      /** Length of zText in bytes */
    const unsigned char *zText,     /** Text to be match against pattern */
    bool                *bMatch     /** OUT: True, pPattern matches zText */
  );

  /** Find extents of all matches in text
   * This method finds the start and end extents of all matches for the pattern
   * in a given text. Extents, (start, end) tuples, are reported to the calling
   * context using trilite_result_extent(pCtx, start, end).
   * Extents must be reported in order of occurrence in the text, ie. sorted by
   * start offset as ties resolved using end, with preference to short matches.
   * Overlapping extents are not encouraged, but fully allows, if it makes sense
   * in your use case.
   *
   * This method return SQLITE_OK on success after communicating all extents.
   * On error an error code (not SQLITE_OK) is returned and an error message
   * communicated using trilite_error(pCtx, ...)
   */
  int (*xExtents)(
    trilite_context     *pCtx,      /** Context of this invocation */
    trilite_pattern     *pPattern,  /** Pattern created by xParse */
    int                 nText,      /** Length of zText in bytes */
    const unsigned char *zText      /** Text to be match against pattern */
  );

  /** Release resources held by pattern
   * This method may assert that nRefs in trilite_pattern is 0, but it must
   * still release the memory should this not be case!
   * This method returns SQLITE_OK on success and an error code on error.
   * Error messages can be communicated using trilite_error(pCtx, ...)
   */
  int (*xDestroy)(
    trilite_context     *pCtx,      /** Context of this invocation */
    trilite_pattern     *pPattern   /** Pattern create by xParse */
  );
};

/** Base struct for trilite pattern
 * The pattern object returned by xParse must be a subclass of this struct.
 * This can be done by adding trilite_pattern as first element in the structure.
 * xParse is not responsible for setting the fields in this struct, but the
 * other methods in trilite_pattern_format can rely on these fields being
 * present when they are invoked.
 */
struct trilite_pattern{
  int                     nRefs;            /** Reference counter */
  int                     nPattern;         /** Length of zPattern */
  const unsigned char     *zPattern;        /** Pattern given to xParse */
  trilite_pattern_format  *pPatternFormat;  /** Pattern format implementation */
};


/** Report error to given invocation context
 * Functions in trilite_pattern_format instances reports errors using this
 * method, and returns SQLITE_ERROR or other error code not SQLITE_OK.
 */
void trilite_error(trilite_context *pCtx, const char *zFmt, ...);

/** Report an extent to given invocation context
 * An extent is the number of bytes from beginning of text to the start of the
 * match and the number of bytes from the beginning of text to the end of the
 * match.
 * This function may only be used inside xExtents implementations to return
 * extents of matches. The extents must be returned in order by start offset,
 * starting with the first match, ties are solve by returning the match that
 * ends first.
 */
void trilite_result_extent(trilite_context *pCtx, int start, int end);

/** Register a match pattern format with TriLite
 * A match pattern may only be registered once, also the prefix must be unique
 * and cannot contain colon. Once a format is registered it will be available
 * within the trilite extension to which it was loaded, that all virtual tables
 * and database connections where the TriLite extension is loaded.
 * This function will return SQLITE_OK, on success and SQLITE_ERROR on failure.
 */
int trilite_register_format(trilite_pattern_format *pPatternFormat);

/** Create a trigram expression matching anything
 * This function may only be used in xExtractTrigrams.
 * Returns SQLITE_OK on success, sets error messsage on trilite_context and
 * return error code on failure.
 */
int trilite_expr_any(
  trilite_context         *pCtx,      /** TriLite invocation context */
  trilite_expression      **ppExpr    /** OUT: Trigram expression object */
);

/** Create a trigram expression matching nothing
 * This function may only be used in xExtractTrigrams.
 * Returns SQLITE_OK on success, sets error messsage on trilite_context and
 * return error code on failure.
 */
int trilite_expr_none(
  trilite_context         *pCtx,      /** TriLite invocation context */
  trilite_expression      **ppExpr    /** OUT: Trigram expression object */
);

/** Create a trigram expression matching a given trigram
 * This function may only be used in xExtractTrigrams.
 * Returns SQLITE_OK on success, sets error messsage on trilite_context and
 * return error code on failure.
 */
int trilite_expr_trigram(
  trilite_context         *pCtx,      /** TriLite invocation context */
  trilite_trigram         trigram,    /** Trigram */
  trilite_expression      **ppExpr    /** OUT: Trigram expression object */
);

/** Create a trigram expression with trigrams from string
 * This function may only be used in xExtractTrigrams.
 * Returns SQLITE_OK on success, sets error messsage on trilite_context and
 * return error code on failure.
 */
int trilite_expr_string(
  trilite_context         *pCtx,      /** TriLite invocation context */
  int                     nString,    /** Length of input string */
  const unsigned char     *zString,   /** String to extract trigrams from */
  trilite_expression      **ppExpr    /** OUT: Trigram expression object */
);

/** OR operator constant
 * Used with trilite_expr_operator to create an OR operator node
 */
#define TRILITE_EXPR_OP_OR              ((1 << 1) | (1 << 2))
/** AND operator constant
 * Used with trilite_expr_operator to create an AND operator node
 */
#define TRILITE_EXPR_OP_AND             ((1 << 1) | (1 << 3))

/** Create a trigram expression operator
 * To create an operator node for the trigram expression tree, you must provide
 * the operator type (eType) either TRILITE_EXPR_OP_OR or TRILITE_EXPR_OP_AND
 * and the number of children (nSize) that you wish this node to have.
 * A newly created operator node does not have any children and is, thus,
 * equivalent to the none expression.
 * You may add as many as nSize children to the expression after creation, but
 * the node cannot be resized, and once full no more children may be added.
 */
int trilite_expr_operator(
  trilite_context         *pCtx,      /** TriLite invocation context */
  int                     eType,      /** Operator type (TRILITE_EXPR_OP_*) */
  int                     nSize,      /** Maximum number of entries */
  trilite_expression      **ppExpr    /** OUT: Trigram expression object */
);

/** Append expression to operator expression
 * Append an operand to a previously created operator, after this operation the
 * operator will take ownership of the operand.
 * Be advised that this method does on-the-fly simplification, so adding any or
 * none expressions to AND/OR operators will not induce any overhead.
 * The simplification is transparent to the caller.
 */
int trilite_expr_append(
  trilite_context         *pCtx,       /** TriLite invocation context */
  trilite_expression      **ppOperator,/** Operator expression */
  trilite_expression      *pOperand    /** Operand expression to be appended */
);

#endif /* TRILITE_TRILITE_PATTERN_H */