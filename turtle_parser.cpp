#include "turtle_parser.h"
#include <cstring>
#include <cstdio>

/*
    TODO: support datatypes: integer, double, decimal, boolean

    CHECKME: test if using a terminating character (\0) is faster than checking for cur==eob
*/
TurtleParser::TurtleParser(TurtleTokenizer::Reader reader, void *reader_arg) :
    tok(reader, reader_arg), state(expecting_subject)
{
    tok.advance();
};

bool TurtleParser::parse_subject()
{
    return parse_resource(subj);
}

bool TurtleParser::parse_predicate()
{
    return parse_resource(pred);
}

bool TurtleParser::parse_resource(std::string &uri)
{
    if(tok.type() == TurtleTokenizer::uri)
    {
        uri.assign(tok.begin(), tok.end());
        tok.advance();
        return true;
    }
    else
    if(tok.type() == TurtleTokenizer::name)
    {
        for(const char *p = tok.begin(); p != tok.end(); ++p)
            if(*p == ':')
            {
                // TODO: ensure there are no more semicolons in string
                std::map<std::string, std::string>::const_iterator i =
                    namespaces.find(std::string(tok.begin(), p));
                if(i == namespaces.end())
                    return false;
                uri = i->second + std::string(p + 1, tok.end());
                tok.advance();
                return true;
            }
    }
    return false;
}

bool TurtleParser::parse_literal()
{
    if(tok.type() == TurtleTokenizer::string)
    {
        lexical.assign(tok.begin(), tok.end());
        tok.advance();

        if(tok.type() == TurtleTokenizer::directive)
        {
            tok.advance();
            if(tok.type() != TurtleTokenizer::name)
                return false;
            if(*(tok.end() - 1) == '-')
                return false;
            lang.assign(tok.begin(), tok.end());
        }
        else
        {
            lang.clear();
            if(tok.type() == TurtleTokenizer::carets)
            {
                tok.advance();
                if(!parse_resource(type))
                    return false;
            }
            else
            {
                type.clear();
            }
        }
        return true;
    }
    // TODO: other types (which must be normalized!)
    return false;
}

bool TurtleParser::parse_object()
{
    if(parse_resource(obj))
    {
        lexical.clear();
        type.clear();
        lang.clear();
        return true;
    }
    if(parse_literal())
    {
        obj.clear();
        return true;
    }
    return false;
}

bool TurtleParser::advance()
{
    switch(state)
    {
    case done:
        return false;

    case expecting_subject:
        if(tok.type() == TurtleTokenizer::finished)
        {
            state = done;
            return false;
        }

        while(tok.type() == TurtleTokenizer::directive)
        {
            if( tok.size() != 6 ||
                std::strncmp(tok.begin(), "prefix", 6) != 0)
            {
                // Unsupported directive
                return false;
            }
            tok.advance();

            if( tok.type() != TurtleTokenizer::name ||
                *tok.begin() == '_' || *(tok.end() - 1) != ':' )
            {
                return false;
            }
            // TODO: additional restrictions on characters & no embedded colons?
            std::string prefix(tok.begin(), tok.end() - 1);
            tok.advance();

            if(tok.type() != TurtleTokenizer::uri)
                return false;
            namespaces[prefix].assign(tok.begin(), tok.end());
            tok.advance();

            if(tok.type() != TurtleTokenizer::dot)
                return false;
            tok.advance();
        }

        if(!parse_subject())
            return false;

    case expecting_predicate:
        if(!parse_predicate())
            return false;

    case expecting_object:
        if(!parse_object())
            return false;

        if(tok.type() == TurtleTokenizer::dot)
        {
            tok.advance();
            state = expecting_subject;
            return true;
        }

        if(tok.type() == TurtleTokenizer::semicolon)
        {
            tok.advance();
            if(tok.type() == TurtleTokenizer::dot)
            {
                state = expecting_subject;
                tok.advance();
            }
            else
            {
                state = expecting_predicate;
            }
            return true;
        }

        if(tok.type() == TurtleTokenizer::comma)
        {
            tok.advance();
            state = expecting_object;
            return true;
        }
    }

    return false;
}

extern "C"
int parse_turtle(
    size_t (*reader) (void *arg, char *buffer, size_t size),
    void *reader_arg,
    int (*callback) ( void *arg, const char *subject, const char *predicate,
                      const char *object, const char *lexical,
                      const char *datatype, const char *language ),
    void *callback_arg )
{
    TurtleParser tp(reader, reader_arg);
    while(tp.advance())
    {
        bool obj_res = tp.object_is_resource();
        int r = callback( callback_arg,
                  tp.subject_uri().c_str(),
                  tp.predicate_uri().c_str(),
                  obj_res ? tp.object_uri().c_str() : NULL,
                  (obj_res || tp.object_lexical().empty())
                        ? NULL: tp.object_lexical().c_str(),
                  (obj_res || tp.object_datatype().empty())
                        ? NULL : tp.object_datatype().c_str(),
                  (obj_res || tp.object_language().empty())
                        ? NULL : tp.object_language().c_str() );
        if(r != 0)
            return r;
    }
    return tp.good() ? 0 : -1;
}

extern "C" 
size_t fp_reader(void *arg, char *buffer, size_t size)
{
    size_t read = std::fread(buffer, 1, size, (FILE*)arg);
    if(read < size && ferror((FILE*)arg))
        return (size_t)-1;
    return read;
}
