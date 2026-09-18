#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#define my_isblank( a ) (isspace(a) || ((a) == '\t'))

#include "ncview/stringlist.h"

static int stringlist_write_single_element_to_file( const StringlistEntry &sl, FILE *fout );
static std::string stringlist_escape_string( const std::string &ss );
static std::string stringlist_unescape_string( const std::string &ss );
static int stringlist_check_args( const char *new_string, const StringlistAux &aux );
static int stringlist_get_tok_indices( char *line, int *index0, int *index1, int *string0, int *string1,
		int *aux_type0, int *aux_type1, int *aux_val0, int *aux_val1 );
static int stringlist_line_to_sl( char *line, int lineno, Stringlist **sl );
static int stringlist_read_from_file_v1( Stringlist **sl, FILE *fin, int nlines_in_header, int debug );

/**************************************************************************************
 * Given a stringlist and a string, this returns a pointer to the element on the
 * stringlist that matches the string, or NULL if there is no match.
 */
	StringlistEntry *
stringlist_match_string_exact( Stringlist *list, const char *str )
{
	if( (list == nullptr) || (str == nullptr) )
		return( nullptr );

	for( auto &e : *list )
		if( e.string == str )
			return( &e );

	return( nullptr );
}

/**************************************************************************************
 * Adds the given string and auxiliary data to the list.
 * The first time this is called, assuming you want to make a new stringlist, pass
 * with *list == NULL. This will make a new stringlist with the passed string (and
 * aux info) as the first element of the new string.
 * Returns 0 on success, -1 on error (usually inability to allocate memory)
 */
	int
stringlist_add_string( Stringlist **list, const char *new_string, const StringlistAux &aux )
{
	int	err;

	if( list == nullptr ) {
		fprintf( stderr, "stringlist: error, passed a null reference to a stringlist\n" );
		return( -5 );
		}

	if( (err = stringlist_check_args( new_string, aux )) != 0 ) {
		fprintf( stderr, "stringlist_add_string: error, bad arguments passed\n" );
		return( err );
		}

	if( *list == nullptr )
		*list = new Stringlist();

	StringlistEntry entry;
	entry.string = new_string;
	entry.index  = (int)(*list)->size();
	entry.aux    = aux;
	(*list)->push_back( std::move(entry) );

	return( 0 );
}

/*******************************************************************************
 * Concatenate one stringlist onto the end of another stringlist. I.e., if
 * dest is a stringlist, and src is a stringlist, this returns (dest, src)
 * This COPIES data to a new entry on the dest list, so if you are done with
 * the src list, it is OK to delete it (and should be deleted).
 *
 * Returns 0 on success, -1 on error (usually inability to allocate memory)
 */
	int
stringlist_cat( Stringlist **dest, Stringlist **src )
{
	if( dest == nullptr ) {
		fprintf( stderr, "stringlist_cat: Error, null reference to a destination stringlist passed!\n" );
		return( -6 );
		}
	if( src == nullptr ) {
		fprintf( stderr, "stringlist_cat: Error, null reference to a source stringlist passed!\n" );
		return( -7 );
		}

	if( *src == nullptr )
		return(0);	/* Nothing to do */

	for( auto &e : **src ) {
		int	err;
		if( (err = stringlist_add_string( dest, e.string.c_str(), e.aux )) < 0 )
			return( err );
		}

	return(0);
}

/******************************************************************************
 * What's in this stringlist, anyway?
 * was: dump_stringlist
 */
	void
stringlist_dump( Stringlist *s )
{
	if( (s == nullptr) || s->empty() ) {
		printf( "<--- null pointer --->\n" );
		return;
		}

	for( auto &e : *s ) {
		printf( "ADDR=%p ", (void*)&e );
		printf( "INDEX=%d: ", e.index );
		printf( "\"%s\" ", e.string.c_str() );
		switch( e.aux.index() ) {
			case SLTYPE_NULL:
				printf( "NULL --\n" );
				break;
			case SLTYPE_INT:
				printf( "INT %d\n", std::get<int>(e.aux) );
				break;
			case SLTYPE_STRING:
				printf( "STRING \"%s\"\n", std::get<std::string>(e.aux).c_str() );
				break;
			case SLTYPE_FLOAT:
				printf( "FLOAT %f\n", std::get<float>(e.aux) );
				break;
			case SLTYPE_BOOL:
				if( std::get<bool>(e.aux) )
					printf( "BOOL TRUE\n" );
				else
					printf( "BOOL FALSE\n" );
				break;
			}
		}
}

