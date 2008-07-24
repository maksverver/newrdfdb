#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <cstring>
#include <libgen.h>
#include <sqlite3.h>

/* FIXME
    This tool assumes the database is consistent (ie. subject is never NULL);
    if this is not the case it may crash.
*/

/* TODO
    - support for blank nodes
    - convert built-in types
    - test generation of namespace prefixes longer than 1 character

    Needs command line options for:
        - disabling automatic URI abbreviation
        - outputing in (a variant of) NTRIPLES format
        - listing all models in the database
        - (maybe: dropping all models in the database)
*/

typedef long long int nid_t;

sqlite3_stmt *prepare(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *stmt;
    if(sqlite3_prepare( db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        std::cerr << "Unable to prepare SQL statement:\n\t" << sql
                  << "\nsqlite: " << sqlite3_errmsg(db) << std::endl;
        return NULL;
    }
    return stmt;
}

void write_escaped( std::ostream &os, const char *str, char extra )
{
    while(true)
    {
        switch(*str)
        {
        case 0x00:
            return;
        case 0x09:
            os.write("\\t", 2);
            break;
        case 0x0A:
            os.write("\\n", 2);
            break;
        case 0x0D:
            os.write("\\r", 2);
            break;
        case 0x5C:
            os.write("\\\\", 2);
            break;
        default:
            if(*str == extra)
                os.put('\\');
            os.put(*str);
        }
        ++str;
    }
}

void write_uri(std::ostream &os, const char *uri)
{
    os.put('<');
    write_escaped(os, uri, '>');
    os.put('>');
}

void write_string(std::ostream &os, const char *str)
{
    os.put('"');
    write_escaped(os, str, '"');
    os.put('"');
}

int list_ntriples(std::ostream &os, sqlite3_stmt *stmt)
{
    int r;
    while((r = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        const char *subj = (const char*)sqlite3_column_text(stmt, 0),
                   *pred = (const char*)sqlite3_column_text(stmt, 1),
                   *obj  = (const char*)sqlite3_column_text(stmt, 2),
                   *type = (const char*)sqlite3_column_text(stmt, 3);
        write_uri(os, subj);
        os.put(' ');
        write_uri(os, pred);
        os.put(' ');
        if(type == NULL)
            write_uri(os, obj);
        else
        {
            write_string(os, obj);
            if(*type)
            {
                os.write("^^", 2);
                write_uri(os, type);
            }
        }
        os.write(".\n", 2);
    }
    return r;
}

void write_resource(
    std::ostream &os_statement,
    std::ostream &os_prefix,
    std::map<std::string, std::string> &abbreviations,
    const char *uri )
{
    const char *p = strchr(uri, '#');
    if(p == NULL)
        write_uri(os_statement, uri);
    else
    {
        std::string ns(uri, ++p);
        std::string &abbr = abbreviations[ns];
        if(abbr.empty())
        {
            // Pick a new abbreviation
            for(int id = abbreviations.size(); id != 0; id /= 26)
                abbr += char('a' + id - 1);
            std::reverse(abbr.begin(), abbr.end());
            os_prefix << "@prefix " << abbr << ": ";
            write_uri(os_prefix, ns.c_str());
            os_prefix << ".\n";
        }
        os_statement << abbr << ':' << p;
    }
}

int list_turtle(std::ostream &os, sqlite3_stmt *stmt)
{
    std::map<std::string, std::string> abbreviations;
    std::ostringstream oss;
    std::string last_subj, last_pred;

    int r;
    while((r = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        const char *subj = (char*)sqlite3_column_text(stmt, 0),
                   *pred = (char*)sqlite3_column_text(stmt, 1),
                   *obj  = (char*)sqlite3_column_text(stmt, 2),
                   *type = (char*)sqlite3_column_text(stmt, 3);

        if(last_subj != subj)
        {
            if(!oss.str().empty())
                (os << oss.str()).write(".\n", 2);
            oss.str("");
            last_subj.assign(subj);
            last_pred.assign(pred);

            // Write subject and predicate
            write_resource(oss, os, abbreviations, subj);
            oss.put(' ');
            write_resource(oss, os, abbreviations, pred);
        }
        else
        if(last_pred != pred)
        {
            oss.write(";\n\t", 3);
            last_pred.assign(pred);

            // Write predicate
            write_resource(oss, os, abbreviations, pred);
        }
        else
        {
            oss.put(',');
        }

        // Write object
        oss.put(' ');
        if(type == NULL)
            write_resource(oss, os, abbreviations, obj);
        else
        {
            write_string(oss, obj);
            if(*type)
            {
                oss.write("^^", 2);
                write_resource(oss, os, abbreviations, type);
            }
        }
    }
    
    // Write final statement
    if(!oss.str().empty())
        (os << oss.str()).write(".\n", 2);
    
    return r;
}

int main(int argc, char *argv[])
{
    if(argc != 3)
    {
        std::cout << "Usage: " << basename(argv[0]) << " <database> <model uri>" << std::endl;
        return 0;
    }
    const char *database_path = argv[1], *model_uri = argv[2];
    
    // Initialize sqlite
    sqlite3 *db;
    if(sqlite3_open(database_path, &db) != SQLITE_OK)
    {
        std::cerr << "Unable to open database!" << std::endl;
        return 1;
    }
    
    // Get model node
    nid_t model_nid = -1;
    {
        sqlite3_stmt *stmt =
            prepare(db, "SELECT oid FROM Node WHERE l=?1 AND d=0");
        if(stmt == NULL)
        {
            sqlite3_close(db);
            return 1;
        }
        sqlite3_bind_text(stmt, 1, model_uri, -1, NULL);
        int r = sqlite3_step(stmt);
        if(r == SQLITE_ROW)
            model_nid = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        
        if(r != SQLITE_ROW)
        {
            sqlite3_close(db);
            
            if(r == SQLITE_BUSY)
                std::cerr << "Database is busy!" << std::endl;
            else
            if(r != SQLITE_DONE)
                std::cerr << "Database error!\nsqlite: "
                          << sqlite3_errmsg(db) << std::endl;
                          
            return (r == SQLITE_DONE) ? 0 : 1;
        }
    }

    // List contents of model
    sqlite3_stmt *stmt = prepare( db,
        "SELECT "
        "(SELECT l FROM Node WHERE oid=s), "
        "(SELECT l FROM Node WHERE oid=p), "
        "(SELECT l FROM Node WHERE oid=o), "
        "(SELECT d.l FROM Node n JOIN Node d ON n.d = d.oid WHERE n.oid=o) "
        "FROM Quad WHERE m=?1" );
    if(stmt == NULL)
    {
        sqlite3_close(db);
        return 1;
    }
    sqlite3_bind_int64(stmt, 1, model_nid);
    int r = list_turtle(std::cout, stmt);
    if(r == SQLITE_BUSY)
        std::cerr << "Database is busy!" << std::endl;
    else
    if(r != SQLITE_DONE)
        std::cerr << "Database error!\nsqlite: "
                  << sqlite3_errmsg(db) << std::endl;
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return (r == SQLITE_DONE) ? 0 : 1;
}
