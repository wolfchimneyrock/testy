#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include "evhtp/evhtp.h"
#include <event2/util.h>
#include <time.h>
#include <unistd.h>
#include <syslog.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <getopt.h>
#include "config.h"
#include "util.h"
#include "http.h"
#include "system.h"

/**
 * @brief this is called automatically when the main thread is cancelled
 */
static void main_cleanup(void *arg) {
    app_parent *parent = (app_parent *)arg;
    event_base_loopbreak(parent->base);
    evhtp_unbind_socket(parent->htp);
    evhtp_free(parent->htp);
    event_base_free(parent->base);
    LOGGER(LOG_INFO, "main thread terminated.");
}

static const char option_string[]  = "DVc:d:s:p:t:T:B:";
static struct option long_options[] = {
    { "daemonize",          no_argument,       0,       'D' },
    { "verbose",            no_argument,       0,       'V' },
    { "config",             required_argument, 0,       'c' },
    { "db-file",            required_argument, 0,       'd' },
    { "share-directory",    required_argument, 0,       's' },
    { "port",               required_argument, 0,       'p' },
    { "threads",            required_argument, 0,       't' },
    { "timeout",            required_argument, 0,       'T' },
    { "buffer-capacity",    required_argument, 0,       'B' },
    { 0, 0, 0, 0 }
};

#define INTARG(a, desc) {                                               \
      a = atoi(optarg);                                                 \
      if (a == 0) {                                                     \
          fprintf(stderr, "--%s must be passed an integer.\n", desc);   \
          exit(1);                                                      \
      }                                                                 \
}

config_t conf;

int main (int argc, char *argv[]) {
    char config_file[256] = "daapper.conf";
    char hostname[255];
    gethostname(hostname, 255);

// empty config values.  if not set by cmdline args, then will be set by get_config()
    conf.dbfile       = NULL;
    conf.extfile      = NULL;
    conf.port         = -1;
    conf.threads      = -1;
    conf.timeout      = -1;
    conf.buffercap    = -1;
    conf.verbose      = -1;
    conf.root         = NULL;
    conf.userid       = NULL;
    conf.fullscan     = -1;
    conf.server_name  = hostname;
    conf.library_name = NULL;

// process cmdline args
    while(1) {
        int option_index = 0;
        int c = getopt_long(argc, argv, option_string, long_options, &option_index);
        if (c == -1) break;
        switch(c) {
            case 'D': flag_daemonize = 1;        
                      break;
            case 'V': conf.verbose = 1;
                      break;
            case 'c': {
                          size_t len = strlen(optarg) + 1;
                          if (len > 256) len = 256;
                          strncpy(config_file, optarg, len);
                      }
                      break;
            case 'd': {
                          conf.dbfile = strdup(optarg);
                      }
                      break;
            case 's': conf.root = strdup(optarg);
                      break;
            case 'p': INTARG(conf.port, "port");
                      break;
            case 't': INTARG(conf.threads, "threads");
                      break;
            case 'T': INTARG(conf.timeout, "timeout");
                      break;
            case 'B': INTARG(conf.buffercap, "buffer-capacity");
                      break;
            default:
                      exit(1);
        }
    }

    main_pid = pthread_self();
    int res;
    evbase_t *evbase;
    evhtp_t  *evhtp;
    app_parent parent;

    get_config(&conf, config_file);

// unix specific initialization in system.c
// TBD windows version
    if (flag_daemonize)
        daemonize(&conf, argv); 
    else
        staylocal(&conf, argv);

    assign_signal_handler();
    atexit(cleanup);
    int cleanup_pop_val;
    pthread_cleanup_push(main_cleanup, &parent);


// WRITER thread:
// this is the only thread with write access to the database
// other threads must request writes by adding a query request
// to a global ringbuffer
//    pthread_create((pthread_t *)&writer_pid, NULL, &writer_thread, &conf);

    
// WATCHER thread:
// this is the thread that watches for library changes,
// and submits updates to the SCANNER thread
//    pthread_create((pthread_t *)&watcher_pid, NULL, &watcher_thread, &conf);

// SCANNER thread:
// this is the thread that scans mpeg files for metadata
//    pthread_create((pthread_t *)&scanner_pid, NULL, &scanner_thread, &conf);

// this maybe should be moved to WRITER thread initialization?
// this maintains "clean" and "dirty" status of the database
//    db_init_status();

// reset global DAAP sessions
//    sessions_init();

// the rest of this is boilerplate LIBEVENT / EVHTP for a multi 
// threaded, thread-pooling HTTP server.  Application specific 
// callbacks are located in http.c
    LOGGER(LOG_INFO, "initialize event base...");
    parent.base = event_base_new();
    LOGGER(LOG_INFO, "initialize evhtp base...");
    parent.htp  = evhtp_new(parent.base, NULL);
    parent.config = &conf;
    register_callbacks(parent.htp);
    evhtp_use_threads_wexit(parent.htp, app_init_thread, app_term_thread, 
                            conf.threads, &parent);
    int socket = 0;
    if (argc > 1) socket = atoi(argv[1]);
    if (socket == 0) socket = conf.port;
    LOGGER(LOG_INFO, "binding socket %d...", socket);
    if ((res = evhtp_bind_socket(parent.htp, "0.0.0.0", socket, 2048)) 
            == -1) {
        LOGGER(LOG_EMERG, "failed to bind socket error %d.", errno);
        evhtp_free(parent.htp);
        event_base_free(parent.base);
        exit(EXIT_FAILURE);
    }
    LOGGER(LOG_INFO, "event loop...");
    event_base_loop(parent.base, 0);
    pthread_cleanup_pop(cleanup_pop_val);
    return EXIT_SUCCESS;
}
