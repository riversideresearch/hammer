#include "jhammer.h"
#include "com_upstandinghackers_hammer_Hammer.h"
#include <stdlib.h>

JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_parse
  (JNIEnv *env, jclass class, jobject obj, jbyteArray input_, jint length_)
{
    HParser *parser;
    uint8_t* input;
    size_t length;
    HParseResult *result;
    jclass resultClass;
    jobject retVal;

    parser = UNWRAP(env, obj);
    
    input = (uint8_t *) ((*env)->GetByteArrayElements(env, input_, NULL));
    length = (size_t) length_;

    result = h_parse(parser, input, length);
    
    if(result==NULL)
        return NULL;

    FIND_CLASS(resultClass, env, "com/upstandinghackers/hammer/ParseResult");
    
    NEW_INSTANCE(retVal, env, resultClass, result);
 
    return retVal;
}

JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_token
  (JNIEnv *env, jclass class, jbyteArray str, jint len)
{
    RETURNWRAP(env, h_token((uint8_t *) ((*env)->GetByteArrayElements(env, str, NULL)), (size_t) len));
}

JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_ch
  (JNIEnv *env, jclass class, jbyte c)
{
    RETURNWRAP(env, h_ch((uint8_t) c));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_chRange
  (JNIEnv *env, jclass class, jbyte lower, jbyte upper)
{
    
    RETURNWRAP(env, h_ch_range((uint8_t) lower, (uint8_t) upper));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_intRange
  (JNIEnv *env, jclass class, jobject obj, jlong lower, jlong upper)
{
    HParser *parser;
    parser = UNWRAP(env, obj); 
    RETURNWRAP(env, h_int_range(parser, (int64_t) lower, (int64_t) upper));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_bits
  (JNIEnv *env, jclass class, jint len, jboolean sign)
{
    RETURNWRAP(env, h_bits((size_t) len, (bool)(sign & JNI_TRUE)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_int64
  (JNIEnv *env, jclass class)
{
    RETURNWRAP(env, h_int64());
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_int32
  (JNIEnv *env, jclass class)
{
    RETURNWRAP(env, h_int32());
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_int16
  (JNIEnv *env, jclass class)
{
    RETURNWRAP(env, h_int16()); 
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_int8
  (JNIEnv *env, jclass class)
{
    RETURNWRAP(env, h_int8());
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_uInt64
  (JNIEnv *env, jclass class)
{
    RETURNWRAP(env, h_uint64());
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_uInt32
  (JNIEnv *env, jclass class)
{
    RETURNWRAP(env, h_uint32()); 
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_uInt16
  (JNIEnv *env, jclass class)
{
    RETURNWRAP(env, h_uint16());
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_uInt8
  (JNIEnv *env, jclass class)
{
    RETURNWRAP(env, h_uint8());
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_whitespace
  (JNIEnv *env, jclass class, jobject parser)
{
    RETURNWRAP(env, h_whitespace(UNWRAP(env, parser)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_left
  (JNIEnv *env, jclass class, jobject p, jobject q)
{
    RETURNWRAP(env, h_left(UNWRAP(env, p), UNWRAP(env, q)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_right
  (JNIEnv *env, jclass class, jobject p, jobject q)
{
    RETURNWRAP(env, h_right(UNWRAP(env, p), UNWRAP(env, q)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_middle
  (JNIEnv *env, jclass class, jobject p, jobject x, jobject q)
{
    RETURNWRAP(env, h_middle(UNWRAP(env, p), UNWRAP(env, x), UNWRAP(env, q)));
}

/**
 * Given another parser, p, and a function f, returns a parser that
 * applies p, then applies f to everything in the AST of p's result.
 *
 * Result token type: any
 */
//HAMMER_FN_DECL(HParser*, h_action, const HParser* p, const HAction a, void* user_data);

JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_in
  (JNIEnv *env, jclass class, jbyteArray charset, jint length)
{
    RETURNWRAP(env, h_in((uint8_t *) ((*env)->GetByteArrayElements(env, charset, NULL)), (size_t)length));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_notIn
  (JNIEnv *env, jclass class, jbyteArray charset, jint length)
{
    RETURNWRAP(env, h_not_in((uint8_t*) ((*env)->GetByteArrayElements(env, charset, NULL)), (size_t)length));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_endP
  (JNIEnv *env, jclass class)
{
    RETURNWRAP(env, h_end_p());
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_nothingP
  (JNIEnv *env, jclass class)
{
    RETURNWRAP(env, h_nothing_p());
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_sequence
  (JNIEnv *env, jclass class, jobjectArray sequence)
{
    jsize length;
    void **parsers;
    int i;
    jobject current;
    const HParser *result;

    length = (*env)->GetArrayLength(env, sequence);
    parsers = malloc(sizeof(void *)*(length+1));
    if(NULL==parsers)
    {
        return NULL;
    }

    for(i=0; i<length; i++)
    {
        current = (*env)->GetObjectArrayElement(env, sequence, (jsize)i);
        parsers[i] = UNWRAP(env, current);
    }
    parsers[length] = NULL;
    
    result = h_sequence__a(parsers);
    RETURNWRAP(env, result);
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_choice
  (JNIEnv *env, jclass class, jobjectArray choices)
{
    jsize length;
    void **parsers;
    int i;
    jobject current;
    const HParser *result;

    length = (*env)->GetArrayLength(env, choices);
    parsers = malloc(sizeof(HParser *)*(length+1));
    if(NULL==parsers)
    {
        return NULL;
    }

    for(i=0; i<length; i++)
    {
        current = (*env)->GetObjectArrayElement(env, choices, (jsize)i);
        parsers[i] = UNWRAP(env, current);
    }
    parsers[length] = NULL;

    result = h_choice__a(parsers);
    RETURNWRAP(env, result);
}

/**
 * Given a null-terminated list of parsers, match a permutation phrase of these
 * parsers, i.e. match all parsers exactly once in any order.
 *
 * If multiple orders would match, the lexically smallest permutation is used;
 * in other words, at any step the remaining available parsers are tried in
 * the order in which they appear in the arguments.
 *
 * As an exception, 'h_optional' parsers (actually those that return a result
 * of token type TT_NONE) are detected and the algorithm will try to match them
 * with a non-empty result. Specifically, a result of TT_NONE is treated as a
 * non-match as long as any other argument matches.
 *
 * Other parsers that succeed on any input (e.g. h_many), that match the same
 * input as others, or that match input which is a prefix of another match can
 * lead to unexpected results and should probably not be used as arguments.
 *
 * The result is a sequence of the same length as the argument list.
 * Each parser's result is placed at that parser's index in the arguments.
 * The permutation itself (the order in which the arguments were matched) is
 * not returned.
 *
 * Result token type: TT_SEQUENCE
 */
//HAMMER_FN_DECL_VARARGS_ATTR(H_GCC_ATTRIBUTE((sentinel)), HParser*, h_permutation, HParser* p);
JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_permutation
  (JNIEnv *env, jclass class, jobjectArray permutation)
{
    jsize length;
    void **parsers;
    int i;
    jobject current;
    const HParser *result;

    length = (*env)->GetArrayLength(env, permutation);
    parsers = malloc(sizeof(HParser *)*(length+1));
    if(NULL==parsers)
    {
        return NULL;
    }

    for(i=0; i<length; i++)
    {
        current = (*env)->GetObjectArrayElement(env, permutation, (jsize)i);
        parsers[i] = UNWRAP(env, current);
    }
    parsers[length] = NULL;

    result = h_permutation__a(parsers);
    RETURNWRAP(env, result);
}

JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_butNot
  (JNIEnv *env, jclass class, jobject p, jobject q)
{
    RETURNWRAP(env, h_butnot(UNWRAP(env, p), UNWRAP(env, q)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_difference
  (JNIEnv *env, jclass class, jobject p, jobject q)
{
    RETURNWRAP(env, h_difference(UNWRAP(env, p), UNWRAP(env, q)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_xor
  (JNIEnv *env, jclass class, jobject p, jobject q)
{
    RETURNWRAP(env, h_xor(UNWRAP(env, p), UNWRAP(env, q)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_many
  (JNIEnv *env, jclass class, jobject p)
{
    RETURNWRAP(env, h_many(UNWRAP(env, p)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_many1
  (JNIEnv *env, jclass class, jobject p)
{
    RETURNWRAP(env, h_many1(UNWRAP(env, p)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_repeatN
  (JNIEnv *env, jclass class, jobject p, jint n)
{
    RETURNWRAP(env, h_repeat_n(UNWRAP(env, p), (size_t)n));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_optional
  (JNIEnv *env, jclass class, jobject p)
{
    RETURNWRAP(env, h_optional(UNWRAP(env, p)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_ignore
  (JNIEnv *env, jclass class, jobject p)
{
    RETURNWRAP(env, h_ignore(UNWRAP(env, p)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_sepBy
  (JNIEnv *env, jclass class, jobject p, jobject sep)
{
    RETURNWRAP(env, h_sepBy(UNWRAP(env, p), UNWRAP(env, sep)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_sepBy1
  (JNIEnv *env, jclass class, jobject p, jobject sep)
{
    RETURNWRAP(env, h_sepBy1(UNWRAP(env, p), UNWRAP(env, sep)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_epsilonP
  (JNIEnv *env, jclass class)
{
    RETURNWRAP(env, h_epsilon_p());
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_lengthValue
  (JNIEnv *env, jclass class, jobject length, jobject value)
{
    RETURNWRAP(env, h_length_value(UNWRAP(env, length), UNWRAP(env, value)));
}

/**
 * This parser attaches a predicate function, which returns true or
 * false, to a parser. The function is evaluated over the parser's
 * result.
 *
 * The parse only succeeds if the attribute function returns true.
 *
 * attr_bool will check whether p's result exists and whether p's
 * result AST exists; you do not need to check for this in your
 * predicate function.
 *
 * Result token type: p's result type if pred succeeded, NULL otherwise.
 */
//HAMMER_FN_DECL(HParser*, h_attr_bool, const HParser* p, HPredicate pred, void* user_data);


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_and
  (JNIEnv *env, jclass class, jobject p)
{
    RETURNWRAP(env, h_and(UNWRAP(env, p)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_not
  (JNIEnv *env, jclass class, jobject p)
{
    RETURNWRAP(env, h_not(UNWRAP(env, p)));
}


JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_indirect
  (JNIEnv *env, jclass class)
{
    RETURNWRAP(env, h_indirect());
}


/**
 * Set the inner parser of an indirect. See comments on indirect for
 * details.
 */
//HAMMER_FN_DECL(void, h_bind_indirect, HParser* indirect, const HParser* inner);
//JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_bind_indirect
//  (JNIEnv *env, jclass class, jobject indirect, jobject inner)
//{
//    RETURNWRAP(env, h_bind_indirect(UNWRAP(env, indirect), UNWRAP(env, inner)));
//}


/**
 * This parser runs its argument parser with the given endianness setting.
 *
 * The value of 'endianness' should be a bit-wise or of the constants
 * BYTE_BIG_ENDIAN/BYTE_LITTLE_ENDIAN and BIT_BIG_ENDIAN/BIT_LITTLE_ENDIAN.
 *
 * Result token type: p's result type.
 */
//HAMMER_FN_DECL(HParser*, h_with_endianness, char endianness, const HParser* p);
JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_with_endianness
  (JNIEnv *env, jclass class, jbyte endianess, jobject p)
{
    RETURNWRAP(env, h_with_endianness((char) endianess, UNWRAP(env, p)));
}


/**
 * The 'h_put_value' combinator stashes the result of the parser
 * it wraps in a symbol table in the parse state, so that non-
 * local actions and predicates can access this value.
 *
 * Try not to use this combinator if you can avoid it.
 *
 * Result token type: p's token type if name was not already in
 * the symbol table. It is an error, and thus a NULL result (and
 * parse failure), to attempt to rename a symbol.
 */
//HAMMER_FN_DECL(HParser*, h_put_value, const HParser *p, const char* name);

/**
 * The 'h_get_value' combinator retrieves a named HParseResult that
 * was previously stashed in the parse state.
 *
 * Try not to use this combinator if you can avoid it.
 *
 * Result token type: whatever the stashed HParseResult is, if
 * present. If absent, NULL (and thus parse failure).
 */
//HAMMER_FN_DECL(HParser*, h_get_value, const char* name);

/**
 * Monadic bind for HParsers, i.e.:
 * Sequencing where later parsers may depend on the result(s) of earlier ones.
 *
 * Run p and call the result x. Then run k(env,x).  Fail if p fails or if
 * k(env,x) fails or if k(env,x) is NULL.
 *
 * Result: the result of k(x,env).
 */
//HAMMER_FN_DECL(HParser*, h_bind, const HParser *p, HContinuation k, void *env);

/**
 * This parser skips 'n' bits of input.
 *
 * Result: None. The HParseResult exists but its AST is NULL.
 */
//HAMMER_FN_DECL(HParser*, h_skip, size_t n);
JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_skip
  (JNIEnv *env, jclass class, jint n)
{
    RETURNWRAP(env, h_skip((size_t) n));
}

/**
 * The HParser equivalent of fseek(), 'h_seek' modifies the parser's input
 * position.  Note that contrary to 'fseek', offsets are in bits, not bytes.
 * The 'whence' argument uses the same values and semantics: SEEK_SET,
 * SEEK_CUR, SEEK_END.
 *
 * Fails if the new input position would be negative or past the end of input.
 *
 * Result: TT_UINT. The new input position.
 */
//HAMMER_FN_DECL(HParser*, h_seek, ssize_t offset, int whence);
//TODO double check mapping for int
JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_seek
  (JNIEnv *env, jclass class, jint offset, jint whence)
{
    RETURNWRAP(env, h_seek((ssize_t) offset, (int) whence));
}

/**
 * Report the current position in bits. Consumes no input.
 *
 * Result: TT_UINT. The current input position.
 */
//HAMMER_FN_DECL_NOARG(HParser*, h_tell);
JNIEXPORT jobject JNICALL Java_com_upstandinghackers_hammer_Hammer_tell
  (JNIEnv *env, jclass class)
{
    RETURNWRAP(env, h_tell());
}
