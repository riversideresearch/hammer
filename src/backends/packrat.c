#include <assert.h>
#include <string.h>
#include "../internal.h"
#include "../parsers/parser_internal.h"

/* #define DETAILED_PACKRAT_STATISTICS */

#ifdef DETAILED_PACKRAT_STATISTICS
static size_t packrat_hash_count = 0;
static size_t packrat_hash_bytes = 0;
static size_t packrat_cmp_count = 0;
static size_t packrat_cmp_bytes = 0;
#endif

static uint32_t cache_key_hash(const void* key);

// short-hand for creating lowlevel parse cache values (parse result case)
static
HParserCacheValue * cached_result(HParseState *state, HParseResult *result) {
  HParserCacheValue *ret = a_new(HParserCacheValue, 1);
  ret->value_type = PC_RIGHT;
  ret->right = result;
  ret->input_stream = state->input_stream;
  return ret;
}

// short-hand for creating lowlevel parse cache values (left recursion case)
static
HParserCacheValue *cached_lr(HParseState *state, HLeftRec *lr) {
  HParserCacheValue *ret = a_new(HParserCacheValue, 1);
  ret->value_type = PC_LEFT;
  ret->left = lr;
  ret->input_stream = state->input_stream;
  return ret;
}

// Really library-internal tool to perform an uncached parse, and handle any common error-handling.
static inline HParseResult* perform_lowlevel_parse(HParseState *state, const HParser *parser) {
  // TODO(thequux): these nested conditions are ugly. Factor this appropriately, so that it is clear which codes is executed when.
  HParseResult *tmp_res;
  if (parser) {
    HInputStream bak = state->input_stream;
    tmp_res = parser->vtable->parse(parser->env, state);
    if (tmp_res) {
      tmp_res->arena = state->arena;
      if (!state->input_stream.overrun) {
	size_t bit_length = h_input_stream_pos(&state->input_stream) - h_input_stream_pos(&bak);
	if (tmp_res->bit_length == 0) { // Don't modify if forwarding.
	  tmp_res->bit_length = bit_length;
	}
	if (tmp_res->ast && tmp_res->ast->bit_length != 0) {
	  ((HParsedToken*)(tmp_res->ast))->bit_length = bit_length;
	}
      } else
	tmp_res->bit_length = 0;
    }
  } else
    tmp_res = NULL;
  if (state->input_stream.overrun)
    return NULL; // overrun is always failure.
#ifdef CONSISTENCY_CHECK
  if (!tmp_res) {
    state->input_stream = INVALID;
    state->input_stream.input = key->input_pos.input;
  }
#endif
  return tmp_res;
}

HParserCacheValue* recall(HParserCacheKey *k, HParseState *state, HHashValue keyhash) {
  HParserCacheValue *cached = h_hashtable_get_precomp(state->cache, k, keyhash);
  HRecursionHead *head = h_hashtable_get(state->recursion_heads, &k->input_pos);

  if (!head) {
    /* No heads found */
    return cached;
  } else {
    /* Some heads found */
    if (!cached && head->head_parser != k->parser && !h_slist_find(head->involved_set, k->parser)) {
      /* Nothing in the cache, and the key parser is not involved */
      cached = cached_result(state, NULL);
      cached->input_stream = k->input_pos;
    }
    if (h_slist_find(head->eval_set, k->parser)) {
      /*
       * Something is in the cache, and the key parser is in the eval set.
       * Remove the key parser from the eval set of the head.
       */
      head->eval_set = h_slist_remove_all(head->eval_set, k->parser);
      HParseResult *tmp_res = perform_lowlevel_parse(state, k->parser);
      /* update the cache */
      if (!cached) {
        cached = cached_result(state, tmp_res);
        h_hashtable_put_precomp(state->cache, k, cached, keyhash);
      } else {
        cached->value_type = PC_RIGHT;
        cached->right = tmp_res;
        cached->input_stream = state->input_stream;
      }
    }

    return cached;
  }
}

/* Setting up the left recursion. We have the LR for the rule head;
 * we modify the involved_sets of all LRs in the stack, until we 
 * see the current parser again.
 */

