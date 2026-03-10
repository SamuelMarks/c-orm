/* clang-format off */
#ifdef C_ORM_HAVE_POSTGRES
#include <libpq-fe.h>
#endif

#ifdef C_ORM_HAVE_MYSQL
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <mysql.h>
#endif

#include <stddef.h>
/* clang-format on */

#ifdef C_ORM_HAVE_POSTGRES
PGconn *PQconnectdb(const char *conninfo) {
  (void)conninfo;
  return NULL;
}
ConnStatusType PQstatus(const PGconn *conn) {
  (void)conn;
  return CONNECTION_BAD;
}
void PQfinish(PGconn *conn) { (void)conn; }
PGresult *PQexec(PGconn *conn, const char *query) {
  (void)conn;
  (void)query;
  return NULL;
}
PGresult *PQexecParams(PGconn *conn, const char *command, int nParams,
                       const Oid *paramTypes, const char *const *paramValues,
                       const int *paramLengths, const int *paramFormats,
                       int resultFormat) {
  (void)conn;
  (void)command;
  (void)nParams;
  (void)paramTypes;
  (void)paramValues;
  (void)paramLengths;
  (void)paramFormats;
  (void)resultFormat;
  return NULL;
}
ExecStatusType PQresultStatus(const PGresult *res) {
  (void)res;
  return PGRES_BAD_RESPONSE;
}
void PQclear(PGresult *res) { (void)res; }
#endif

#ifdef C_ORM_HAVE_MYSQL
MYSQL *mysql_init(MYSQL *mysql) { return mysql ? mysql : (MYSQL *)1; }
MYSQL *mysql_real_connect(MYSQL *mysql, const char *host, const char *user,
                          const char *passwd, const char *db, unsigned int port,
                          const char *unix_socket, unsigned long clientflag) {
  (void)mysql;
  (void)host;
  (void)user;
  (void)passwd;
  (void)db;
  (void)port;
  (void)unix_socket;
  (void)clientflag;
  return NULL;
}
void mysql_close(MYSQL *sock) { (void)sock; }
int mysql_query(MYSQL *mysql, const char *q) {
  (void)mysql;
  (void)q;
  return -1;
}

MYSQL_STMT *mysql_stmt_init(MYSQL *mysql) {
  (void)mysql;
  return NULL;
}
int mysql_stmt_prepare(MYSQL_STMT *stmt, const char *query,
                       unsigned long length) {
  (void)stmt;
  (void)query;
  (void)length;
  return -1;
}
int mysql_stmt_bind_param(MYSQL_STMT *stmt, MYSQL_BIND *bnd) {
  (void)stmt;
  (void)bnd;
  return -1;
}
int mysql_stmt_execute(MYSQL_STMT *stmt) {
  (void)stmt;
  return -1;
}
my_bool mysql_stmt_close(MYSQL_STMT *stmt) {
  (void)stmt;
  return 0;
}
#endif
