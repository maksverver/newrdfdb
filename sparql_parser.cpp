#include <memory>
#include "sparql_parser.h"

void Parser::accumulate_variables(const Pattern &p, std::set<std::string> &vars)
{
    for( std::vector<Quad>::const_iterator i = p.mandatory_quads.begin();
         i != p.mandatory_quads.end(); ++i )
    {
        for(int f = 0; f < 4; ++f)
        {
            const Node &node = (*i)[f];
            if(node.type == Node::variable)
                vars.insert(node.lexical);
        }
    }

    for( std::vector<Pattern*>::const_iterator i = p.optional_patterns.begin();
         i != p.optional_patterns.end(); ++i )
    {
        accumulate_variables(**i, vars);
    }
}


Parser::Parser(const char *begin, const char *end)
    : tok(begin, end)
{
}

bool Parser::accept(int type)
{
    if(tok.type() == type)
    {
        tok.advance();
        return true;
    }
    else
    {
        return false;
    }
}

bool Parser::accept_keyword(const char *keyword)
{
    if(tok.type() == Tokenizer::keyword)
    {
        const char *p = tok.begin();
        while(p < tok.end() && *keyword)
        {
            if(std::tolower(*p) != std::tolower(*keyword))
                return false;
            ++p, ++keyword;
        }

        if(p == tok.end() && !*keyword)
        {
            tok.advance();
            return true;
        }
    }
    return false;
}

bool Parser::parse_iri(std::string &str)
{
    if(tok.type() == Tokenizer::relative_iri)
    {
        const char *cur = tok.begin();
        while(*cur != ':')
            ++cur;
        std::map<std::string, std::string>::const_iterator i =
            namespace_prefix.find(std::string(tok.begin(), cur));
        if(i == namespace_prefix.end())
        {
            throw std::string("Undeclared namespace prefix \"")
                + std::string(tok.begin(), cur) + "\" used!";
        }
        str = i->second;
        str.append(cur + 1, tok.end());
    }
    else
    if(tok.type() == Tokenizer::absolute_iri)
    {
        // FIXME: escaping?
        str.assign(tok.begin() + 1, tok.end() - 1);
    }
    else
    {
        return false;
    }

    tok.advance();
    return true;
}

bool Parser::parse_node(Node &node)
{
    switch(tok.type())
    {
    case Tokenizer::relative_iri:
    case Tokenizer::absolute_iri:
        if(!parse_iri(node.lexical))
            return false;
        node.type = Node::resource;
        node.datatype.clear();
        return true;

    case Tokenizer::literal:
        {
            // FIXME: escaping
            // FIXME: support for plain integers etc.
            node.type = Node::literal;
            node.lexical.assign(tok.begin() + 1, tok.end() - 1);
            node.datatype.clear();
            tok.advance();

            if(accept(Tokenizer::operator_datatype))
            {
                if(!parse_iri(node.datatype))
                    syntax_error("datatype IRI expected after '^^' token");
            }
            else
            if(accept(Tokenizer::language_tag))
            {
                // FIXME: be more strict on format of language identifier
                if(tok.type() != Tokenizer::keyword)
                    syntax_error("language identifier expected after '@' token");
                // Note: language is ignored
            }
            else
            {
                node.datatype.clear();  // No datatype given.
            }
        }
        return true;

    case Tokenizer::variable:
        {
            node.type = Node::variable;
            node.lexical.assign(tok.begin() + 1, tok.end());
            tok.advance();
        }
        return true;
    }

    return false;
}

bool Parser::parse_basic_graph_pattern(std::vector<Quad> &quads)
{
    Quad t = { Node::unbound };

    if(!parse_node(t.subject))
        return false;

    if(!parse_node(t.predicate))
        syntax_error("predicate expected while reading triple");

    if(!parse_node(t.object))
        syntax_error("object expected while reading triple");

    quads.push_back(t);

    while(true)
    {
        switch(tok.type())
        {
        case '.':
            tok.advance();

            if(!parse_node(t.subject))
                return true;

            if(!parse_node(t.predicate))
                syntax_error("predicate expected while reading triple");

            if(!parse_node(t.object))
                syntax_error("object expected while reading triple");

            quads.push_back(t);
            break;

        case ';':
            tok.advance();

            if(!parse_node(t.predicate))
                syntax_error("predicate expected while reading triple");
        
            if(!parse_node(t.object))
                syntax_error("object expected while reading triple");

            quads.push_back(t);
            break;

        case ',':
            tok.advance();

            if(!parse_node(t.object))
                syntax_error("object expected while reading triple");

            quads.push_back(t);
            break;

        default:
            return true;
        }
    }
}

