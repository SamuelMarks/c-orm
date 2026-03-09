#ifndef FAKE_LIBPQ_FE_H
#define FAKE_LIBPQ_FE_H

typedef struct PGconn PGconn;
typedef struct PGresult PGresult;
typedef enum { CONNECTION_OK = 0, CONNECTION_BAD } ConnStatusType;
typedef enum {
  PGRES_COMMAND_OK = 0,
  PGRES_TUPLES_OK,
  PGRES_BAD_RESPONSE
} ExecStatusType;
typedef unsigned int Oid;

PGconn *PQconnectdb(const char *conninfo);
ConnStatusType PQstatus(const PGconn *conn);
void PQfinish(PGconn *conn);
PGresult *PQexec(PGconn *conn, const char *query);
PGresult *PQexecParams(PGconn *conn, const char *command, int nParams,
                       const Oid *paramTypes, const char *const *paramValues,
                       const int *paramLengths, const int *paramFormats,
                       int resultFormat);
ExecStatusType PQresultStatus(const PGresult *res);
void PQclear(PGresult *res);

#endif
