# Valgrind Leak Check Results Interpretation

**Date:** Generated from leak-check.sh run  
**Test Suite:** build/debug/src/build/debug/test_suite  
**Valgrind Version:** 3.22.0

## Executive Summary

⚠️ **CRITICAL ISSUES FOUND** - The test suite has significant memory management problems that need immediate attention.

### Overall Status: ❌ FAILING

- **438 errors** detected across **436 different code contexts**
- **137,056 bytes** of memory definitely or indirectly lost
- **Use-after-free** errors detected (critical)
- **Uninitialized value** usage detected

---

## Error Summary

```
ERROR SUMMARY: 438 errors from 436 contexts (suppressed: 0 from 0)
```

**Status:** ❌ **CRITICAL** - This is a very high number of errors indicating systematic memory management issues.

---

## Leak Summary

```
LEAK SUMMARY:
   definitely lost: 20,128 bytes in 429 blocks    ❌ CRITICAL
   indirectly lost: 116,928 bytes in 2,582 blocks  ❌ CRITICAL
   possibly lost: 168 bytes in 7 blocks            ⚠️  INVESTIGATE
   still reachable: 27,723 bytes in 100 blocks   ✅ ACCEPTABLE
   suppressed: 6,706 bytes in 6 blocks             ✅ EXPECTED (GLib)
```

### Analysis by Category

#### 1. Definitely Lost: 20,128 bytes in 429 blocks ❌ **CRITICAL**

**Severity:** HIGH - These are real memory leaks that will cause memory consumption to grow over time.

**Pattern Analysis:**
- Most leaks are small (averaging ~47 bytes per block)
- Many leaks originate from arena allocations in `allocator.c`
- Common sources:
  - `h_new_arena()` allocations not being freed
  - Parser result structures not being properly cleaned up
  - Test helper functions creating parsers that aren't freed

**Key Leak Locations:**
1. **Arena allocations** (`allocator.c:81` via `h_new_arena`)
   - Called from `h_packrat_parse()` in `packrat.c:310`
   - These arenas are created but not always freed when parsing fails or in error paths

2. **Parser allocations** in various test files
   - Many test functions create parsers but don't clean them up
   - Example: `test_result_length()` in `t_parser.c:743`

3. **Token allocations** (`token.c:79`)
   - Created via `h_token__m()` but not always freed

#### 2. Indirectly Lost: 116,928 bytes in 2,582 blocks ❌ **CRITICAL**

**Severity:** HIGH - These are cascading leaks caused by the "definitely lost" memory.

**Analysis:**
- When an arena or large structure is leaked, all memory it points to becomes "indirectly lost"
- The large size (116KB) suggests that entire data structures are being leaked
- This is typically fixed by fixing the "definitely lost" leaks

**Pattern:**
- Most indirectly lost memory comes from arena-allocated structures
- When `h_new_arena()` allocations leak, all arena-allocated memory becomes indirectly lost

#### 3. Possibly Lost: 168 bytes in 7 blocks ⚠️ **INVESTIGATE**

**Severity:** MEDIUM - May be false positives, but should be verified.

**Analysis:**
- Very small amount (168 bytes total)
- Could be intentional (e.g., caches) or real leaks
- Low priority compared to definitely/indirectly lost

#### 4. Still Reachable: 27,723 bytes in 100 blocks ✅ **ACCEPTABLE**

**Severity:** LOW - This is normal and acceptable.

**Analysis:**
- Includes GLib internal allocations (suppressed)
- Static buffers and caches that persist for program lifetime
- No action needed

---

## Error Types Detected

### 1. Use-After-Free Errors ❌ **CRITICAL**

**Count:** Multiple instances detected

**Example Error:**
```
Invalid read of size 8
   at 0x49CDCF0: unamb_sub (pprint.c:189)
   Address 0x61d8f00 is 2,320 bytes inside a block of size 4,120 free'd
   Block was free'd at:
      at 0x484988F: free
      by 0x49CE62C: system_free (system_allocator.c:76)
      by 0x49C3E94: h_delete_arena (allocator.c:248)
      by 0x49CCCE5: h_parse_result_free (hammer.c:565)
```

**Problem:**
- Memory is freed via `h_delete_arena()` in `allocator.c:248`
- But `h_write_result_unamb()` in `pprint.c:225` tries to read from it afterward
- This is a **use-after-free** bug that can cause crashes or undefined behavior

**Location:** `pprint.c:189` in `unamb_sub()` function

**Root Cause:**
- `h_parse_result_free()` frees the arena
- But `h_write_result_unamb()` is called after freeing, trying to access freed memory
- The parse result structure is being used after it's been freed

**Fix Required:**
- Ensure `h_write_result_unamb()` is called before `h_parse_result_free()`
- Or make a copy of needed data before freeing
- Review the lifetime management of parse results

### 2. Uninitialized Value Errors ⚠️ **MEDIUM**

**Count:** Multiple instances (at least 2 contexts)

**Example Error:**
```
Conditional jump or move depends on uninitialised value(s)
   at 0x49BF68B: h_drop_from___mv (sequence.c:222)
   Uninitialised value was created by a stack allocation
   at 0x49BF47F: h_drop_from___mv (sequence.c:198)
```

**Problem:**
- Variable is used before being initialized
- Stack-allocated variable in `sequence.c:198` is read at `sequence.c:222` without initialization

**Location:** `sequence.c:222` in `h_drop_from___mv()` function

