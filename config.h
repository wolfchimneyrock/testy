#ifndef __CONFIG_H__
#define __CONFIG_H__

typedef struct config_t {
    long   port;
    long   threads;
    long   fullscan;
    long   timeout;
    long   buffercap;
    long   verbose;
    char *name;
    char *root;
    char *dbfile;
    char *extfile;
    char *server_name;
    char *library_name;
    char *userid;
} config_t;

extern config_t config;

void get_config(config_t *config, char *cfg_file);
#endif
