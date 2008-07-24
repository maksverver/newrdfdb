#include <algorithm>
#include <iostream>
#include <deque>
#include <vector>
#include <sqlite3.h>
#include <libgen.h>
#include "turtle_parser.h"

/* TODO
    - support for anonymous URI's
    - convert built-in types
*/

typedef long long int nid_t;


/* Built-in datatypes */
#define TYPE_URI        (0ll)
#define TYPE_LITERAL    (1ll)
#define TYPE_BOOLEAN    (2ll)
#define TYPE_INTEGER    (3ll)
#define TYPE_DATE_TIME  (4ll)
#define TYPE_FLOAT      (5ll)
#define TYPE_DOUBLE     (6ll)

struct Triple
{
    nid_t subj, pred, obj;
};

inline bool operator< (const Triple &q, const Triple &r);
inline bool operator== (const Triple &q, const Triple &r);
inline bool operator!= (const Triple &q, const Triple &r);


/*
 * SQL statements used.
 */

#define STATEMENTS 5

static const char * const statements[STATEMENTS] = {
#define SQL_FIND_NODE               ( 0)

    "SELECT oid FROM Node WHERE l=?1 AND d=?2",

#define SQL_ADD_NODE                ( 1)
    "INSERT INTO Node (l, d) VALUES (?1, ?2)",

#define SQL_ADD_QUAD                ( 2)
    "INSERT INTO Quad (m, s, p, o) VALUES (?1, ?2, ?3, ?4)",

#define SQL_REMOVE_QUAD             ( 3)
    "DELETE FROM Quad WHERE oid=?1",

#define SQL_LIST_QUADS              ( 4)
    "SELECT oid, s, p, o FROM Quad WHERE m=?1 ORDER BY s ASC, p ASC, o ASC",
};

static sqlite3 *db;
static sqlite3_stmt *stmts[STATEMENTS];
static nid_t model;
static std::deque<Triple> triples;
static std::deque<Triple> added;
static std::deque<nid_t> removed;

bool operator< (const Triple &q, const Triple &r)
{
    return q.subj < r.subj || (q.subj == r.subj && (q.pred < r.pred ||
        (q.pred == r.pred && q.obj < r.obj)));
}

bool operator== (const Triple &q, const Triple &r)
{
    return q.subj == r.subj && q.pred == r.pred && q.obj == r.obj;
}

bool operator!= (const Triple &q, const Triple &r)
{
    return q.subj != r.subj || q.pred != r.pred || q.obj != r.obj;
}


static nid_t nid(const char *lexical, nid_t datatype)
{
    nid_t id = -1;

    // Try to find existing node
    sqlite3_stmt *stmt = stmts[SQL_FIND_NODE];
    sqlite3_bind_text (stmt, 1, lexical, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, datatype);
    if(sqlite3_step(stmt) == SQLITE_ROW)
        id = sqlite3_column_int64(stmt, 0);
    sqlite3_reset(stmt);

    if(id == -1)
    {
        // Insert new node
        sqlite3_stmt *stmt = stmts[SQL_ADD_NODE];
        sqlite3_bind_text (stmt, 1, lexical, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, datatype);
        if(sqlite3_step(stmt) == SQLITE_DONE)
            id = sqlite3_last_insert_rowid(db);
        sqlite3_reset(stmt);
    }

    if(id == -1)
    {
        std::cerr << "Failed to generate node identifier for node "
                  << "\"" << lexical << "\" (" << datatype << ")\n"
                  << "sqlite: " << sqlite3_errmsg(db) << std::endl;
    }

    return id;
}

static int triple_handler( void *arg,
    const char *subject, const char *predicate, const char *object,
    const char *lexical, const char *datatype, const char *language )
{
    nid_t object_type;
    if(object == NULL)
        if(datatype == NULL)
            object_type = TYPE_LITERAL;
        else
            object_type = nid(datatype, TYPE_URI);
    else
        object_type = TYPE_URI;

    // Generate triple
    Triple t = { nid(subject, TYPE_URI),
                 nid(predicate, TYPE_URI),
                 nid(object ? object : lexical, object_type) };
    if(t.subj < 0 || t.pred < 0 || t.obj < 0)
        return 1; // Unable to generate node ID's; abort parsing.

    triples.push_back(t);
    return 0;
}

