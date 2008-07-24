/* FIXME
    Te tokenizer currently DOES NOT conform to the SPARQL production rules!
      - it lacks support for character escapes;
      - it needs to be stricter in what characters it accepts in names;
      - ... and there might be more issues. */

#include "sparql_tokenizer.h"
#include <cctype>
#include <cstring>

Tokenizer::Tokenizer()
{
}

Tokenizer::Tokenizer(const char *begin, const char *end)
    : cur(begin), eof(end), token_type(error)
{
    advance();
}


bool Tokenizer::idchar(char c)
{
    return std::isalnum(c) || c == '_';
}

bool Tokenizer::ncchar(char c)
{
    return std::isalnum(c) || c == ':' || c == '_' || c == '-';
}

int Tokenizer::advance()
{
    while(cur != eof && std::isspace(*cur)) ++cur;
    if(cur == eof)
        return token_type = done;

    if(std::isdigit(*cur))
    {
        token_begin = cur++;
        while(cur != eof && isdigit(*cur)) ++cur;
        token_end = cur;
        return token_type = integer;
    }

    switch(*cur)
    {

    case '<':
        const char *p = cur + 1;
        while( p != eof && ((unsigned char)(*p)) > 0x20 &&
               std::strchr("<>'{}|^`", *p) == NULL )
            ++p;
        if(*p == '>')
        {
            token_begin = cur;
            token_end   = cur = ++p;
            return token_type = absolute_iri;
        }
    // FALLS THROUGH!

    case '>':
    case '!':
        token_begin = cur++;
        if(cur == eof || *cur != '=')
        {
            token_end = cur;
            return token_type = *token_begin;
        }
        token_end = ++cur;
        switch(*token_begin)
        {
        case '<': return token_type = operator_less_equal;
        case '>': return token_type = operator_greater_equal;
        case '!': return token_type = operator_not_equal;
        }
        break;

    case '^':
    case '&':
    case '|':
        token_begin = cur++;
        if(cur == eof || *cur != *token_begin)
            break;
        token_end = ++cur;
        switch(*token_begin)
        {
        case '^': return token_type = operator_datatype;
        case '&': return token_type = operator_and;
        case '|': return token_type = operator_or;
        }
        break;

    case '{':
    case '}':
    case '(':
    case ')':
    case '[':
    case ']':
    case ';':
    case '.':
    case '+':
    case ',':
    case '*':
    case '-':
    case '/':
        token_begin = cur;
        token_end   = ++cur;
        return token_type = *token_begin;

    case '@':
        token_begin = ++cur;
        if(cur == eof || !std::isalpha(*cur))
            break;
        ++cur;
        while(cur != eof && (std::isalpha(*cur) || *cur == '-'))
            ++cur;
        token_end = cur;
        return token_type = language_tag;

    case '$':
    case '?':
        token_begin = cur++;
        while(cur != eof && idchar(*cur)) ++cur;
        token_end = cur;
        return token_type = variable;

    case '\'':
    case '\"':
        token_begin = cur++;
        while(cur != eof && *cur != *token_begin) ++cur;
        token_end = ++cur;
        return token_type = literal;

    default:
        if(ncchar(*cur))
        {
            token_type = keyword;

            token_begin = cur;
            while(cur != eof && ncchar(*cur))
            {
                if(*cur == ':')
                    token_type = relative_iri;
                ++cur;
            }
            token_end = cur;
            return token_type;
        }
    }

    return token_type = error;
}
