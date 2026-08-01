static bool LangDict_BuildProfanityFilteredString( const char *input, const char *replacement, bool censorProfanity, idStr &out ) {
	static const char *openTag = "<PROFANITY>";
	static const char *closeTag = "</PROFANITY>";
	static const int openTagLen = 11;
	static const int closeTagLen = 12;

	const char *open = strstr( input, openTag );
	if ( open == NULL ) {
		return false;
	}

	out.Empty();
	const char *cursor = input;
	while ( open != NULL ) {
		out.Append( cursor, static_cast<int>( open - cursor ) );
		const char *profanityStart = open + openTagLen;
		const char *close = strstr( profanityStart, closeTag );
		if ( close == NULL ) {
			// Preserve malformed trailing content instead of silently deleting it.
			out.Append( open );
			return true;
		}

		if ( censorProfanity ) {
			out.Append( replacement );
		} else {
			out.Append( profanityStart, static_cast<int>( close - profanityStart ) );
		}

		cursor = close + closeTagLen;
		open = strstr( cursor, openTag );
	}

	out.Append( cursor );
	return true;
}

/*
============
idLangDict::idLangDict
============
*/
idLangDict::idLangDict( void ) {
	args.SetGranularity( 256 );
	hash.SetGranularity( 256 );
	hash.Clear( 4096, 8192 );
	baseID = 0;
	profanityScratchIndex = 0;
}

/*
============
idLangDict::~idLangDict
============
*/
idLangDict::~idLangDict( void ) {
	Clear();
}

/*
============
idLangDict::Clear
============
*/
void idLangDict::Clear( void ) {
	args.Clear();
	hash.Clear();
	profanityScratchIndex = 0;
	for ( int i = 0; i < 4; ++i ) {
		profanityScratch[i].Clear();
	}
}

/*
============
LANGDICT_CP1252_HIGH

Windows-1252 0x80-0x9F block - the only range where Windows-1252 diverges from
Latin-1. 0 marks the five unassigned CP1252 slots (0x81/0x8D/0x8F/0x90/0x9D).
============
*/
static const unsigned short LANGDICT_CP1252_HIGH[32] = {
	0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
	0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178
};

/*
============
LANGDICT_GLYPH_FOLD

The stock Quake 4 .fontdat atlases hold 256 byte-indexed glyphs, of which only
83 of the 128 high slots carry real art, and the missing set is identical in
every shipped Latin font. These CP1252 codes land on a 2x2 .notdef cell with
zero horizontal advance, so a non-breaking space would not merely draw nothing,
it would delete the word gap. Fold them to ASCII equivalents that do have art.
============
*/
typedef struct langDictGlyphFold_s {
	unsigned char	cp1252;
	const char *	ascii;
} langDictGlyphFold_t;

static const langDictGlyphFold_t LANGDICT_GLYPH_FOLD[] = {
	{ 0x82, "," },  { 0x84, "\"" }, { 0x85, "..." },
	{ 0x91, "'" },  { 0x92, "'" },  { 0x93, "\"" }, { 0x94, "\"" },
	{ 0x96, "-" },  { 0x97, "-" },  { 0xA0, " " },
	{ 0x00, NULL }
};

static const char *LangDict_FoldForCp1252( unsigned char c ) {
	for ( int i = 0; LANGDICT_GLYPH_FOLD[i].ascii != NULL; i++ ) {
		if ( LANGDICT_GLYPH_FOLD[i].cp1252 == c ) {
			return LANGDICT_GLYPH_FOLD[i].ascii;
		}
	}
	return NULL;
}

/*
============
LangDict_DecodeUtf8

Returns the number of bytes consumed, or 0 if the sequence is not well-formed
shortest-form UTF-8.
============
*/
static int LangDict_DecodeUtf8( const unsigned char *p, int available, unsigned int &codePoint ) {
	if ( available <= 0 ) {
		return 0;
	}

	const unsigned char c0 = p[0];
	if ( c0 < 0x80 ) {
		codePoint = c0;
		return 1;
	}

	int extra;
	unsigned int cp;
	unsigned int minimum;
	if ( ( c0 & 0xE0 ) == 0xC0 ) {
		extra = 1; cp = c0 & 0x1Fu; minimum = 0x80;
	} else if ( ( c0 & 0xF0 ) == 0xE0 ) {
		extra = 2; cp = c0 & 0x0Fu; minimum = 0x800;
	} else if ( ( c0 & 0xF8 ) == 0xF0 ) {
		extra = 3; cp = c0 & 0x07u; minimum = 0x10000;
	} else {
		return 0;
	}

	if ( available < extra + 1 ) {
		return 0;
	}
	for ( int i = 1; i <= extra; i++ ) {
		if ( ( p[i] & 0xC0 ) != 0x80 ) {
			return 0;
		}
		cp = ( cp << 6 ) | ( p[i] & 0x3Fu );
	}
	if ( cp < minimum || cp > 0x10FFFF || ( cp >= 0xD800 && cp <= 0xDFFF ) ) {
		return 0;
	}

	codePoint = cp;
	return extra + 1;
}