void setupLR(const HParser *p, HParseState *state, HLeftRec *rec_detect) {
  if (!rec_detect->head) {
    HRecursionHead *some = a_new(HRecursionHead, 1);
    some->head_parser = p;
    some->involved_set = h_slist_new(state->arena);
    some->eval_set = NULL;
    rec_detect->head = some;
  }

  HSlistNode *it;
  for(it=state->lr_stack->head; it; it=it->next) {
    HLeftRec *lr = it->elem;

    if(lr->rule == p)
      break;

    lr->head = rec_detect->head;
    h_slist_push(lr->head->involved_set, (void*)lr->rule);
  }
}

// helper: true iff pos1 is less than pos2
static inline bool pos_lt(HInputStream pos1, HInputStream pos2) {
  return ((pos1.index < pos2.index) ||
	  (pos1.index == pos2.index && pos1.bit_offset < pos2.bit_offset));
}

/* If recall() returns NULL, we need to store a dummy failure in the cache and compute the
 * future parse. 
 */

HParseResult* grow(HParserCacheKey *k, HParseState *state, HRecursionHead *head) {
  // Store the head into the recursion_heads
  h_hashtable_put(state->recursion_heads, &k->input_pos, head);
  HParserCacheValue *old_cached = h_hashtable_get(state->cache, k);
  if (!old_cached || PC_LEFT == old_cached->value_type)
    h_platform_errx(1, "impossible match");
  HParseResult *old_res = old_cached->right;

  // rewind the input
  state->input_stream = k->input_pos;
  
  // reset the eval_set of the head of the recursion at each beginning of growth
  head->eval_set = h_slist_copy(head->involved_set);
  HParseResult *tmp_res = perform_lowlevel_parse(state, k->parser);

  if (tmp_res) {
    if (pos_lt(old_cached->input_stream, state->input_stream)) {
      h_hashtable_put(state->cache, k, cached_result(state, tmp_res));
      return grow(k, state, head);
    } else {
      // we're done with growing, we can remove data from the recursion head
      h_hashtable_del(state->recursion_heads, &k->input_pos);
      HParserCacheValue *cached = h_hashtable_get(state->cache, k);
      if (cached && PC_RIGHT == cached->value_type) {
        state->input_stream = cached->input_stream;
	return cached->right;
      } else {
	h_platform_errx(1, "impossible match");
      }
    }
  } else {
    h_hashtable_del(state->recursion_heads, &k->input_pos);
    state->input_stream = old_cached->input_stream;
    return old_res;
  }
}

HParseResult* lr_answer(HParserCacheKey *k, HParseState *state, HLeftRec *growable) {
  if (growable->head) {
    if (growable->head->head_parser != k->parser) {
      // not the head rule, so not growing
      return growable->seed;
    }
    else {
      // update cache
      h_hashtable_put(state->cache, k, cached_result(state, growable->seed));
      if (!growable->seed)
	return NULL;
      else
	return grow(k, state, growable->head);
    }
  } else {
    h_platform_errx(1, "lrAnswer with no head");
  }
}

/* Warth's recursion. Hi Alessandro! */
HParseResult* h_do_parse(const HParser* parser, HParseState *state) {
  HParserCacheKey *key = a_new(HParserCacheKey, 1);
  HHashValue keyhash;
  HLeftRec *base = NULL;
  HParserCacheValue *m = NULL, *cached = NULL;

  key->input_pos = state->input_stream; key->parser = parser;
  keyhash = cache_key_hash(key);

  if (parser->vtable->higher) {
    m = recall(key, state, keyhash);
  }

  /* check to see if there is already a result for this object... */
  if (!m) {
    /*
     * But only cache it now if there's some chance it could grow; primitive
     * parsers can't
     */
    if (parser->vtable->higher) {
      base = a_new(HLeftRec, 1);
      base->seed = NULL; base->rule = parser; base->head = NULL;
      h_slist_push(state->lr_stack, base);
      /* cache it */
      h_hashtable_put_precomp(state->cache, key,
                              cached_lr(state, base), keyhash);
    }

    /* parse the input */
    HParseResult *tmp_res = perform_lowlevel_parse(state, parser);
    if (parser->vtable->higher) {
      /* the base variable has passed equality tests with the cache */
      h_slist_pop(state->lr_stack);
      /* update the cached value to our new position */
      cached = h_hashtable_get_precomp(state->cache, key, keyhash);
      assert(cached != NULL);
      cached->input_stream = state->input_stream;
    }

    /*
     * setupLR, used below, mutates the LR to have a head if appropriate,
     * so we check to see if we have one
     */
    if (!base || NULL == base->head) {
      if (parser->vtable->higher) {
        h_hashtable_put_precomp(state->cache, key,
                                cached_result(state, tmp_res), keyhash);
      }
      return tmp_res;
    } else {
      base->seed = tmp_res;
      HParseResult *res = lr_answer(key, state, base);
      return res;
    }
  } else {
    /* it exists! */
    state->input_stream = m->input_stream;
    if (PC_LEFT == m->value_type) {
      setupLR(parser, state, m->left);
      return m->left->seed;
    } else {
      return m->right;
    }
  }
}

