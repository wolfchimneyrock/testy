#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <event2/util.h>
#include <evhtp/evhtp.h>
#include <syslog.h>
#include "config.h"
#include "http.h"
#include "util.h"
#include "client.h"

static const int  current_rev = 2;
#define API_V1_STR "/v1"
#define CLIENT_STR "/clients"
#define REG_NUM "/([0-9]+)"
#define REG_UUID "/([0-9a-f]{32})"
#define BODYSIZE 1024
#define BUFSIZE  4096

#define ADD_DATE_HEADER(txt) \
    char date_now[30]; timestamp_rfc1123(date_now);\
    evhtp_headers_add_header(req->headers_out, \
    evhtp_header_new(txt, date_now, 0, 0));

static evthr_t *get_request_thr(evhtp_request_t * request) {
    evhtp_connection_t * htpconn;
    evthr_t            * thread;

    htpconn = evhtp_request_get_connection(request);
    thread  = htpconn->thread;

    return thread;
}

void log_request(evhtp_request_t *req, void *a) {
    char ipaddr[16];
    printable_ipaddress((struct sockaddr_in *)req->conn->saddr, ipaddr);
    const char *ua  = evhtp_kv_find(req->headers_in, "User-Agent");
    syslog(LOG_INFO, "%s [%s] requested %s: %s%s\n", 
            ipaddr, ua, (char *)a, req->uri->path->path,
            req->uri->path->file);
    evhtp_kv_t *kv;
}

void app_init_thread(evhtp_t *htp, evthr_t *thread, void *arg) {
// per thread init here: open db, etc
    app_parent *parent = (app_parent *)arg;
    app *aux = malloc(sizeof(app));
    aux->parent = parent;
    aux->base   = evthr_get_base(thread);
    aux->config = parent->config;
// to be retrieved by request callbacks that need
    evthr_set_aux(thread, aux); 
    syslog(LOG_INFO, "evhtp thread listening for connections.\n");
}

void app_term_thread(evhtp_t *htp, evthr_t *thread, void *arg) {
    app *aux = (app *)evthr_get_aux(thread);
    free(aux);
    syslog(LOG_INFO, "evhtp thread terminated.\n");
}

void add_headers_out(evhtp_request_t *req) {
    evhtp_headers_add_header(req->headers_out,
          evhtp_header_new("Access-Control-Allow-Origin", "*", 0, 0));
    evhtp_headers_add_header(req->headers_out,
          evhtp_header_new("Cache-Control", "no-cache", 0, 0));
    evhtp_headers_add_header(req->headers_out,
          evhtp_header_new("Keep-Alive", "timeout=120", 0, 0));
    evhtp_headers_add_header(req->headers_out,
          evhtp_header_new("Connection", "keep-alive", 0, 0));
}

// generic response for unassigned uri
void server_xmlrpc(evhtp_request_t *req, void *a) {
    const char *str = a;
    log_request(req, a);
    evhtp_send_reply(req, EVHTP_RES_OK);
    syslog(LOG_INFO, "received request %s: %s - %s\n", 
            (char *)a, req->uri->path->path, req->uri->path->file); 
}



void res_agent_clients(evhtp_request_t *req, void *a) {
    log_request(req, a);
    add_headers_out(req);
    ADD_DATE_HEADER("Date");

    evthr_t *thread = get_request_thr(req);
    app     *aux    = (app *)evthr_get_aux(thread);
    CLIENT  *c      = NULL;
    evbase_t *base  = aux->base;
    
    // check method type:
    // GET - respond to a scan with OK
    // PUT - change some general parameter?
    // POST - create a new client
    //        should return a clientid in response if successful
    switch(req->method) {
        case htp_method_GET: 
            
            // return OK status
            // lets a scanner know we're accepting connections
            
            evhtp_send_reply(req, EVHTP_RES_OK);

            break;
        case htp_method_PUT:

            // change agent-level parameter

            break;
        case htp_method_POST: {
            syslog(LOG_INFO, "handling POST request\n"); 
            // create a new test
            // the test parameters should be in the body of the request
            // we will send a response only after we get ready confirmation 
            // we need to parse the body
            // copy out of the evbuffer since we don't know
            // if it is contiguous, plus we can mutate the copy,
            // note: subsequence methods should try not to copy again

            char body[BODYSIZE];
            size_t len = evbuffer_remove(req->buffer_in, body, BODYSIZE);
            if (len > 0) {
                c = parse_client_post_body(body, len, base);

            }
            else {
                // error - required body not present
                evhtp_send_reply(req, EVHTP_RES_BADREQ);
                break;
            }

            if (c == NULL) {
                // error - required body not present
                evhtp_send_reply(req, EVHTP_RES_BADREQ);
                break;
            }

            // at this point, client should be started and ready
            // so add to global array 
            
            int clientid = clients_add(c);
            if (clientid < 0) {
                // error
                evhtp_send_reply(req, EVHTP_RES_BADREQ);
                break;
            }

            syslog(LOG_INFO, "created client task %d\n", clientid);
            // we respond with a Location header
            // and 201 CREATED code
            char location[BODYSIZE];
            snprintf(location, BODYSIZE, API_V1_STR CLIENT_STR "/%d", clientid);
            evhtp_headers_add_header(req->headers_out,
                  evhtp_header_new("Location", location, 0, 0));
            if (client_isready(c))
                evhtp_send_reply(req, EVHTP_RES_CREATED);
            else
                client_respond_when_ready(c, req);
            
        } break;
        
    }
}

void res_agent_specific_client(evhtp_request_t *req, void *a) {
    log_request(req, a);
    add_headers_out(req);
    ADD_DATE_HEADER("Date");
    evthr_t *thread = get_request_thr(req);
    app     *aux    = (app *)evthr_get_aux(thread);
   

    int id = atoi(req->uri->path->file);
    // check if specific clientid exists first
    CLIENT *c = clients_get(id);

    if (c) {
        // successfully got it
        syslog(LOG_INFO, "found client object: %d", id);

    } else {
        evhtp_send_reply(req, EVHTP_RES_NOTFOUND);
        return;
    }

    // check method type:
    // GET - status update
    // PUT - start / stop the test 
    // DELETE - delete the test
    switch(req->method) {
        case htp_method_GET: {
            syslog(LOG_INFO, "handling GET request");
            // we want to read from the pipe and respond with contents
            int      messageid = 0;
            messageid = read_client_pipe_lines(req->buffer_out, c);  
            if (messageid >= 0) {
                char msgid[16];
                snprintf(msgid, 16, "%d", messageid);
                evhtp_headers_add_header(req->headers_out,
                      evhtp_header_new("Message-id", msgid, 0, 0));
                evhtp_send_reply(req, EVHTP_RES_OK);
            } else {
                evhtp_send_reply(req, EVHTP_RES_NOCONTENT);
            }


        } break;
        case htp_method_PUT:
            break;
        case htp_method_DELETE:

            break;

        
    }
}

void register_callbacks(evhtp_t *evhtp) {
    syslog(LOG_INFO, "registering callbacks...\n");

// regex callbacks must be registered in correct order: most specific first
    evhtp_set_regex_cb(evhtp, 
                   API_V1_STR CLIENT_STR REG_NUM,
                   res_agent_specific_client, 
                   "specific client request");

    evhtp_set_cb(evhtp, 
                   API_V1_STR CLIENT_STR,
                   res_agent_clients, 
                   "general clients request");

// static callbacks can be registered in any order

}