/******************************************************************************
 * Returns the number of entries in the Stringlist
 * was: n_strings_in_list
 */
	int
stringlist_len( Stringlist *s )
{
	if( s == nullptr )
		return( 0 );
	return( (int)s->size() );
}

/*******************************************************************************************
 * Checks args for correctness. Returns 0 on success (no error), -1 if there is a problem.
 */
	static int
stringlist_check_args( const char *new_string, const StringlistAux &aux )
{
	if( new_string == nullptr ) {
		fprintf( stderr, "stringlist: NULL pointer to character string passed\n" );
		return(-1);
		}

	if( strlen(new_string) > STRINGLIST_MAX_LEN ) {
		fprintf( stderr, "stringlist: error, trying to add string that is too long to a stringlist.\n" );
		fprintf( stderr, "String trying to add:\n" );
		fprintf( stderr, "\"%s\"\n", new_string );
		return(-1);
		}

	if( std::holds_alternative<std::string>(aux) && (std::get<std::string>(aux).size() > STRINGLIST_MAX_LEN) ) {
		fprintf( stderr, "stringlist_check_args: error, trying to add auxiliary string data to a stringlist element and that string is longer than allowed max of %d\n",
			STRINGLIST_MAX_LEN );
		return( -68 );
		}

	return(0);
}

/**************************************************************************************
 * Returns 0 on success, -1 on error
 * Note this allocates all space needed for the stringlist, it must not be pre-allocated
 *
 * Structure of input file:
 *
 * integer (index #)          "string"         aux_type    aux_value
 *
 * where aux_value will be in "quotes" if aux_type == STRING
 */
 	int
 stringlist_read_from_file( Stringlist **sl, FILE *fin )
 {
 	char		line[4001];
	int		err, version_number, lineno, debug;
	Stringlist 	*header_el = nullptr;

	/* Check for obvious errors */
	if( sl == nullptr ) {
		fprintf( stderr, "stringlist_read_from_file: error, passed a NULL pointer\n" );
		exit(-1);
		}
	if( *sl != nullptr ) {
		fprintf( stderr, "stringlist_read_from_file: error, bad coding, should be passed a pointer to a NULL list\n" );
		exit(-1);
		}

	debug = 0;

	if( debug ) printf( "stringlist_read_from_file: entering\n" );

	/* Advance to our header: it should read something like this:
	 * -1 "STRINGLIST_SAVE_FILE_VERSION" INT 1
	 */
	lineno = 0;
	version_number = -1;
	while( fgets( line, 4000, fin ) != NULL ) {

		if( debug ) printf( "stringlist_read_from_file: line %d: >%s<\n", lineno+1, line );

		if( strncmp( line, "-1 \"STRINGLIST_SAVE_FILE_VERSION\" INT", 36 ) == 0 ) {

			/* found the header */
			if( debug ) printf( "stringlist_read_from_file: found header line\n" );

			/* chomp a trailing newline, if there is one -- a valid
			 * final line with no trailing newline must not lose
			 * its last character. Handles CRLF too. */
			{
			size_t len = strlen(line);
			if( len > 0 && line[len-1] == '\n' ) { line[--len] = '\0'; }
			if( len > 0 && line[len-1] == '\r' ) { line[--len] = '\0'; }
			}

			if( (err = stringlist_line_to_sl( line, lineno, &header_el )) != 0 ) {
				fprintf( stderr, "stringlist_read_from_file: error reading header line from file!\n" );
				return( err );
				}
			/* header_el is a local, one-line-only temporary -- own it here
			 * so every return path below frees it, instead of only the
			 * dispatch-to-v1 path a bare `delete` after the version check
			 * would cover (the "not an int" branch right below used to
			 * leak it). */
			std::unique_ptr<Stringlist> header_owner( header_el );

			if( !std::holds_alternative<int>((*header_el)[0].aux) ) {
				fprintf( stderr, "stringlist_read_from_file: error reading save file version number from file\n" );
				return( -53 );
				}

			version_number = std::get<int>((*header_el)[0].aux);
			if( debug ) printf( "stringlist_read_from_file: save file version number %d\n", version_number );
			if( version_number == 1 )
				return( stringlist_read_from_file_v1( sl, fin, lineno, debug ));
			else
				{
				fprintf( stderr, "This version of the code cannot read save file version number %d\n", version_number );
				return( -53 );
				}
			}
		}

	return(0);
 }

