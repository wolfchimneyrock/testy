#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fcntl.h>
#include <string.h>
#include <event2/util.h>
#include <evhtp/evhtp.h>
#include "config.h"
#include "util.h"
#include "system.h"
#include "client.h"

#define META_SCRATCH_SIZE    4096
#define PATH_SCRATCH_SIZE    4096 
#define CLIENT_BUFSIZE       600
#define BUFSIZE              256
#define TOKENSIZE            64
#define PIPEBUF_SIZE         4096 // 4k is largest atomic read size
///////////////////////////////////////////////////////////////////////////////

struct _client {
    int           id;
    int           pid;
    int           messages_sent;
    int           pipe; // pipe file descriptor
    int           ready;
    int           stopsig;
    int           deleted;
    ev_t         *event;
    evbuf_t      *buffer;
    void         *req;
    char         *ready_message;
    //evbuf_ptr  last_eol;
    //int           eol_size;
};

static CLIENT          *clients[CLIENT_BUFSIZE];
static pthread_mutex_t  clients_mutex            = PTHREAD_MUTEX_INITIALIZER;
static int              client_count             = 0;

int clients_add(CLIENT *c) {
    if (c == NULL) return -1;
    if (client_count == CLIENT_BUFSIZE) return -1;

    int result;
    LOGGER(LOG_INFO, "clients_add()\n");
    LOGGER(LOG_INFO, "    pid: %d\n", c->pid);
   
    pthread_mutex_lock(&clients_mutex);

        result = client_count++;
        clients[result] = c;
        c->id = result;
    pthread_mutex_unlock(&clients_mutex);
    
    return result;
}

int client_stop(CLIENT *c) {
    if (c == NULL) return -1;
    c->deleted = 1;
    syslog(LOG_INFO, "sending signal %d to pid %d", c->stopsig, c->pid);
    event_del(c->event);
    int ret = kill(c->pid, c->stopsig);
    waitpid(c->pid, NULL, 0); 
    clients[c->id] = NULL;
    event_free(c->event);
    //free(c);

    return ret;
}

CLIENT *clients_get(int clientid) {
   if (clientid < client_count)
       return clients[clientid]; 
   else
       return NULL;
}

int read_client_pipe_lines(evbuf_t *out, CLIENT *c) {
    syslog(LOG_INFO, "read_client_pipe_lines()\n");
    if (out == NULL || c == NULL) return -1;
    int result = -1;
    if (0 == evbuffer_add_buffer(out, c->buffer)) {
        result = c->messages_sent++;
    }
    return result;
}

CLIENT * parse_client_post_body(char *body, size_t body_len, void *base) {
    // this will parse the json body that goes with a POST request
    // if successful, it allocates and returns a CLIENT object
    // to cleanup the client object, run close_client(c)

    LOGGER(LOG_INFO, "parse_client_post_body()\n");
    int res;
    int i;
    size_t cmdlen    = 0;
    size_t rdylen    = 0;
    size_t durlen    = 0;
    size_t stoplen   = 0;
    char  *cmdtag    = NULL;
    char  *rdytag    = NULL;
    char  *durtag    = NULL;
    char  *stoptag   = NULL;

    jsmn_parser p;
    jsmntok_t tokens[TOKENSIZE];

    jsmn_init(&p);
    syslog(LOG_INFO, "body: %s", body);
    res = jsmn_parse(&p, body, body_len, tokens, 
            sizeof(tokens) / sizeof(tokens[0]));
    LOGGER(LOG_INFO, "body contains %d JSON objects...", res);
    if (res < 0) {
        // error parsing JSON
        return NULL;
    }
/*
    if (res < 1 || tokens[0].type != JSMN_OBJECT) {
        // expected root-level object, but not found
        return NULL;
    }
*/
    for (i = 0; i < res; i++) {
        // loop over all children of the root object
        
        if (json_key_eq(body, &tokens[i], "Cmdline")) {
            // what follows should be the execution string
            if (i + 1 == res) {
                return NULL;
                // no matching value for key
            }
            cmdtag = body + tokens[++i].start;
            cmdlen = (size_t)(tokens[i].end - tokens[i].start);
            cmdtag[cmdlen] = '\0';
            LOGGER(LOG_INFO, "got Cmdline tag: '%s'", cmdtag);
        }

        else if (json_key_eq(body, &tokens[i], "ReadyMessage")) {
            if (i + 1 == res) return NULL;
            rdytag = body + tokens[++i].start;
            rdylen = (size_t)(tokens[i].end - tokens[i].start);
            rdytag[rdylen] = '\0';
            LOGGER(LOG_INFO, "got ReadyMessage tag: '%s'", rdytag);
        }

        else if (json_key_eq(body, &tokens[i], "Duration")) {
            if (i + 1 == res) return NULL;
            durtag = body + tokens[++i].start;
            durlen = (size_t)(tokens[i].end - tokens[i].start);
            durtag[durlen] = '\0';
            LOGGER(LOG_INFO, "got Duration tag: '%s'", durtag);
        }
        
        else if (json_key_eq(body, &tokens[i], "StopSignal")) {
            if (i + 1 == res) return NULL;
            stoptag = body + tokens[++i].start;
            stoplen = (size_t)(tokens[i].end - tokens[i].start);
            stoptag[stoplen] = '\0';
            LOGGER(LOG_INFO, "got StopSignal tag: '%s'", durtag);
        }

    }

    if (cmdtag && *cmdtag) {
        // we need the command key to be meaningful
        return create_client(cmdtag, rdytag, stoptag, base);
    }
    return NULL;

}


