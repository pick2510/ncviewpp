#ifndef SEEN_STRINGLIST_H
#define SEEN_STRINGLIST_H

#include <cstdio>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/*-------------------------------------------
 * How long the string in a stringlist can be
 *-------------------------------------------*/
constexpr int STRINGLIST_MAX_LEN = 1000;

/*-------------------------------------------------------------------
 * When writing to a file, we will never write more strings than this
 *-------------------------------------------------------------------*/
constexpr int STRINGLIST_MAX_NSTRINGS_WRITE = 10000;

/*-----------------------------------------------------------------------
 * Types that the aux data in a stringlist can be -- kept as named
 * constants purely so call sites can still write SLTYPE_INT/SLTYPE_STRING/
 * etc. rather than spelling out a std::variant index; the type actually
 * carried is StringlistAux below, which is self-describing.
 *-----------------------------------------------------------------------*/
constexpr int SLTYPE_NULL   = 0;
constexpr int SLTYPE_INT    = 1;
constexpr int SLTYPE_STRING = 2;
constexpr int SLTYPE_FLOAT  = 3;
constexpr int SLTYPE_BOOL   = 4;

/* Version number for the save file */
constexpr int STRINGLIST_SAVEFILE_VERSION = 1;

/* The aux payload a stringlist entry can carry. Index 0 (std::monostate)
 * is SLTYPE_NULL -- "no aux data" -- and the variant's own index() is
 * exactly the old sltype tag, so there is no separate tag field to keep in
 * sync (see modernization.md Phase 3). */
using StringlistAux = std::variant<std::monostate, int, std::string, float, bool>;

/*****************************************************************************/
/* A general purpose list of character strings. Was an intrusive doubly-
 * linked list of individually malloc()'d nodes (see modernization.md Phase
 * 3); is now a plain std::vector, so most call sites still hold a
 * `Stringlist *` exactly as before (nullptr means "no list yet", matching
 * the old convention), but the *list itself is one contiguous allocation
 * with no per-node bookkeeping, no manual free(), and no magic-number
 * use-after-free detection (SL_MAGIC/SL_BAD_MAGIC/stringlist_check_args
 * are gone -- a std::vector can't be used after it's freed by accident the
 * way a dangling node pointer could).
 */
struct StringlistEntry {
	std::string string;
	int index = 0;		/* position in the list when added */
	StringlistAux aux;	/* auxiliary data; monostate == SLTYPE_NULL */
};

using Stringlist = std::vector<StringlistEntry>;

/* Appends new_string (+ optional aux) to *list, allocating *list on first
 * use if it's still nullptr. Returns 0 on success, -1 on error (usually an
 * illegally long string). */
int 	stringlist_add_string( Stringlist **list, const char *new_string, const StringlistAux &aux = std::monostate{} );

/* Appends a copy of every entry in *src onto the end of *dest (re-indexed
 * by dest's own position, exactly as stringlist_add_string does). *src is
 * left unmodified. Returns 0 on success. */
int 	stringlist_cat( Stringlist **dest, Stringlist **src );

/* Returns 0 on success, -1 on error (usually inability to allocate memory) */
int 	stringlist_write_to_file( Stringlist *sl, FILE *fout );

/* Returns 0 on success, -1 on error (usually inability to allocate memory) */
int 	stringlist_read_from_file( Stringlist **sl, FILE *fin );

/* Return nullptr if the string is not found, and a pointer to the matching
 * entry if it is found. The pointer is invalidated by any subsequent
 * mutation of *list (vector reallocation), exactly as a plain
 * `&(*list)[i]` would be -- use it before appending anything else. */
StringlistEntry *stringlist_match_string_exact( Stringlist *list, const char *str );

/* Frees *sl (a no-op if it's nullptr). Returns 0 always (kept int for
 * source compatibility with existing call sites' `if (err = ...)` style). */
int 	stringlist_delete_entire_list( Stringlist *sl );


void 	stringlist_dump( Stringlist *s );
int 	stringlist_len( Stringlist *s );

#endif
