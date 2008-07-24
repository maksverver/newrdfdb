#include <iostream>
#include <sstream>
#include <map>
#include <string>
#include <libgen.h>
#include "sqlite3.h"
#include "sparql_parser.h"

static sqlite3 *db;

typedef long long int nid_t;

/* Built-in datatypes */
#define TYPE_URI        (0ll)
#define TYPE_LITERAL    (1ll)
#define TYPE_BOOLEAN    (2ll)
#define TYPE_INTEGER    (3ll)
#define TYPE_DATE_TIME  (4ll)
#define TYPE_FLOAT      (5ll)
#define TYPE_DOUBLE     (6ll)

class SQLMapper
{
    typedef std::map<std::pair<std::string, nid_t>, nid_t> nodes_t;
    typedef std::map<std::string, std::pair<int, char> > bindings_t;

    std::ostringstream os;
    int tables;
    bindings_t bindings;
    std::set<std::string> resources;

    void generate_joins(const Pattern &p, bool optional);
    void write_expression(const Expr &expr);

public:
    SQLMapper(const Query &query);

    bool resource(const std::string &var) const;
    std::string sql() const;
};

static nid_t nid(const std::string &lexical, nid_t datatype);


void SQLMapper::generate_joins(const Pattern &p, bool optional)
{
    for( std::vector<Quad>::const_iterator i = p.mandatory_quads.begin();
         i != p.mandatory_quads.end(); ++i )
    {
        const Quad &q = *i;
        const int table = tables++;
        os << (optional ? " LEFT JOIN" : " JOIN") << " Quad q" << table;

        int constraint = 0;
        static const char field[4] = { 'g', 's', 'p', 'o' };
        for(int f = 0; f < 4; ++f)
        {
            const Node &node = q[f];
            if(node.type == Node::variable)
            {
                if(f != 3)
                    resources.insert(node.lexical);
    
                bindings_t::const_iterator j = bindings.find(node.lexical);
                if(j == bindings.end())
                {
                    bindings[node.lexical] = std::make_pair(table, field[f]);
                }
                else
                {
                    os << (constraint++ == 0 ? " ON" : " AND")
                        << " q" << table << '.' << field[f] << '='
                        << 'q' << j->second.first << '.' << j->second.second;
                }
            }
            else
            if(node.type == Node::resource)
            {
                os << (constraint++ == 0 ? (" ON") : " AND")
                    << " q" << table << '.' << field[f] << '='
                    << nid(node.lexical.c_str(), TYPE_URI);
                    ++constraint;
            }
            else
            if(node.type == Node::literal)
            {
                nid_t datatype = node.datatype.empty() ? TYPE_LITERAL
                    : nid(node.datatype.c_str(), TYPE_URI);
                os << (constraint++ == 0 ? (" ON") : " AND")
                    << " q" << table << '.' << field[f] << '='
                    << nid(node.lexical.c_str(), datatype);
            }
        }
    }

    for( std::vector<Pattern*>::const_iterator i = p.optional_patterns.begin();
         i != p.optional_patterns.end(); ++i )
    {
        generate_joins(**i, true);
    }
}

void SQLMapper::write_expression(const Expr &expr)
{
    switch(expr.op)
    {
    case Expr::value:
        switch(expr.node->type)
        {
        case Node::variable:
            {
                bindings_t::const_iterator j = bindings.find(expr.node->lexical);
                if(j == bindings.end())
                {
                    throw std::string("Variable \"") + expr.node->lexical +
                        "\" not used in graph pattern!";
                }
        
                os << " (SELECT l FROM Node WHERE oid=q" << j->second.first
                << '.' << j->second.second << ")";
            }
            break;

        default:
            throw "unsupported node type";
        }
        break;

    case Expr::and:
    case Expr::or:
    case Expr::mult:
    case Expr::div:
    case Expr::plus:
    case Expr::min:
    case Expr::neg:
    case Expr::inv:
    case Expr::equal:
    case Expr::not_equal:
    case Expr::greater:
    case Expr::greater_equal:
    case Expr::less:
    case Expr::less_equal:
    default:
        throw "unsupported expression operator";
    }
}

bool SQLMapper::resource(const std::string &var) const
{
    return resources.find(var) != resources.end();
}

