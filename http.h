#ifndef __HTTP_H__
#define __HTTP_H__
#include <evhtp/evhtp.h>

#define SERVER_IDENTIFIER "RJW/1.0"

// thread-pooling using libevhtp thread-design.c as a template

typedef struct app_parent {
    evhtp_t  *htp;
    evbase_t *base;
    config_t *config;
} app_parent;

typedef struct app {
    int          fd;    
    app_parent   *parent;
    evbase_t     *base;
    config_t     *config;
} app;

void app_init_thread(evhtp_t *htp, evthr_t *thread, void *arg);
void precompile_statements(void *arg);
void app_term_thread(evhtp_t *htp, evthr_t *thread, void *arg);
void register_callbacks(evhtp_t *evhtp);
#endif