static bool LangDict_CodePointToCp1252( unsigned int codePoint, unsigned char &out ) {
	if ( codePoint < 0x80 || ( codePoint >= 0xA0 && codePoint <= 0xFF ) ) {
		out = (unsigned char)codePoint;
		return true;
	}
	for ( int i = 0; i < 32; i++ ) {
		if ( LANGDICT_CP1252_HIGH[i] != 0 && LANGDICT_CP1252_HIGH[i] == codePoint ) {
			out = (unsigned char)( 0x80 + i );
			return true;
		}
	}
	return false;
}

/*
============
LangDict_ConvertUtf8ToCp1252

Stock Quake 4 string tables are Windows-1252 and the fonts are 256 byte-indexed
glyphs, so a UTF-8 table draws two wrong glyphs per accent - "MENUS" comes out
as "MENA S". Convert only when the WHOLE buffer is well-formed UTF-8, every
multi-byte code point round-trips to CP1252, and at least one multi-byte
sequence is present. A genuine CP1252 table with accents is never valid UTF-8
end to end (0xE9 followed by an ASCII letter fails the continuation-byte test),
so CP1252 and pure-ASCII inputs are left byte-identical.
============
*/
static bool LangDict_ConvertUtf8ToCp1252( const char *buffer, int length, idStr &converted ) {
	const unsigned char *p = (const unsigned char *)buffer;
	bool sawMultiByte = false;

	for ( int i = 0; i < length; ) {
		unsigned int cp = 0;
		const int used = LangDict_DecodeUtf8( p + i, length - i, cp );
		if ( used == 0 ) {
			return false;
		}
		if ( used > 1 ) {
			unsigned char mapped;
			if ( !LangDict_CodePointToCp1252( cp, mapped ) ) {
				return false;
			}
			sawMultiByte = true;
		}
		i += used;
	}
	if ( !sawMultiByte ) {
		return false;
	}

	// The output can never be longer than the input: every multi-byte sequence
	// collapses to a single byte, and the one expanding fold (U+2026 -> "...")
	// is three bytes in and three bytes out.
	converted.Fill( ' ', length );

	int outIndex = 0;
	for ( int i = 0; i < length; ) {
		unsigned int cp = 0;
		const int used = LangDict_DecodeUtf8( p + i, length - i, cp );
		if ( used == 0 ) {
			// cannot happen after the validation pass above; fail closed
			return false;
		}

		unsigned char mapped = '?';
		LangDict_CodePointToCp1252( cp, mapped );

		const char *fold = ( used > 1 ) ? LangDict_FoldForCp1252( mapped ) : NULL;
		if ( fold != NULL ) {
			for ( int k = 0; fold[k] != '\0'; k++ ) {
				converted[outIndex++] = fold[k];
			}
		} else {
			converted[outIndex++] = (char)mapped;
		}
		i += used;
	}
	converted.CapLength( outIndex );
	return true;
}