**Fix Required:**
- Initialize the variable before use
- Check the logic flow to ensure all code paths initialize the variable

---

## Detailed Leak Analysis

### Top Leak Patterns

#### Pattern 1: Arena Leaks in Packrat Parser

**Location:** `packrat.c:310` → `allocator.c:81` (`h_new_arena`)

**Frequency:** Very common (many test cases)

**Stack Trace Pattern:**
```
h_new_arena (allocator.c:81)
  → h_packrat_parse (packrat.c:310)
    → h_parse__m (hammer.c:555)
      → h_parse (hammer.c:541)
        → [various test functions]
```

**Problem:**
- Arenas are created for parsing but not always freed
- Error paths may skip cleanup
- Test functions may not call cleanup functions

**Fix:**
- Ensure `h_delete_arena()` is called for all arenas
- Use arena cleanup in error paths
- Review test cleanup code

#### Pattern 2: Token Parser Leaks

**Location:** `token.c:79` (`h_token__m`)

**Frequency:** Moderate

**Stack Trace Pattern:**
```
h_token__m (token.c:79)
  → h_token (token.c:75)
    → [test functions]
```

**Problem:**
- Token parsers created but not freed
- Test code doesn't clean up parser structures

**Fix:**
- Ensure parsers are freed after use in tests
- Consider using parser cleanup helpers in tests

#### Pattern 3: Bits Parser Leaks

**Location:** `bits.c:89` (`h_bits__m`)

**Frequency:** Moderate

**Problem:**
- Similar to token leaks - parsers not freed

---

## Test Results

**Total Tests:** 440  
**Status:** All tests passed (but with memory errors)

The tests themselves pass, but they leak memory during execution. This means:
- The functionality works correctly
- But memory management is broken
- Long-running applications would eventually run out of memory

---

## Recommendations

### Priority 1: Critical Fixes (Immediate)

1. **Fix Use-After-Free in `pprint.c`**
   - **File:** `src/pprint.c:189` (`unamb_sub`)
   - **Issue:** Reading from freed memory
   - **Action:** Review `h_write_result_unamb()` and `h_parse_result_free()` call order
   - **Impact:** Can cause crashes or undefined behavior

2. **Fix Arena Leaks**
   - **Files:** `src/allocator.c`, `src/backends/packrat.c`
   - **Issue:** Arenas not freed in error paths or after use
   - **Action:** 
     - Add cleanup in all error paths
     - Ensure `h_delete_arena()` is called for every `h_new_arena()`
     - Review `h_packrat_parse()` cleanup logic

3. **Fix Uninitialized Value**
   - **File:** `src/parsers/sequence.c:222`
   - **Issue:** Variable used before initialization
   - **Action:** Initialize variable before use in `h_drop_from___mv()`

### Priority 2: Memory Leak Fixes (High)

1. **Add Parser Cleanup in Tests**
   - **Files:** Various test files
   - **Issue:** Parsers created but not freed
   - **Action:** 
     - Add cleanup calls after parser use
     - Consider helper macros/functions for test cleanup
     - Review all test functions for missing cleanup

2. **Review Parse Result Lifetime**
   - **Files:** `src/hammer.c`, `src/backends/packrat.c`
   - **Issue:** Parse results may be freed too early or not at all
   - **Action:** 
     - Document lifetime requirements
     - Ensure consistent cleanup patterns
     - Add assertions to catch use-after-free

### Priority 3: Code Quality (Medium)

1. **Add Memory Leak Tests**
   - Create specific tests that verify no leaks occur
   - Run leak-check.sh as part of CI/CD

2. **Improve Error Handling**
   - Ensure all error paths clean up resources
   - Use RAII-like patterns where possible

3. **Code Review**
   - Review all `h_new_arena()` calls
   - Review all `h_parse()` usage patterns
   - Review all test cleanup code

---

## Specific Files to Review

### Critical Files (Must Fix)

1. **`src/pprint.c`** - Use-after-free errors
2. **`src/parsers/sequence.c`** - Uninitialized value errors
3. **`src/allocator.c`** - Arena allocation/deallocation logic
4. **`src/backends/packrat.c`** - Parser cleanup logic

### Files with Leaks (Should Fix)

1. **`src/parsers/token.c`** - Token parser leaks
2. **`src/parsers/bits.c`** - Bits parser leaks
3. **Test files** - Missing cleanup in tests

---

## Next Steps

1. **Immediate:** Fix use-after-free in `pprint.c` (can cause crashes)
2. **Short-term:** Fix arena leaks in packrat parser
3. **Short-term:** Fix uninitialized value in `sequence.c`
4. **Medium-term:** Add cleanup to all test functions
5. **Long-term:** Add automated leak detection to CI/CD

---

## How to Verify Fixes

After making fixes, run:

```bash
./tools/leak-check.sh > leak-report-new.txt 2>&1
```

Then check:
- `ERROR SUMMARY: 0 errors` ✅
- `definitely lost: 0 bytes` ✅
- `indirectly lost: 0 bytes` ✅

Compare with this report to verify improvements.

---

## Notes

- The "still reachable" memory (27KB) is acceptable and mostly from GLib
- Some leaks may be intentional (e.g., static caches) but should be documented
- The high number of errors suggests systematic issues rather than isolated bugs
- Consider using a memory pool or arena allocator pattern more consistently

---

**Generated:** From valgrind output analysis  
**Report Status:** Issues identified, fixes recommended