bool Parser::parse_group_graph_pattern(Pattern &pattern)
{
    if(!accept('{'))
        return false;

    while(true)
    {
        parse_basic_graph_pattern(pattern.mandatory_quads);

        if(parse_group_graph_pattern(pattern))
        {
            accept('.');
            continue;
        }
        else
/*
        if(parse_value_constraint())
        {
            accept('.');
            continue;
        }
        else
*/
        if(accept_keyword("OPTIONAL"))
        {
            Pattern *p = new Pattern;
            if(!parse_group_graph_pattern(*p))
            {
                delete p;
                throw "group pattern expected after OPTIONAL keyword";
            }
            accept('.');
            pattern.optional_patterns.push_back(p);
            continue;
        }
        else
/*
        if(parse_union_constraint())
        {
            accept('.');
            continue;
        }
        else
        if(parse_dataset_constraint())
        {
            accept('.');
            continue;
        }
        else
*/
        break;
    }

    if(!accept('}'))
        syntax_error("closing curly brace expected");

    return true;
}

bool Parser::parse_integer(long long &i)
{
    if(tok.type() != Tokenizer::integer)
        return false;

    i = 0;
    for(const char *p = tok.begin(); p != tok.end(); ++p)
    {
        long long j = 10*i + (*p - '0');
        if(j < i)   // integer overflow!
            return false;
        i = j;
    }

    tok.advance();

    return true;
}

OrderCond *Parser::parse_order_condition()
{
    bool desc = false;
    Expr *expr;

    if(tok.type() == Tokenizer::variable)
    {
        Node *var = new Node;
        parse_node(*var);
        expr = new Expr(var);
    }
    else
    if(accept_keyword("ASC"))
    {
        expr = parse_bracketted_expression();
        if(!expr)
            syntax_error("bracketted expression expected after ASC keyword");
    }
    else
    if(accept_keyword("DESC"))
    {
        desc = true;
        expr = parse_bracketted_expression();
        if(!expr)
            syntax_error("bracketted expression expected after DESC keyword");
    }
    else
    {
        expr = parse_bracketted_expression();
    }

    return expr ? new OrderCond(desc, expr) : NULL;
}


Expr *Parser::parse_bracketted_expression()
{
    if(!accept('('))
        return NULL;

    Expr *e = parse_or_expression();
    if(!e)
        syntax_error("expression expected after '(' token");

    if(!accept(')'))
    {
        delete e;
        syntax_error("')' token expected after expression");
    }

    return e;
}

Expr *Parser::parse_or_expression()
{
    Expr *e = parse_and_expression();
    if(!e)
        return false;

    while(accept(Tokenizer::operator_and))
    {
        Expr *f = parse_and_expression();
        if(!f)
        {
            delete e;
            syntax_error("expression expected after '||' token");
        }
        e = new Expr(Expr::and, e, f);
    }

    return e;
}

Expr *Parser::parse_and_expression()
{
    Expr *e = parse_relational_expression();
    if(!e)
        return false;

    while(accept(Tokenizer::operator_and))
    {
        Expr *f = parse_relational_expression();
        if(!f)
        {
            delete e;
            syntax_error("expression expected after '&&' token");
        }
        e = new Expr(Expr::and, e, f);
    }

    return e;
}

Expr *Parser::parse_relational_expression()
{
    Expr *e = parse_additive_expression();
    if(!e)
        return NULL;

    Expr::Op op;

    if(accept('='))
        op = Expr::equal;
    else
    if(accept('<'))
        op = Expr::less;
    else
    if(accept('>'))
        op = Expr::greater;
    else
    if(accept(Tokenizer::operator_not_equal))
        op = Expr::not_equal;
    else
    if(accept(Tokenizer::operator_less_equal))
        op = Expr::less_equal;
    else
    if(accept(Tokenizer::operator_greater_equal))
        op = Expr::greater_equal;
    else
        return e;

    Expr *f = parse_additive_expression();
    if(!f)
    {
        delete e;
        syntax_error("expression expected after relational token");
    }

    return new Expr(op, e, f);
}

Expr *Parser::parse_additive_expression()
{
    Expr *e = parse_multiplicative_expression();
    if(!e)
        return false;

    while(true)
    {
        Expr::Op op;
        if(accept('+'))
            op = Expr::plus;
        else
        if(accept('-'))
            op = Expr::min;
        else
            break;

        Expr *f = parse_multiplicative_expression();
        if(!f)
        {
            delete e;
            syntax_error("expression expected after additive token");
        }
        e = new Expr(op, e, f);
    }

    return e;
}

