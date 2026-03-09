#ifndef FAKE_MYSQL_H
#define FAKE_MYSQL_H

typedef struct MYSQL {
  int dummy;
} MYSQL;
typedef struct MYSQL_STMT {
  int dummy;
} MYSQL_STMT;
typedef struct MYSQL_BIND {
  int dummy;
} MYSQL_BIND;
typedef char my_bool;

MYSQL *mysql_init(MYSQL *mysql);
MYSQL *mysql_real_connect(MYSQL *mysql, const char *host, const char *user,
                          const char *passwd, const char *db, unsigned int port,
                          const char *unix_socket, unsigned long clientflag);
void mysql_close(MYSQL *sock);
int mysql_query(MYSQL *mysql, const char *q);

MYSQL_STMT *mysql_stmt_init(MYSQL *mysql);
int mysql_stmt_prepare(MYSQL_STMT *stmt, const char *query,
                       unsigned long length);
int mysql_stmt_bind_param(MYSQL_STMT *stmt, MYSQL_BIND *bnd);
int mysql_stmt_execute(MYSQL_STMT *stmt);
my_bool mysql_stmt_close(MYSQL_STMT *stmt);

#endif
