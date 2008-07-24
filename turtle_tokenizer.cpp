#include <cstring>
#include <istream>
#include "turtle_tokenizer.h"

/*
    Missing support for:
        - \uXX or \uXXXX escaped characters
        - long strings (""" foo """)
        - collections
        - blank nodes denoted by square brackets0

    Bug:
        allows whitespaces between string literal and
        language tag / datatype.
*/

TurtleTokenizer::TurtleTokenizer(Reader reader, void *reader_arg)
    : reader(reader), reader_arg(reader_arg)
{
    cur = eob = buffer = new char[buffer_size = 512];
    error = 0;
}

bool TurtleTokenizer::refill_buffer()
{
    cur = buffer;
    eob = buffer + reader(reader_arg, buffer, buffer_size);
    return cur != eob;
}

bool TurtleTokenizer::extend_buffer()
{
    // Note: requires that t_begin is set correctly!
    if(size_t(4)*(t_begin - buffer) > buffer_size)
    {
        // Move active part of the buffer (starting at t_begin) to the front
        std::memmove(buffer, t_begin, eob - t_begin);
        cur     = cur   - t_begin + buffer;
        t_end   = t_end - t_begin + buffer;
        eob     = eob   - t_begin + buffer;
        t_begin = buffer;
    }

    if(size_t(2)*(eob - buffer) > buffer_size)
    {
        // Expand buffer
        size_t new_buffer_size = 2*buffer_size;
        char *new_buffer = new char[new_buffer_size];
        memcpy(new_buffer, buffer, eob - buffer);
        cur     = cur     - buffer + new_buffer;
        eob     = eob     - buffer + new_buffer;
        t_begin = t_begin - buffer + new_buffer;
        t_end   = t_end   - buffer + new_buffer;
        buffer = new_buffer;
        buffer_size = new_buffer_size;
    }

    eob += reader(reader_arg, eob, buffer + buffer_size - eob);
    return cur != eob;
}

bool TurtleTokenizer::parse_string(char end_char)
{
    bool escape_set = false;
    t_begin = t_end = ++cur;
parse_string:
    while(cur != eob)
    {
        if(escape_set)
        {
            switch(*cur)
            {
            case '\\':
                *(t_end++) = '\\';
                break;
            case 't':
                *(t_end++) = 0x09;
                break;
            case 'n':
                *(t_end++) = 0x0A;
                break;
            case 'r':
                *(t_end++) = 0x0D;
                break;
            default:
                if(*cur != end_char)
                    return false;
                *(t_end++) = end_char;
            }
            escape_set = false;
        }
        else
        if(*cur == '\\')
        {
            escape_set = true;
        }
        else
        if(*cur == end_char)
        {
            ++cur;
            return true;
        }
        else
        {
            *(t_end++) = *cur;
        }
        ++cur;
    }
    if(cur == eob)
    {
        if(!extend_buffer())
            return false;
        goto parse_string;
    }
    return true;
}

bool TurtleTokenizer::parse_directive()
{
    t_begin = ++cur;
parse_directive:
    while( cur != eob && ( *cur == '-' ||
                           (*cur >= 'a' && *cur <= 'z') ||
                           (*cur >= '0' && *cur <= '9') ))
    {
        ++cur;
    }
    if(cur == eob && extend_buffer())
        goto parse_directive;
    t_end = cur;
    return true;
}

bool TurtleTokenizer::name_char(char c)
{
    return (c >= 'a' && *cur <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c == '_' || c == ':' || ((signed char)c) < 0);
}

TurtleTokenizer::token_type TurtleTokenizer::advance()
{
    // Skip whitespace
skip_whitespace:
    while(cur != eob && ( *cur == 0x09 || *cur == 0x0A ||
                          *cur == 0x0D || *cur == 0x20) )
    {
        ++cur;
    }
    if(cur == eob)
    {
        if(refill_buffer())
            goto skip_whitespace;
        else
            return t_type = finished;
    }
    else
    {
        switch(*cur)
        {

        case '"':
            // Parse string
            if(parse_string('"'))
                return t_type = string;
            break;

        case '#':
            // Skip comment
        skip_comment:
            do {
                ++cur;
            } while(cur != eob && *cur != 0x0A && *cur != 0x0D);
            if(cur == eob)
            {
                if(refill_buffer())
                    goto skip_comment;
                else
                    return t_type = finished;
            }
            goto skip_whitespace;
    
        case ',':
            t_begin = cur;
            t_end   = ++cur;
            return t_type = comma;

        case '.':
            t_begin = cur;
            t_end   = ++cur;
            return t_type = dot;

        case ';':
            t_begin = cur;
            t_end   = ++cur;
            return t_type = semicolon;

        case '<':
            // Parse URI
            if(parse_string('>'))
                return t_type = uri;
            break;

        case '^':
            t_begin = cur;
            if(++cur != eob || extend_buffer())
            {
                if(*cur == '^')
                {
                    t_end = ++cur;
                    return t_type = carets;
                }
            }
            break;
    
        case '@':
            // Parse directive
            if(parse_directive())
                return t_type = directive;
            break;

        case '+': case '-': case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7': case '8': case '9':
            // Parse integer;
            t_begin = cur;
        parse_integer:
            do {
                ++cur;
            } while(cur != eob && *cur >= '0' && *cur <= '9');
            if(cur == eob && extend_buffer())
                goto parse_integer;
            t_end = cur;
            if(t_begin + 1 != t_end || (*t_begin != '+' && *t_begin != '-'))
                return t_type = integer;

        default:
            if(name_char(*cur))
            {
                // Parse name
                t_begin = cur++;
            parse_name:
                while(cur != eob && name_char(*cur))
                    ++cur;
                if(cur == eob)
                {
                    if(extend_buffer())
                        goto parse_name;
                }
                t_end   = cur;
                return t_type = name;
            }
        }
    }

    // Tokenizer error!
    error = 1;
    return t_type = finished;
}