Expr *Parser::parse_multiplicative_expression()
{
    Expr *e = parse_unary_expression();
    if(!e)
        return false;

    while(true)
    {
        Expr::Op op;
        if(accept('*'))
            op = Expr::mult;
        else
        if(accept('/'))
            op = Expr::div;
        else
            break;

        Expr *f = parse_unary_expression();
        if(!f)
        {
            delete e;
            syntax_error("expression expected after additive token");
        }
        e = new Expr(op, e, f);
    }

    return e;
}

Expr *Parser::parse_unary_expression()
{
    if(accept('!'))
    {
        Expr *e = parse_primary_expression();
        if(!e)
            syntax_error("primary expression expected after '!' token");
        else
            return new Expr(Expr::inv, e);
    }
    else
    if(accept('+'))
    {
        Expr *e = parse_primary_expression();
        if(!e)
            syntax_error("primary expression expected after '+' token");
        else
            return e;
    }
    else
    if(accept('-'))
    {
        Expr *e = parse_primary_expression();
        if(!parse_primary_expression())
            syntax_error("primary expression expected after '-' token");
        else
            return new Expr(Expr::neg, e);
    }
    else
    {
        return parse_primary_expression();
    }

    // Should never get here.
    return NULL;
}

Expr *Parser::parse_primary_expression()
{
    // TODO: built-in call
    // TODO: function call

    Expr *e;

    // Bracketed expression
    if((e = parse_bracketted_expression()))
        return e;

    // Value
    Node n;
    if(parse_node(n))
        return new Expr(new Node(n));

    return NULL;
}

Query *Parser::parse()
{
    std::auto_ptr<Query> query(new Query());

    // Parse namespace abbreviations
    while(accept_keyword("PREFIX"))
    {
        if(tok.type() != Tokenizer::relative_iri)
            syntax_error("IRI prefix expected in PREFIX clause");

        if(*(tok.end() - 1) != ':')
            syntax_error("IRI prefix should end with a colon");

        std::string prefix(tok.begin(), tok.end() - 1);
        tok.advance();

        if(tok.type() != Tokenizer::absolute_iri)
            syntax_error("absolute IRI expected in PREFIX clause");

        namespace_prefix[prefix].assign(tok.begin() + 1, tok.end() - 1);
        tok.advance();
    }

    // Parse verb
    if(!accept_keyword("SELECT"))
        syntax_error("query verb expected");

    // Parse distinct
    query->distinct = accept_keyword("DISTINCT");

    // Parse projection
    while(tok.type() == Tokenizer::variable)
    {
        query->projection.push_back(std::string(tok.begin() + 1, tok.end()));
        tok.advance();
    }
    if(query->projection.empty() && !accept('*'))
        syntax_error("list of variables or '*' expected after SELECT keyword");

    // Parse graph pattern
    accept_keyword("WHERE");
    if(!parse_group_graph_pattern(*query->pattern))
        syntax_error("group graph pattern expected after WHERE keyword");

    if(query->projection.empty())
    {
        std::set<std::string> vars;
        accumulate_variables(*query->pattern, vars);
        query->projection.assign(vars.begin(), vars.end());
    }

    // Parse ORDER BY solution modifier
    if(accept_keyword("ORDER"))
    {
        Node var;
        if(!accept_keyword("BY"))
            syntax_error("BY keyword expected after ORDER keyword");

        OrderCond *oc = parse_order_condition();
        if(!oc)
            syntax_error("order condition expected after 'ORDER BY'");

        do {
            query->order.push_back(oc);
        } while((oc = parse_order_condition()));

    }

    // Parse LIMIT solution modifier
    query->limit = -1;
    if(accept_keyword("LIMIT") && !parse_integer(query->limit))
        syntax_error("non-negative integer expected after LIMIT keyword");

    // Parse OFFSET solution modifier
    query->offset = -1;
    if(accept_keyword("OFFSET") && !parse_integer(query->offset))
        syntax_error("non-negative integer expected after OFFSET keyword");

    return query.release();
}

bool Parser::full()
{
    return tok.type() == Tokenizer::done;
}

void Parser::syntax_error(const char *msg)
{
    // TODO: annotate with location of error
    // TODO: include next available (unexpected) token
    throw std::string("Syntax error: ") + msg + "!";
}