/**************************************************************************************
 * Reads a version 1 stringlist save file
 */
 	static int
 stringlist_read_from_file_v1( Stringlist **sl, FILE *fin, int nlines_in_header, int debug )
 {
	char		line[4001];
	int		err, lineno;

	if( debug ) printf( "stringlist_read_from_file_v1: entering\n" );

	lineno = 0;
	while( fgets( line, 4000, fin ) != NULL ) {

		if( debug ) printf( "stringlist_read_from_file_v1: line %d: >%s<\n", lineno+nlines_in_header, line );

		if( (strlen(line)>2) && (line[0] != '\0') && (line[0] != '#' )) {

			/* chomp a trailing newline, if there is one -- a valid
			 * final line with no trailing newline must not lose
			 * its last character. Handles CRLF too. */
			{
			size_t len = strlen(line);
			if( len > 0 && line[len-1] == '\n' ) { line[--len] = '\0'; }
			if( len > 0 && line[len-1] == '\r' ) { line[--len] = '\0'; }
			}

			if( (err = stringlist_line_to_sl( line, lineno+nlines_in_header, sl )) != 0 )
				return( err );
			}
		lineno++;
		}

	return(0);
}

/**************************************************************************************
 * Given a line of chars in savelist save file format, this parses out the index/
 * string/aux_type/aux_value tokens and appends one new entry to *retval (allocating
 * *retval on first use, exactly like stringlist_add_string).
 * Returns 0 on success, < 0 on error
 */
	static int