int h_packrat_compile(HAllocator* mm__, HParser* parser, const void* params) {
  parser->backend_vtable = &h__packrat_backend_vtable;
  parser->backend = PB_PACKRAT;
  return 0; // No compilation necessary, and everything should work
	    // out of the box.
}

void h_packrat_free(HParser *parser) {
  parser->backend_vtable = h_get_default_backend_vtable();
  parser->backend = h_get_default_backend();
}

static uint32_t cache_key_hash(const void* key) {
#ifdef DETAILED_PACKRAT_STATISTICS
  ++(packrat_hash_count);
  packrat_hash_bytes += sizeof(HParserCacheKey);
#endif
  return h_djbhash(key, sizeof(HParserCacheKey));
}

static bool cache_key_equal(const void* key1, const void* key2) {
#ifdef DETAILED_PACKRAT_STATISTICS
  ++(packrat_cmp_count);
  packrat_cmp_bytes += sizeof(HParserCacheKey);
#endif
  return memcmp(key1, key2, sizeof(HParserCacheKey)) == 0;
}

static uint32_t pos_hash(const void* key) {
#ifdef DETAILED_PACKRAT_STATISTICS
  ++(packrat_hash_count);
  packrat_hash_bytes += sizeof(HInputStream);
#endif
  return h_djbhash(key, sizeof(HInputStream));
}

static bool pos_equal(const void* key1, const void* key2) {
#ifdef DETAILED_PACKRAT_STATISTICS
  ++(packrat_cmp_count);
  packrat_cmp_bytes += sizeof(HInputStream);
#endif
  return memcmp(key1, key2, sizeof(HInputStream)) == 0;
}

HParseResult *h_packrat_parse(HAllocator* mm__, const HParser* parser, HInputStream *input_stream) {
  HArena * arena = h_new_arena(mm__, 0);

  // out-of-memory handling
  jmp_buf except;
  h_arena_set_except(arena, &except);
  if(setjmp(except)) {
    h_delete_arena(arena);
    return NULL;
  }

  HParseState *parse_state = a_new_(arena, HParseState, 1);
  parse_state->cache = h_hashtable_new(arena, cache_key_equal, // key_equal_func
				       cache_key_hash); // hash_func
  parse_state->input_stream = *input_stream;
  parse_state->lr_stack = h_slist_new(arena);
  parse_state->recursion_heads = h_hashtable_new(arena, pos_equal, pos_hash);
  parse_state->arena = arena;
  parse_state->symbol_table = NULL;
  HParseResult *res = h_do_parse(parser, parse_state);
  h_slist_free(parse_state->lr_stack);
  h_hashtable_free(parse_state->recursion_heads);
  // tear down the parse state
  h_hashtable_free(parse_state->cache);
  if (!res)
    h_delete_arena(parse_state->arena);

  return res;
}

HParserBackendVTable h__packrat_backend_vtable = {
  .compile = h_packrat_compile,
  .parse = h_packrat_parse,
  .free = h_packrat_free,
  /* Name/param resolution functions */
  .backend_short_name = "packrat",
  .backend_description = "Packrat parser with Warth's recursion",
  .get_description_with_params = h_get_description_with_no_params,
  .get_short_name_with_params = h_get_short_name_with_no_params
};