SQLMapper::SQLMapper(const Query &query)
{
    // Generate joins
    tables = 0;
    generate_joins(*query.pattern, false);
    std::string joins = os.str();
    os.str(std::string());

    os << "SELECT";

    if(query.distinct)
        os << " DISTINCT";

    // Generate projection
    for( std::vector<std::string>::const_iterator i = query.projection.begin();
         i != query.projection.end(); ++i )
    {
        bindings_t::const_iterator j = bindings.find(*i);
        if(j == bindings.end())
        {
            throw std::string("Variable \"") + *i + "\" not used in graph pattern!";
        }

        if(resources.find(*i) == resources.end())
        {
            // Select datatype as well
            os << " (SELECT d.l FROM Node n JOIN Node d ON n.d = d.oid"
                  " WHERE n.oid=q" << j->second.first << '.' << j->second.second
               << "),";
        }

        os << " (SELECT l FROM Node WHERE oid=q" << j->second.first
           << '.' << j->second.second << ")" << ',';
    }
    os << " NULL FROM (SELECT NULL)" << joins;

    // Solution modifier: ORDER BY
    if(!query.order.empty())
    {
        for( std::vector<OrderCond*>::const_iterator i = query.order.begin();
             i != query.order.end(); ++i )
        {
            os << (i == query.order.begin() ? " ORDER BY" : ",");
            write_expression(*(*i)->expr);
            if((*i)->desc)
                os << " DESC";
        }
    }

    // Solution modifier: LIMIT
    if(query.limit >= 0)
        os << " LIMIT " << query.limit;

    // Solution modifier: OFFSET
    if(query.offset >= 0)
        os << (query.limit >= 0 ? " OFFSET " : " LIMIT -1 OFFSET ") << query.offset;
}

std::string SQLMapper::sql() const
{
    return os.str();
}


nid_t nid(const std::string &lexical, nid_t datatype)
{
    static sqlite3_stmt *stmt = NULL;

    if(!stmt)
    {
        // Prepare statement
        const char *sql = "SELECT oid FROM Node WHERE l=?1 AND d=?2";
        int result = sqlite3_prepare(db, sql, -1, &stmt, NULL);
        if(result == SQLITE_BUSY)
            throw "Database is busy!";
        else
        if(result != SQLITE_OK)
        {
            throw std::string() +
                "Unable to prepare statement: \"" + sql + "\"!";
        }
    }

    nid_t id = -1;
    sqlite3_bind_text (stmt, 1, lexical.data(), lexical.size(), SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, datatype);
    int result = sqlite3_step(stmt);
    if(result == SQLITE_ROW)
        id = sqlite3_column_int64(stmt, 0);
    else
    if(result == SQLITE_BUSY)
        throw "Database is busy!";
    else
    if(result != SQLITE_DONE)
        throw "Unable to retrieve node identifier!";

    sqlite3_reset(stmt);

    return id;
}


/*
    Restricties:
        - minstens 1 verplichte quad vereist
        - twee gescheiden optionele subgroups mogen geen variabelen delen
*/

#include "libxml/xmlwriter.h"


static char *argv0;

void usage(bool fatal = true)
{
    std::cout << "Usage: " << basename(argv0) << " [-s|--sql] <database> <query>" << std::endl;
    exit(fatal ? 1 : 0);
}