/*
============
idLangDict::Load
============
*/
bool idLangDict::Load( const char *fileName, bool clear ) {
	if ( clear ) {
		Clear();
	}

	const char *buffer = NULL;
	// 'transcoded' must be declared before 'src': idLexer::LoadMemory() stores
	// the pointer without copying, so the buffer has to outlive the lexer.
	idStr transcoded;
	idLexer src( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWMULTICHARLITERALS | LEXFL_ALLOWBACKSLASHSTRINGCONCAT );

	int len = idLib::fileSystem->ReadFile( fileName, (void**)&buffer );
	if ( len <= 0 ) {
		// let whoever called us deal with the failure (so sys_lang can be reset)
		return false;
	}

	const char *parseText = buffer;
	int parseLength = (int)strlen( buffer );
	if ( parseLength >= 3 && (unsigned char)parseText[0] == 0xEF &&
		 (unsigned char)parseText[1] == 0xBB && (unsigned char)parseText[2] == 0xBF ) {
		// a UTF-8 BOM would break ExpectTokenString( "{" )
		parseText += 3;
		parseLength -= 3;
	}
	if ( LangDict_ConvertUtf8ToCp1252( parseText, parseLength, transcoded ) ) {
		idLib::common->DPrintf( "%s: converted UTF-8 string table to the 8-bit font codepage\n", fileName );
		parseText = transcoded.c_str();
		parseLength = transcoded.Length();
	}

	src.LoadMemory( parseText, parseLength, fileName );
	if ( !src.IsLoaded() ) {
		return false;
	}

	idToken tok, tok2;
	src.ExpectTokenString( "{" );
	while ( src.ReadToken( &tok ) ) {
		if ( tok == "}" ) {
			break;
		}

		if ( src.ReadToken( &tok2 ) ) {
			if ( tok2 == "}" ) {
				break;
			}
			idLangKeyValue kv;
			kv.key = tok;
			kv.value = tok2;
// RAVEN BEGIN
			if( kv.key.CmpPrefix( STRTABLE_ID ) ) {
				common->Warning( "Invalid token id \'%s\' in \'%s\' line %d", kv.key.c_str(), fileName, src.GetLineNum() );
			} else {
				hash.Add( GetHashKey( kv.key ), args.Append( kv ) );
			}
// RAVEN END
		}
	}
	idLib::common->Printf( "%i strings read from %s\n", args.Num(), fileName );
	idLib::fileSystem->FreeFile( (void*)buffer );
	
	return true;
}

/*
============
idLangDict::Save
============
*/
void idLangDict::Save( const char *fileName ) {
	idFile *outFile = idLib::fileSystem->OpenFileWrite( fileName );
// RAVEN BEGIN
	if( !outFile ) {
		common->Printf( "Could not open file \'%s\'for writing\n", fileName );
		return;
	}
// RAVEN END
	outFile->WriteFloatString( "// string table" NEWLINE "// english" NEWLINE "//" NEWLINE NEWLINE "{" NEWLINE );
	for ( int j = 0; j < args.Num(); j++ ) {
		outFile->WriteFloatString( "\t\"%s\"\t\"", args[j].key.c_str() );
		int l = args[j].value.Length();
		char slash = '\\';
		char tab = 't';
		char nl = 'n';
		for ( int k = 0; k < l; k++ ) {
			char ch = args[j].value[k];
			if ( ch == '\t' ) {
				outFile->Write( &slash, 1 );
				outFile->Write( &tab, 1 );
			} else if ( ch == '\n' || ch == '\r' ) {
				outFile->Write( &slash, 1 );
				outFile->Write( &nl, 1 );
			} else {
				outFile->Write( &ch, 1 );
			}
		}
		outFile->WriteFloatString( "\"" NEWLINE );
	}
	outFile->WriteFloatString( NEWLINE "}" NEWLINE );
	idLib::fileSystem->CloseFile( outFile );
}

/*
============
idLangDict::FilterProfanity
============
*/
const char *idLangDict::FilterProfanity( const char *value ) const {
	idStr &filtered = profanityScratch[profanityScratchIndex];
	profanityScratchIndex = ( profanityScratchIndex + 1 ) & 3;

	bool censorProfanity = true;
	if ( idLib::cvarSystem != NULL ) {
		censorProfanity = idLib::cvarSystem->GetCVarInteger( "com_profanity" ) == 0;
	}

	const char *replacement = "!@#$";
	const int replacementHash = GetHashKey( "#str_41028" );
	for ( int i = hash.First( replacementHash ); i != -1; i = hash.Next( i ) ) {
		if ( args[i].key.Cmp( "#str_41028" ) == 0 ) {
			replacement = args[i].value;
			break;
		}
	}

	if ( LangDict_BuildProfanityFilteredString( value, replacement, censorProfanity, filtered ) ) {
		return filtered.c_str();
	}
	return value;
}