stringlist_line_to_sl( char *line, int lineno, Stringlist **retval )
{
	int	ival, debug, err, index0, index1, string0, string1, aux_type0, aux_type1, aux_val0, aux_val1,
		nfields;
	char	t_index[4000], t_string[4000], t_aux_type[4000], t_aux_val[4000];
	float	fval;

	debug = 0;

	/* Get indices (into the string char array) of the
	 * first and last positions of each of the four
	 * elements (index_num, string, aux_type, and aux_value)
	 */
	err = stringlist_get_tok_indices( line, &index0, &index1,
			&string0,   &string1,
			&aux_type0, &aux_type1,
			&aux_val0,  &aux_val1 );
	if( err != 0 ) {
		fprintf( stderr, "stringlist_read_from_file: error, could not parse information on input file line %d\n", lineno+1 );
		fprintf( stderr, "here was the problematical line:\n" );
		fprintf( stderr, ">%s<\n", line );
		return( err );
		}
	strncpy( t_index,    line+index0,    index1-index0+1 );
	t_index[index1-index0+1] = '\0';

	strncpy( t_string,   line+string0,   string1-string0+1 );
	t_string[string1-string0+1] = '\0';
	if( strlen(t_string) > STRINGLIST_MAX_LEN ) {
		fprintf( stderr, "stringlist_line_to_sl: error, encountered a string that is too long. Max allowed: %d. Found: %zu. Line number: %d\n",
			STRINGLIST_MAX_LEN, strlen(t_string), lineno );
		return( -63 );
		}
	std::string t_string_ue = stringlist_unescape_string( t_string );

	strncpy( t_aux_type, line+aux_type0, aux_type1-aux_type0+1 );
	t_aux_type[aux_type1-aux_type0+1] = '\0';

	strncpy( t_aux_val,  line+aux_val0,  aux_val1-aux_val0+1 );
	t_aux_val[aux_val1-aux_val0+1] = '\0';

	if( debug ) {
		printf( "line: >%s<\n", line );
		printf( "index:>%s< string:>%s< aux_type:>%s< aux_val:>%s<\n",
			t_index, t_string_ue.c_str(), t_aux_type, t_aux_val );
		}

	/* Now put this information into a new stringlist element */
	if( strcmp( t_aux_type, "NULL" ) == 0 ) {
		if( (err = stringlist_add_string( retval, t_string_ue.c_str() )) != 0 ) {
			fprintf( stderr, "stringlist_read_from_file: error encountered while processing line %d\n", lineno+1 );
			return( err );
			}
		}

	else if( strcmp( t_aux_type, "BOOL" ) == 0 ) {
		if( strcmp( t_aux_val, "TRUE" ) == 0 ) {
			if( (err = stringlist_add_string( retval, t_string_ue.c_str(), true )) != 0 ) {
				fprintf( stderr, "stringlist_read_from_file: error encountered while processing line %d\n", lineno+1 );
				return( err );
				}
			}

		else if( strcmp( t_aux_val, "FALSE" ) == 0 ) {
			if( (err = stringlist_add_string( retval, t_string_ue.c_str(), false )) != 0 ) {
				fprintf( stderr, "stringlist_read_from_file: error encountered while processing line %d\n", lineno+1 );
				return( err );
				}
			}
		else
			{
			fprintf( stderr, "Error, got a boolean type that is neither TRUE nor FALSE, it is: %s\n",
				t_aux_val );
			fprintf( stderr, "stringlist_read_from_file: error encountered while processing line %d\n", lineno+1 );
			return( -38 );
			}
		}

	else if( strcmp( t_aux_type, "INT" ) == 0 ) {
		nfields = sscanf( t_aux_val, "%d", &ival );
		if( nfields != 1 ) {
			fprintf( stderr, "stringlist_read_from_file: error, while reading line %d could not get a single integer from token >%s<\n",
				lineno+1, t_aux_val );
			return( -30 );
			}
		if( (err = stringlist_add_string( retval, t_string_ue.c_str(), ival )) != 0 ) {
			fprintf( stderr, "stringlist_read_from_file: error encountered while processing line %d\n", lineno+1 );
			return( err );
			}
		}

	else if( strcmp( t_aux_type, "FLOAT" ) == 0 ) {
		nfields = sscanf( t_aux_val, "%f", &fval );
		if( nfields != 1 ) {
			fprintf( stderr, "stringlist_read_from_file: error, while reading line %d could not get a single float from token >%s<\n",
				lineno+1, t_aux_val );
			return( -31 );
			}
		if( (err = stringlist_add_string( retval, t_string_ue.c_str(), fval )) != 0 ) {
			fprintf( stderr, "stringlist_read_from_file: error encountered while processing line %d\n", lineno+1 );
			return( err );
			}
		}

	else if( strcmp( t_aux_type, "STRING" ) == 0 ) {
		std::string t_aux_val_ue = stringlist_unescape_string( t_aux_val );
		if( t_aux_val_ue.size() > STRINGLIST_MAX_LEN ) {
			fprintf( stderr, "stringlist_line_to_sl: error, encountered a string that is too long. Max allowed: %d. Found: %zu. Line number: %d\n",
				STRINGLIST_MAX_LEN, t_aux_val_ue.size(), lineno );
			return( -67 );
			}
		if( (err = stringlist_add_string( retval, t_string_ue.c_str(), t_aux_val_ue )) != 0 ) {
			fprintf( stderr, "stringlist_read_from_file: error encountered while processing line %d\n", lineno+1 );
			return( err );
			}
		}

	else
		{
		fprintf( stderr, "stringlist_read_from_file: encountered unhandled type: %s\n",
			t_aux_type );
		return( -37 );
		}

	return(0);
}

/**************************************************************************************
 */
/* A quote at line[cursor] is escaped iff it's preceded by an ODD number of
 * consecutive backslashes (each pair of backslashes is itself an escaped
 * backslash, not an escape of the quote) -- checking only line[cursor-1]
 * (as this used to) gets a string ending in an even run of backslashes
 * followed by a quote wrong, e.g. \\" is an escaped backslash then a real
 * closing quote, not an escaped quote. */
	static bool
stringlist_quote_is_escaped( const char *line, int cursor )
{
	int n_backslashes = 0;
	while( cursor - 1 - n_backslashes >= 0 && line[cursor - 1 - n_backslashes] == '\\' )
		n_backslashes++;
	return( (n_backslashes % 2) == 1 );
}

	static int