int main(int argc, char *argv[])
{
    if(argc < 3)
        usage(argc != 1);

    // Parse command line options
    bool output_sql = false;
    const char *database_path, *query;
    argv0 = *(argv++), --argc;
    if(std::string("-s") == *argv || std::string("--sql") == *argv)
    {
        output_sql = true;
        ++argv, --argc;
    }
    if(argc != 2)
        usage();
    database_path = *(argv++), --argc;
    query         = *(argv++), --argc;

    // Initialize sqlite
    if(sqlite3_open(database_path, &db) != SQLITE_OK)
    {
        std::cerr << "Unable to open database!" << std::endl;
        return 1;
    }

    // Initialize XML writer
    xmlTextWriterPtr writer = xmlNewTextWriterFilename("-", 0);
    if(writer == NULL)
    {
        std::cerr << "Unable to create XML text writer!" << std::endl;
        return 1;
    }

    /*
    Tokenizer t(input, input + std::strlen(input));
    while(t.type() > 0)
    {
        std::cout << t.type() << ' ';
        std::cout << '[';
        std::cout.write(t.begin(), t.size());
        std::cout << ']' << std::endl;
        t.advance();
    }
    */

    // Write header
    if(!output_sql)
    {
        xmlTextWriterStartDocument(writer, NULL, NULL, "yes");
        xmlTextWriterStartElement(writer, (xmlChar*)"sparql");
        xmlTextWriterWriteAttribute( writer, (xmlChar*)"xmlns",
            (xmlChar*)"http://www.w3.org/2005/sparql-results#" );
    }

    try {
        // Parse query
        Parser p(query, query + std::strlen(query));
        Query *q = p.parse();
        if(!p.full())
        {
            throw "Extra characters at end of SPARQL query!";
        }

        // Map to SQL
        SQLMapper mapper(*q);
        std::string sql = mapper.sql();

        // Determine types of variables
        std::vector<bool> is_resource;
        for( std::vector<std::string>::const_iterator i = q->projection.begin();
             i != q->projection.end(); ++i )
        {
            is_resource.push_back(mapper.resource(*i));
        }

        // Prepare generated query
        sqlite3_stmt *stmt;
        if(sqlite3_prepare(db, sql.data(), sql.size(), &stmt, NULL) != SQLITE_OK)
        {
            throw std::string("Unable to prepare generated SQL query: \"")
                + sql + "\"!";
        }

        if(output_sql)
        {
            std::cout << sql << std::endl;
        }
        else
        {
            int result = sqlite3_step(stmt);
            if(result != SQLITE_DONE && result != SQLITE_ROW)
            {
                sqlite3_finalize(stmt);
                if(result == SQLITE_BUSY)
                    throw "Database is busy!";
            }

            // Write header
            xmlTextWriterStartElement(writer, (xmlChar*)"head");
            for( std::vector<std::string>::const_iterator i = q->projection.begin();
                i != q->projection.end(); ++i )
            {
                xmlTextWriterStartElement(writer, (xmlChar*)"variable");
                xmlTextWriterWriteAttribute( writer,
                    (xmlChar*)"name", (xmlChar*)i->c_str() );
                xmlTextWriterEndElement(writer);
            }
            xmlTextWriterEndElement(writer);
    
            // Write results
            xmlTextWriterStartElement(writer, (xmlChar*)"results");
            while(result == SQLITE_ROW)
            {
                xmlTextWriterStartElement(writer, (xmlChar*)"result");
                int col = 0;
                for(size_t n = 0; n < q->projection.size(); ++n)
                {
                    xmlTextWriterStartElement(writer, (xmlChar*)"binding");
                    xmlTextWriterWriteAttribute( writer,
                        (xmlChar*)"name", (xmlChar*)(q->projection[n].c_str()) );
    
                    const unsigned char *datatype = NULL;
                    if(!is_resource[n])
                        datatype = sqlite3_column_text(stmt, col++);
    
                    if(datatype)
                    {
                        xmlTextWriterStartElement(writer, (xmlChar*)"literal");
                        if(*datatype)
                        {
                            xmlTextWriterWriteAttribute(
                                writer, (xmlChar*)"datatype", datatype );
                        }
                        xmlTextWriterWriteString(
                            writer, sqlite3_column_text(stmt, col++) );
                        xmlTextWriterEndElement(writer);
                    }
                    else
                    {
                        // TODO: support blank nodes in addition to uri's
                        xmlTextWriterStartElement(writer, (xmlChar*)"uri");
                        xmlTextWriterWriteString(writer, sqlite3_column_text(stmt, col++));
                        xmlTextWriterEndElement(writer);
                    }
    
                    xmlTextWriterEndElement(writer);
                }
    
                result = sqlite3_step(stmt);
                xmlTextWriterEndElement(writer);
            }
            xmlTextWriterEndElement(writer);
        }

        // Clean up
        sqlite3_finalize(stmt);

    } catch(const char *str) {
        if(output_sql)
        {
            std::cerr << str;
        }
        else
        {
            xmlTextWriterStartElement(writer, (xmlChar*)"head");
            xmlTextWriterStartElement(writer, (xmlChar*)"error");
            xmlTextWriterWriteCDATA(writer, (xmlChar*)str);
        }
    } catch(const std::string &str) {
        if(output_sql)
        {
            std::cerr << str;
        }
        else
        {
            xmlTextWriterStartElement(writer, (xmlChar*)"head");
            xmlTextWriterStartElement(writer, (xmlChar*)"error");
            xmlTextWriterWriteCDATA(writer, (xmlChar*)str.c_str());
        }
    }

    if(!output_sql)
    {
        xmlTextWriterEndDocument(writer);
        xmlFreeTextWriter(writer);
    }

    return 0;
}