/*
============
idLangDict::GetString
============
*/
const char *idLangDict::GetString( const char *str ) const {

	if ( str == NULL || str[0] == '\0' ) {
		return "";
	}

	if ( idStr::Cmpn( str, STRTABLE_ID, STRTABLE_ID_LENGTH ) != 0 ) {
		return str;
	}

	int hashKey = GetHashKey( str );
	for ( int i = hash.First( hashKey ); i != -1; i = hash.Next( i ) ) {
		if ( args[i].key.Cmp( str ) == 0 ) {
			return FilterProfanity( args[i].value );
		}
	}

	// Quake 4 content uses "#str_1xxxxx" while some legacy engine paths still
	// request Doom 3-era "#str_xxxxx" ids. If the legacy id misses, retry with
	// a Quake 4-style remap before reporting an unknown string.
	const int legacyDigits = 5;
	const int legacyLength = STRTABLE_ID_LENGTH + legacyDigits;
	bool triedLegacyRemap = false;
	if ( idStr::Length( str ) == legacyLength ) {
		bool hasOnlyDigits = true;
		for ( int i = STRTABLE_ID_LENGTH; i < legacyLength; ++i ) {
			if ( str[i] < '0' || str[i] > '9' ) {
				hasOnlyDigits = false;
				break;
			}
		}

		if ( hasOnlyDigits ) {
			triedLegacyRemap = true;
			char remapped[STRTABLE_ID_LENGTH + legacyDigits + 2];
			idStr::snPrintf( remapped, sizeof( remapped ), "%s1%s", STRTABLE_ID, str + STRTABLE_ID_LENGTH );
			hashKey = GetHashKey( remapped );
			for ( int i = hash.First( hashKey ); i != -1; i = hash.Next( i ) ) {
				if ( args[i].key.Cmp( remapped ) == 0 ) {
					return FilterProfanity( args[i].value );
				}
			}
		}
	}

	// Keep compatibility paths quiet for stale Doom 3 id requests that don't
	// have a Quake 4 remap in the active language dictionary.
	if ( triedLegacyRemap ) {
		return str;
	}

	idLib::common->Warning( "Unknown string id %s", str );
	return str;
}

/*
============
idLangDict::AddString
============
*/
const char *idLangDict::AddString( const char *str ) {
	
	if ( ExcludeString( str ) ) {
		return str;
	}

	int c = args.Num();
	for ( int j = 0; j < c; j++ ) {
		if ( idStr::Cmp( args[j].value, str ) == 0 ){
			return args[j].key;
		}
	}

	int id = GetNextId();
	idLangKeyValue kv;
	kv.key = va( "#str_%06i", id );
	kv.value = str;
	c = args.Append( kv );
	assert( kv.key.CmpPrefix( STRTABLE_ID ) == 0 );
	hash.Add( GetHashKey( kv.key ), c );
	return args[c].key;
}

/*
============
idLangDict::GetNumKeyVals
============
*/
int idLangDict::GetNumKeyVals( void ) const {
	return args.Num();
}

/*
============
idLangDict::GetKeyVal
============
*/
const idLangKeyValue * idLangDict::GetKeyVal( int i ) const {
	return &args[i];
}

/*
============
idLangDict::AddKeyVal
============
*/
void idLangDict::AddKeyVal( const char *key, const char *val ) {
	idLangKeyValue kv;
	kv.key = key;
	kv.value = val;
	assert( kv.key.CmpPrefix( STRTABLE_ID ) == 0 );
	hash.Add( GetHashKey( kv.key ), args.Append( kv ) );
}

/*
============
idLangDict::ExcludeString
============
*/
bool idLangDict::ExcludeString( const char *str ) const {
	if ( str == NULL ) {
		return true;
	}

	int c = strlen( str );
	if ( c <= 1 ) {
		return true;
	}

	if ( idStr::Cmpn( str, STRTABLE_ID, STRTABLE_ID_LENGTH ) == 0 ) {
		return true;
	}

	if ( idStr::Icmpn( str, "gui::", strlen( "gui::" ) ) == 0 ) {
		return true;
	}

	if ( str[0] == '$' ) {
		return true;
	}

	int i;
	for ( i = 0; i < c; i++ ) {
		// isalpha() is only defined for unsigned char values and EOF; passing a
		// plain signed char is undefined for any byte >= 0x80, which every
		// accented CP1252 character is
		if ( isalpha( (unsigned char)str[i] ) ) {
			break;
		}
	}
	if ( i == c ) {
		return true;
	}
	
	return false;
}

/*
============
idLangDict::GetNextId
============
*/
int idLangDict::GetNextId( void ) const {
	int c = args.Num();
	//Let and external user supply the base id for this dictionary
	int id = baseID;

	if ( c == 0 ) {
		return id;
	}

	idStr work;
	for ( int j = 0; j < c; j++ ) {
		work = args[j].key;
		work.StripLeading( STRTABLE_ID );
		int test = atoi( work );
		if ( test > id ) {
			id = test;
		}
	}
	return id + 1;
}

/*
============
idLangDict::GetHashKey
============
*/
int idLangDict::GetHashKey( const char *str ) const {
	int hashKey = 0;
	for ( str += STRTABLE_ID_LENGTH; str[0] != '\0'; str++ ) {
		assert( str[0] >= '0' && str[0] <= '9' );
		hashKey = hashKey * 10 + str[0] - '0';
	}
	return hashKey;
}
