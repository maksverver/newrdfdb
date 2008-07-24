#ifndef TOKENIZER_INCLUDED
#define SPARQL_TOKENIZER_INCLUDED

class Tokenizer
{
public:
    enum types { error = -1, done = 0,
                 keyword, absolute_iri, relative_iri, variable, language_tag,
                 literal, integer,
                 operator_or, operator_and, operator_datatype,
                 operator_not_equal, operator_less_equal, operator_greater_equal };

protected:
    const char *cur, *eof;
    int token_type;
    const char *token_begin, *token_end;

    inline static bool idchar(char c);
    inline static bool ncchar(char c);

public:
    Tokenizer();
    Tokenizer(const char *begin, const char *end);

    inline int type() const;
    inline int size() const;
    inline const char *begin() const;
    inline const char *end() const;

    int advance();
};

int Tokenizer::type() const
{
    return token_type;
}

int Tokenizer::size() const
{
    return token_end - token_begin;
}

const char *Tokenizer::begin() const
{
    return token_begin;
}

const char *Tokenizer::end() const
{
    return token_end;
}

#endif /* ndef SPARQL_TOKENIZER_INCLUDED */

