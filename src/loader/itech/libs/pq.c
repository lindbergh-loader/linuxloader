// libpq.so.3 (scripts/linux/build-occ-libs.sh): PostgreSQL's client API as
// Orange County Choppers Pinball calls it, with no server. The cabinet's
// database held ITnet's message queues (the online network) and, by
// default, the persistence layer, which the loader moves to its file store
// (src/loader/itech/itech.c). Every connection fails: ITnet stays offline.

#include <stdlib.h>
#include <string.h>

typedef struct
{
    int unused;
} PGconn;

typedef struct
{
    int unused;
} PGresult;

#define CONNECTION_BAD 1
#define PGRES_FATAL_ERROR 7

static PGconn badConnection;

PGconn *PQconnectdb(const char *conninfo)
{
    (void)conninfo;
    return &badConnection;
}

int PQstatus(const PGconn *conn)
{
    (void)conn;
    return CONNECTION_BAD;
}

int PQsocket(const PGconn *conn)
{
    (void)conn;
    return -1;
}

int PQbackendPID(const PGconn *conn)
{
    (void)conn;
    return 0;
}

char *PQerrorMessage(const PGconn *conn)
{
    (void)conn;
    return "no database server\n";
}

void PQfinish(PGconn *conn)
{
    (void)conn;
}

PGresult *PQexec(PGconn *conn, const char *query)
{
    (void)conn;
    (void)query;
    return NULL;
}

int PQresultStatus(const PGresult *res)
{
    (void)res;
    return PGRES_FATAL_ERROR;
}

void PQclear(PGresult *res)
{
    (void)res;
}

char *PQcmdTuples(PGresult *res)
{
    (void)res;
    return "";
}

int PQntuples(const PGresult *res)
{
    (void)res;
    return 0;
}

char *PQgetvalue(const PGresult *res, int row, int column)
{
    (void)res;
    (void)row;
    (void)column;
    return "";
}

int PQgetisnull(const PGresult *res, int row, int column)
{
    (void)res;
    (void)row;
    (void)column;
    return 1;
}

int PQgetlength(const PGresult *res, int row, int column)
{
    (void)res;
    (void)row;
    (void)column;
    return 0;
}

// bytea's escapes: freed by the caller (PQfreemem did not exist yet: free).
unsigned char *PQescapeBytea(const unsigned char *from, size_t length, size_t *toLength)
{
    (void)from;
    (void)length;
    *toLength = 1;
    return calloc(1, 1);
}

unsigned char *PQunescapeBytea(const unsigned char *from, size_t *toLength)
{
    (void)from;
    *toLength = 0;
    return calloc(1, 1);
}
