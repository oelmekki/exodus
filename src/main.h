#ifndef _MAIN_H_
#define _MAIN_H_

#define MAX_PATH_LEN 2000
#define MAX_NAME_LEN 200
#define MAX_FILE_LEN 5 * 1024 * 1024 // 5MB
#define MAX_OBJECT_LEN 5 * 1024 * 100 // 500KB

typedef struct {
  char database[MAX_PATH_LEN];
  char migrations[MAX_PATH_LEN];
  char structure[MAX_PATH_LEN];
  char recreate[MAX_NAME_LEN];
  char migration_name[MAX_NAME_LEN];
  int command;
} options_t;

enum {
  UNKNOWN_COMMAND,
  COMMAND_GENERATE,
  COMMAND_MIGRATE,
};

#endif