stringlist_get_tok_indices( char *line, int *index0, int *index1, int *string0, int *string1,
		int *aux_type0, int *aux_type1, int *aux_val0, int *aux_val1 )
{
	int	cursor, slen, aux_val_is_string;

	if( line == NULL )
		return( -1 );

	cursor = 0;
	slen   = strlen( line );

	/* ========= GET INDEX ======= */
	/* Advance past initial whitespace */
	while( (cursor < slen) && my_isblank( line[cursor] ))
		cursor++;
	/* cursor is now NOT white space */
	if( cursor == slen ) {
		fprintf( stderr, "stringlist_get_tok_indices: error reading from file, is input line all blank?\n" );
		return(-1);	/* line is all blank? */
		}
	*index0 = cursor;

	/* Advance to next white space */
	while( (cursor < slen) && (! my_isblank(line[cursor])))
		cursor++;
	/* cursor is now white space */
	if( cursor == slen ) {
		fprintf( stderr, "stringlist_get_tok_indices: error reading from file, does line end immeidiately after the index number?\n" );
		return(-1);
		}
	*index1 = cursor-1;

	/* ========= GET STRING ======= */
	/* Advance past initial whitespace */
	while( (cursor < slen) && my_isblank( line[cursor] ))
		cursor++;
	if( cursor == slen ) {
		fprintf( stderr, "stringlist_get_tok_indices: error reading from file, does line have ONLY the index number, no string?\n" );
		return(-1);
		}
	/* cursor is now NOT white space, needs to be a quote sign (") */
	if( line[cursor] != '"' ) {
		fprintf( stderr, "stringlist_get_tok_indices: error reading from file, does string NOT start with a quote sign?\n" );
		return(-1);
		}
	cursor++;	/* advance past quote sign */
	*string0 = cursor;

	/* Advance to next quote that is NOT escaped by a backslash */
	/* an quote that is NOT escaped is this: ((line[cursor] == '"') && (line[cursor-1] != '\')) */
	/* "while line[cursor] is NOT a (quote that is NOT escaped by a backslash)" */
	while( (cursor < slen) && (! ((line[cursor] == '"') && !stringlist_quote_is_escaped(line, cursor))) )
		cursor++;
	if( cursor == slen ) {
		fprintf( stderr, "stringlist_get_tok_indices: error reading from file, does string not end with a quote sign?\n" );
		return(-1);
		}
	/* cursor is now on an NOT escaped quote */
	*string1 = cursor - 1;
	cursor++;	/* advance past the NON escaped quote, should be whitespace */

	/* ========= GET AUX TYPE ======= */
	/* Advance past initial whitespace */
	while( (cursor < slen) && my_isblank( line[cursor] ))
		cursor++;
	/* cursor is now NOT white space */
	if( cursor == slen ) {
		fprintf( stderr, "stringlist_get_tok_indices: error reading from file, does line end before specifying an aux_type?\n" );
		return(-1);
		}
	*aux_type0 = cursor;

	/* Advance to next white space */
	while( (cursor < slen) && (! my_isblank(line[cursor])))
		cursor++;
	/* cursor is now white space */
	if( cursor == slen ) {
		fprintf( stderr, "stringlist_get_tok_indices: error reading from file, does line end immediately after the aux_type?\n" );
		return(-1);
		}
	*aux_type1 = cursor-1;

	/* ========== GET AUX VAL ========
	 * If it starts with a quote, we assume it's a string
	 * and go to the matching unescaped quote.
	 * Otherwise, just go to first whitespace or we
	 * run out of string.
	 */
	/* Advance past initial whitespace */
	while( (cursor < slen) && my_isblank( line[cursor] ))
		cursor++;
	if( cursor == slen ) {
		fprintf( stderr, "stringlist_get_tok_indices: error reading from file, does line end before an aux_val is specified?\n" );
		return(-1);
		}
	/* cursor is now NOT white space */
	aux_val_is_string = (line[cursor] == '"');

	if( aux_val_is_string ) {
		cursor++;	/* Advance past the opening quote */
		*aux_val0 = cursor;
		/* Advance to next quote that is NOT escaped by a backslash; see above */
		while( (cursor < slen) && (! ((line[cursor] == '"') && !stringlist_quote_is_escaped(line, cursor))) )
			cursor++;
		if( cursor == slen ) {
			fprintf( stderr, "stringlist_get_tok_indices: error reading from file, does the string-valued aux_val NOT end with a quote?\n" );
			return(-1);
			}
		/* cursor is now on an NOT escaped quote */
		*aux_val1 = cursor - 1;
		}
	else
		{
		*aux_val0 = cursor;
		while( (cursor < slen) && (! my_isblank(line[cursor])))
			cursor++;
		/* cursor is now white space */
		*aux_val1 = cursor-1;
		}

	return(0);
}