CLIENT *create_client(char *command, const char *ready_message, const char *stop_signal, void *base) {
// command is mutated by tokenize() to null-terminate tokens
    LOGGER(LOG_INFO, "create_client()\n");
    int p[2];
    int res = pipe2(p, O_NONBLOCK);
    if (res < 0) return NULL;

    char **list = NULL;
    list = tokenize(list, command, " \t\n");

    int childdup = STDERR_FILENO; // we want to read from pipe

    int child;
    int stopsig;

    if ((child = fork()) == 0) {  
        // we are the child, try to execute
        dup2(p[1], childdup);
        close(p[1]);
        close(p[0]);
        if (execv(*list, list) == -1 &&
            execvp(*list, list) == -1) { 
            // execute failed, clean up
                LOGGER(LOG_INFO, "failed to execute %s", *list);
                free(list);
                return NULL;
        }
    } else
    if (child < 0) {  
        // fork failed, clean up
        close(p[1]);
        close(p[0]);
        free(list);
        return NULL;
    }
    else {
        // we are the parent - save the child information and return it
        close(p[1]);
        free(list);
        CLIENT *c = malloc(sizeof(CLIENT));
        c->pipe   = p[0];  // fdopen(p[1-childdup], "r");
        c->pid    = child;
        c->deleted = 0;
        c->buffer = evbuffer_new();
        evbuffer_enable_locking(c->buffer, NULL);
        
        // create libevent event for fifo 
        c->event = event_new(base, c->pipe, EV_READ|EV_PERSIST,
                client_fifo_read, c);
        if (c->event) {
            LOGGER(LOG_INFO, "fifo event created.");
            event_add(c->event, NULL);
        } 
        c->messages_sent = 0;
        if (ready_message && *ready_message) {
            c->ready_message = strdup(ready_message);
            c->ready = 0;
        } else {
            c->ready_message = NULL;
            c->ready = 1;
        }
        stopsig = atoi(stop_signal);
        if (stopsig > 0) {
            c->stopsig = stopsig;
        } else {
            syslog(LOG_INFO, "using default stop signal 9.");
            c->stopsig = 9;
        }
        return c;
    }
}
   
void client_fifo_read(int fd, short event, void *arg) {
    CLIENT *c = (CLIENT *)arg;
    if (c == NULL)  return;
    if (c->deleted) return;
    int          count       = 0;
    int          total_read  = 0;
    do {
        evbuffer_expand(c->buffer, PIPEBUF_SIZE);
        count = evbuffer_read(c->buffer, fd, PIPEBUF_SIZE); 
        if (!c->ready) {
            // we want to search for ready_message
            size_t size = 0;
            do {
                char *line = evbuffer_readln(c->buffer, &size, EVBUFFER_EOL_CRLF);
                if (line == NULL) break;
                char *found = strstr(line, c->ready_message);
                if (found != NULL) {
                    c->ready = 1;
                    LOGGER(LOG_INFO, "found ready message!");
                    evhtp_send_reply(c->req, EVHTP_RES_CREATED);
                } 
                free(line);
            } while (!c->ready && size > 0); 
        }
        
        //syslog(LOG_INFO, "client_fifo_read() %d\n", count);
        
        if (count > 0) { 
            total_read += count;
        } 
    } while (count == PIPEBUF_SIZE);

    if (count < 0) {
        // error reading
        LOGGER(LOG_INFO, "    error reading from client");
        
    }
}

void client_respond_when_ready(CLIENT *c, void *req) {
    if (c == NULL) return;
    c->req = req;
}

int client_isready(CLIENT *c) {
    if (c == NULL) return 0;
    return c->ready;
}

int close_client(CLIENT *c) {
    syslog(LOG_INFO, "close_client()\n");
   if (c == NULL) return -1; 

   int status;
   waitpid(c->pid, &status, 0);
   if (!WIFEXITED(status)) {
       close(c->pipe);
       return WEXITSTATUS(status);
   }
   int result = close(c->pipe);
   if (c->ready_message) free(c->ready_message);
   free(c);
   return result;
}