static int compare_triples()
{
    sqlite3_stmt *stmt = stmts[SQL_LIST_QUADS];
    sqlite3_bind_int64(stmt, 1, model);
    int r;
    while((r = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        Triple t = { sqlite3_column_int64(stmt, 1),
                     sqlite3_column_int64(stmt, 2),
                     sqlite3_column_int64(stmt, 3) };

        while(!triples.empty() && triples.front() < t)
        {
            added.push_back(triples.front());
            triples.pop_front();
        }
        if(!triples.empty() && triples.front() == t)
            triples.pop_front();
        else
            removed.push_back(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_reset(stmt);
    if(r != SQLITE_DONE)
        return 1;

    // Add remaining triples in model to 'added' list
    while(!triples.empty())
    {
        added.push_back(triples.front());
        triples.pop_front();
    }

    return 0;
}

static int update_triples()
{
    // Remove triples
    {
        sqlite3_stmt *stmt = stmts[SQL_REMOVE_QUAD];
        for( std::deque<nid_t>::const_iterator i = removed.begin();
             i != removed.end(); ++i )
        {
            sqlite3_bind_int64(stmt, 1, *i);
            int r = sqlite3_step(stmt);
            sqlite3_reset(stmt);
            if(r != SQLITE_DONE)
                return 1;
        }
    }

    // Add triples
    {
        sqlite3_stmt *stmt = stmts[SQL_ADD_QUAD];
        for( std::deque<Triple>::const_iterator
                i = added.begin(); i != added.end(); ++i )
        {
            sqlite3_bind_int64(stmt, 1, model);
            sqlite3_bind_int64(stmt, 2, i->subj);
            sqlite3_bind_int64(stmt, 3, i->pred);
            sqlite3_bind_int64(stmt, 4, i->obj);
            int r = sqlite3_step(stmt);
            sqlite3_reset(stmt);
            if(r != SQLITE_DONE)
                return 1;
        }
    }

    return 0;
}

static void finalize_sqlite()
{
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    for(int n = 0; n < STATEMENTS; ++n)
        sqlite3_finalize(stmts[n]);
    sqlite3_close(db);
}

static bool initialize_sqlite(const char *database_path, const char *model_uri)
{
    // Initialize sqlite
    if(sqlite3_open(database_path, &db) != SQLITE_OK)
    {
        std::cerr << "Unable to open database!" << std::endl;
        return false;
    }

    // Prepare statements
    for(int n = 0; n < STATEMENTS; ++n)
        if(sqlite3_prepare(db, statements[n], -1, &stmts[n], NULL) != SQLITE_OK)
        {
            std::cerr << "Unable to prepare statement: \"" << statements[n] << "\"!" << std::endl;

            while(n--)
                sqlite3_finalize(stmts[n]);
            sqlite3_close(db);

            return false;
        }

    // Start transaction
    if(sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL) != SQLITE_OK)
    {
        std::cerr << "Unable to begin transaction!" << std::endl;
        finalize_sqlite();
        return false;
    }

    // Create node id for model
    model = nid(model_uri, TYPE_URI);
    if(model == -1)
    {
        std::cerr << "Unable generate node id for model!" << std::endl;
        finalize_sqlite();
        return false;
    }

    return true;
}

int main(int argc, char *argv[])
{
    if(argc != 4)
    {
        std::cout << "Usage: " << basename(argv[0]) << " <database> <model uri> <model path>" << std::endl;
        return 0;
    }
    const char *database_path = argv[1], *model_uri = argv[2], *model_path = argv[3];

    // Open source file
    FILE *fp = std::fopen(model_path, "r");
    if(fp == NULL)
    {
        std::cerr << "Unable to open file \"" << model_path
                  << "\" for reading!" << std::endl;
        return 1;
    }

    // Initialize sqlite
    if(!initialize_sqlite(database_path, model_uri))
        return 1;

    std::cerr << "Reading model <" << model_uri << ">"
              << " from file \"" << model_path << "\"... " << std::flush;

    // Step 1: read quads from input file
    if(parse_turtle(fp_reader, (void*)fp, triple_handler, NULL) != 0)
    {
        std::cerr << "\nParsing failed!" << std::endl;
        finalize_sqlite();
        return 1;
    }
    if( sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK ||
        sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL) != SQLITE_OK )
    {
        std::cerr << "\nUnable to commit and restart transaction!\n" 
                  << "sqlite: " << sqlite3_errmsg(db) << std::endl;
        finalize_sqlite();
        return 1;
    }
    std::cerr << "done.\n\t" << triples.size() << " triples read." << std::endl;

    // Step 2: sort and remove duplicates
    std::cerr << "Sorting... " << std::flush;
    size_t dups = 0;
    {
        std::sort(triples.begin(), triples.end());
        std::deque<Triple>::iterator i =
            std::unique(triples.begin(), triples.end());
        if(i != triples.end())
        {
            dups = triples.size();
            triples.erase(i, triples.end());
            dups -= triples.size();
        }
    }
    std::cerr << "done." << std::endl;
    if(dups > 0)
    {
        std::cerr << "\tWARNING: " << dups
                  << " duplicate triples removed!" << std::endl;
    }

    // Step 3: compare with stored model
    std::cerr << "Comparing with database... " << std::flush;
    if(compare_triples() != 0)
    {
        std::cerr << "\nUnable to read triples from database!\n"
                  << "sqlite: " << sqlite3_errmsg(db) << std::endl;
        finalize_sqlite();
        return 1;
    }
    std::cerr << "done.\n"
              << '\t' << removed.size() << " triples to be removed;\n"
              << '\t' << added.size()   << " triples to be added.\n"
              << std::flush;

    // Step 4: update database
    std::cerr << "Updating database... " << std::flush;
    if(update_triples() != 0)
    {
        std::cerr << "\nUnable to write updates to database!\n"
                  << "sqlite: " << sqlite3_errmsg(db) << std::endl;
        finalize_sqlite();
        return 1;
    }
    std::cerr << "done." << std::endl;

    // Step 5: commit transaction
    std::cerr << "Committing transaction... " << std::flush;
    if(sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
    {
        std::cerr << "Unable to commit transaction!\n" 
                  << "sqlite: " << sqlite3_errmsg(db) << std::endl;
        finalize_sqlite();
        return 1;
    }
    std::cerr << "done." << std::endl;

    finalize_sqlite();
    return 0;
}
