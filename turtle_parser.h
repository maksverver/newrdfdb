#ifndef TURTLEPARSER_H_INCLUDED
#define TURTLEPARSER_H_INCLUDED

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>

/*
Parses the Turtle data provided by 'reader' and calls the 'handler' once
for each tripple that is parsed. Both callback functions take an argument
supplied by the caller, which can be used to pass state information to
these functions.

The meaning of the arguments for 'reader' is as follows:
    arg:       caller-supplied argument
    buffer:    pointer to array to be filled
    size:      maximum number of bytes to read
The function should return the number of bytes read into 'buffer', 0 on
end-of-file or (size_t)-1 on error, which will cause parsing to be aborted.

The meaning of the arguments of 'handler' is as follows:
    arg:        caller-supplied argument
    subject:    the URI of the subject
    predicate:  the URI of the predicate
    object:     the URI of the object
                if it is a resource, or NULL if it is a literal.
    lexical:    the lexical representation of the object, if it is a literal
                or NULL if the object is not a literal
    datatype:   the URI of the datatype of a typed literal, or NULL if the
                object is not a typed literal
    language:   the language identifier of a literal, or NULL if the object is
                not a plain literal or has no language tag
All strings are UTF-8 encoded zero-terminated strings. The function should
return 0 on succes; any other value is regarded as an error and causes parsing
to be aborted.

This functions returns 0 if parsing all data has succeeded or -1 if an error
has occured. If the handler has returned a non-zero value, that value will be
returned instead.
*/

int parse_turtle(
    size_t (*reader) (void *arg, char *buffer, size_t size),
    void *reader_arg,
    int (*callback) ( void *arg, const char *subject, const char *predicate,
                      const char *object, const char *lexical,
                      const char *datatype, const char *language ),
    void *callback_arg );


/* Reader function for reading from FILE* objects.
   The argument should be (FILE*) cast to (void*). */
size_t fp_reader(void *arg, char *buffer, size_t size);


#ifdef __cplusplus
} /* extern "C" */


#include <string>
#include <map>
#include "turtle_tokenizer.h"

class TurtleParser
{
    TurtleTokenizer tok;
    enum { expecting_subject, expecting_predicate, expecting_object, done } state;
    std::map<std::string, std::string> namespaces;
    std::string subj, pred, obj, lexical, type, lang;

    bool parse_resource(std::string &uri);
    bool parse_literal();
    inline bool parse_subject();
    inline bool parse_predicate();
    inline bool parse_object();

public:
    TurtleParser(TurtleTokenizer::Reader reader, void *reader_arg);

    bool advance();
    inline bool good() const;
    inline const std::string &subject_uri() const;
    inline const std::string &predicate_uri() const;
    inline const std::string &object_uri() const;
    inline const std::string &object_lexical() const;
    inline const std::string &object_datatype() const;
    inline const std::string &object_language() const;
    inline bool object_is_resource() const;
    inline bool object_is_literal() const;
};

bool TurtleParser::good() const
{
    return state == done && tok.good();
}

const std::string &TurtleParser::subject_uri() const
{
    return subj;
}

const std::string &TurtleParser::predicate_uri() const
{
    return pred;
}

const std::string &TurtleParser::object_uri() const
{
    return obj;
}

const std::string &TurtleParser::object_lexical() const
{
    return lexical;
}

const std::string &TurtleParser::object_datatype() const
{
    return type;
}

const std::string &TurtleParser::object_language() const
{
    return lang;
}

bool TurtleParser::object_is_resource() const
{
    return !obj.empty();
}

bool TurtleParser::object_is_literal() const
{
    return obj.empty();
}

#endif /* def__cplusplus */

#endif /* ndef TURTLEPARSER_H_INCLUDED */
