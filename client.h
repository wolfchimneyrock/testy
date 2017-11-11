#ifndef __CLIENT_H__
#define __CLIENT_H__

typedef struct _client CLIENT;

int      clients_add(CLIENT *c);
CLIENT * clients_get(int clientid);
int      read_client_pipe_lines(struct evbuffer *out, CLIENT *c);
CLIENT * parse_client_post_body(char *body, size_t body_len, void *base);
CLIENT * create_client(char *command, const char *ready_message, void *base);
int      client_isready(CLIENT *c);
void     client_respond_when_ready(CLIENT *c, void *req);
int      close_client(CLIENT *c);
int      wait_for_client_ready(CLIENT *c, const char *ready_message);
void     client_fifo_read(int fd, short event, void *arg);

#endif
