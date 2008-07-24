#ifndef TURTLETOKENIZER_H_INCLUDED
#define TURTLETOKENIZER_H_INCLUDED

#include <cstddef>

class TurtleTokenizer
{
public:
    typedef size_t (*Reader) (void *arg, char *buffer, size_t size);
    static size_t istream_reader(void *arg, char *buffer, size_t size);
    enum token_type {
        finished, directive, string, uri, name, integer,
        dot, semicolon, comma, carets };

protected:
    char *cur, *eob, *t_begin, *t_end, *buffer;
    size_t buffer_size;
    token_type t_type;

    Reader reader;
    void *reader_arg;

    int error;


    bool refill_buffer();
    bool extend_buffer();
    bool parse_string(char end_char);
    bool parse_directive();

    inline bool name_char(char c);

public:
    TurtleTokenizer(Reader reader, void *reader_arg);

    inline bool good() const;
    inline token_type type() const;
    inline const char *begin() const;
    inline const char *end() const;
    inline size_t size() const;

    token_type advance();
};

bool TurtleTokenizer::good() const
{
    return error == 0;
}

TurtleTokenizer::token_type TurtleTokenizer::type() const
{
    return t_type;
}

const char *TurtleTokenizer::begin() const
{
    return t_begin;
}

const char *TurtleTokenizer::end() const
{
    return t_end;
}

size_t TurtleTokenizer::size() const
{
    return t_end - t_begin;
}

#endif // ndef TURTLETOKENIZER_H_INCLUDED

