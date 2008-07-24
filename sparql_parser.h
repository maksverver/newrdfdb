#ifndef SPARQL_PARSER_INCLUDED

#include <string>
#include <vector>
#include <map>
#include <set>
#include "sparql_tokenizer.h"

struct Node
{
    enum Type { unbound, resource, literal, variable } type;
    std::string lexical, datatype;
};

struct Quad
{
    Node graph, subject, predicate, object;

    inline Node &operator[] (int n);
    inline const Node &operator[] (int n) const;
};

class Expr
{
private:
    Expr(const Expr&);
    Expr &operator=(const Expr&);

public:
    const enum Op { and, or, mult, div, plus, min, neg, inv,
                    equal, not_equal, greater, greater_equal, less, less_equal,
                    value } op;
    Expr *const lhs, *const rhs;
    Node *const node;

    inline Expr(Op op, Expr *lhs, Expr *rhs = NULL);
    inline Expr(Node *node);
    inline ~Expr();
};

class Pattern
{
private:
    Pattern(const Pattern&);
    Pattern &operator=(const Pattern&);

public:
    inline Pattern();
    inline ~Pattern();

    std::vector<Quad>     mandatory_quads;
    std::vector<Pattern*> optional_patterns;
};

class OrderCond
{
    OrderCond(const OrderCond&);
    OrderCond &operator=(const OrderCond&);

public:
    inline OrderCond(bool desc, Expr *expr);
    inline ~OrderCond();

    bool desc;
    Expr *expr;
};

class Query
{
private:
    Query(const Query&);
    Query &operator=(const Query&);

public:
    inline Query();
    inline ~Query();

    // verb
    bool distinct;
    std::vector<std::string> projection;
    Pattern * const pattern;
    // from?
    // projection
    // distinct
    std::vector<OrderCond*> order;
    long long limit;
    long long offset;
};

class Parser
{
    Tokenizer tok;

    std::map<std::string, std::string> namespace_prefix;

    inline bool accept(int type);
    inline bool accept_keyword(const char *keyword);
    bool parse_iri(std::string &str);
    bool parse_node(Node &nr);
    bool parse_basic_graph_pattern(std::vector<Quad> &quads);
    bool parse_group_graph_pattern(Pattern &pattern);
    bool parse_integer(long long &i);

    OrderCond *parse_order_condition();

    // Parsing expressions
    Expr *parse_bracketted_expression();
    Expr *parse_or_expression();
    Expr *parse_and_expression();
    Expr *parse_relational_expression();
    Expr *parse_additive_expression();
    Expr *parse_multiplicative_expression();
    Expr *parse_unary_expression();
    Expr *parse_primary_expression();

    static void accumulate_variables(const Pattern &p, std::set<std::string> &vars);

    void syntax_error(const char *msg);

public:
    Parser(const char *begin, const char *end);

    Query *parse();
    bool full();
};


// Implementation of Quad inline members
Node &Quad::operator[] (int n)
{
    return ((Node*)this)[n];
}

const Node &Quad::operator[] (int n) const
{
    return ((Node*)this)[n];
}

// Implementation of Pattern inline members
Pattern::Pattern()
{
}

Pattern::~Pattern()
{
    for( std::vector<Pattern*>::const_iterator i = optional_patterns.begin();
         i != optional_patterns.end(); ++i )
    {
        delete *i;
    }
}

// Implementation of OrderCond inline members
OrderCond::OrderCond(bool desc, Expr *expr)
    : desc(desc), expr(expr)
{
}

OrderCond::~OrderCond()
{
    delete expr;
}


// Implementation of Query inline members
Query::Query() : pattern(new Pattern)
{
}

Query::~Query()
{
    delete pattern;

    for( std::vector<OrderCond*>::const_iterator i = order.begin();
         i != order.end(); ++i )
    {
        delete *i;
    }
}


// Implementation of Expr inline members
Expr::Expr(Op op, Expr *lhs, Expr *rhs)
    : op(op), lhs(lhs), rhs(rhs), node(NULL)
{
}

Expr::Expr(Node *node)
    : op(value), lhs(NULL), rhs(NULL), node(node)
{
}

Expr::~Expr()
{
    delete lhs;
    delete rhs;
    delete node;
}

#endif /* ndef SPARQL_PARSER_INCLUDED */