/**************************************************************************************
 * Returns 0 on success, -1 on error (usually inability to allocate memory)
 */
 	int
 stringlist_write_to_file( Stringlist *sl, FILE *fout )
 {
	int	err;

	/* Note that if we are passed a NULL, we do not even write a header */
 	if( (sl == nullptr) || sl->empty() )
		return(0);

	/* Stringlists are supposed to be small amounts of data. I add this check
	 * as a security issue ... I do not want stringlists to be able to be
	 * tricked into filling up a disk or something like that.
	 */
	if( stringlist_len( sl ) > STRINGLIST_MAX_NSTRINGS_WRITE ) {
		fprintf( stderr, "stringlist_write_to_file: error, can only write a maximum of %d strings, but passed list had %d\n",
			STRINGLIST_MAX_NSTRINGS_WRITE, stringlist_len( sl ) );
		return( -61 );
		}

	/* We add, as a header, the version of this save file. Index -1, exactly
	 * as the original hand-built header element did (it was never routed
	 * through stringlist_add_string's position-based indexing). */
	StringlistEntry header;
	header.string = "STRINGLIST_SAVE_FILE_VERSION";
	header.index  = -1;
	header.aux    = (int)STRINGLIST_SAVEFILE_VERSION;

	/* Write header to file */
	if( (err = stringlist_write_single_element_to_file( header, fout )) != 0 )
		return( err );

	/* Write stringlist contents to file */
	for( auto &e : *sl ) {
		if( e.string.size() > STRINGLIST_MAX_LEN ) {
			fprintf( stderr, "Error, trying to write an illegally long string to the file. Offending string: %s\n",
				e.string.c_str() );
			return( -69 );
			}
		if( std::holds_alternative<std::string>(e.aux) && (std::get<std::string>(e.aux).size() > STRINGLIST_MAX_LEN) ) {
			fprintf( stderr, "Error, trying to write an illegally long aux information string to the file. Offending aux info string: %s\n",
				std::get<std::string>(e.aux).c_str() );
			return( -70 );
			}
		if( (err = stringlist_write_single_element_to_file( e, fout )) != 0 )
			return( err );
		}

	return( 0 );
}

/**************************************************************************************
 * Returns 0 on success, -1 on error
 */
	static int
stringlist_write_single_element_to_file( const StringlistEntry &sl, FILE *fout )
{
	std::string string_esc = stringlist_escape_string( sl.string );
	fprintf( fout, "%d \"%s\"", sl.index, string_esc.c_str() );

	switch( sl.aux.index() ) {
		case SLTYPE_NULL:
			fprintf( fout, " NULL NULL\n" );
			break;

		case SLTYPE_INT:
			fprintf( fout, " INT %d\n", std::get<int>(sl.aux) );
			break;

		case SLTYPE_FLOAT:
			fprintf( fout, " FLOAT %f\n", std::get<float>(sl.aux) );
			break;

		case SLTYPE_BOOL:
			if( std::get<bool>(sl.aux) )
				fprintf( fout, " BOOL TRUE\n" );
			else
				fprintf( fout, " BOOL FALSE\n" );
			break;

		case SLTYPE_STRING: {
			std::string aux_esc = stringlist_escape_string( std::get<std::string>(sl.aux) );
			fprintf( fout, " STRING \"%s\"\n", aux_esc.c_str() );
			break;
			}

		default:
			fprintf( stderr, "stringlist_write_to_file: error, encountered unknown aux data type %zu\n", sl.aux.index() );
			return( -30 );
		}

	return(0);
}

/**************************************************************************************
 * Deletes *sl (a no-op if it's nullptr). Kept as a function (rather than
 * having every caller just `delete list`) for source compatibility with
 * the many existing call sites, and because it still returns the same
 * int-error-code shape they check.
 */
	int
stringlist_delete_entire_list( Stringlist *sl )
{
	delete sl;
	return( 0 );
}

/**************************************************************************************
 * Creates an "UNescaped" version of a string, that is, one with backslashes before
 * all quote or backslashes removed.
 */
	static std::string
stringlist_unescape_string( const std::string &ss )
{
	std::string retval;
	retval.reserve( ss.size() );

	for( size_t ii = 0; ii < ss.size(); ii++ ) {
		if( ss[ii] == '\\' )
			ii++;
		if( ii < ss.size() )
			retval.push_back( ss[ii] );
		}

	return( retval );
}

/**************************************************************************************
 * Creates an "escaped" version of a string, that is, one with backslashes before
 * all quote or backslashes.
 */
	static std::string
stringlist_escape_string( const std::string &ss )
{
	std::string retval;
	retval.reserve( 2 * ss.size() );

	for( char c : ss ) {
		if( (c == '"') || (c == '\\') )
			retval.push_back( '\\' );
		retval.push_back( c );
		}

	return( retval );
}
