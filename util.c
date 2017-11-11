#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <netinet/in.h>
#include "util.h"

static const char *DAY_NAMES[] =
  { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *MONTH_NAMES[] =
  { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

void timestamp_rfc1123(char *buf) {
    time_t now;
    struct tm ts;
    time(&now);
    gmtime_r(&now, &ts);
    strftime(buf, 30, "---, %d --- %Y %H:%M:%S GMT", &ts);
    memcpy(buf, DAY_NAMES[ts.tm_wday], 3);
    memcpy(buf+8, MONTH_NAMES[ts.tm_mon], 3);
}

int count_char(char *buf, char ch) {
    if (buf == NULL) return 0;
    int i = 0, res = 0;
    while (buf[i]) if (buf[i++] == ch) ++res;
    return res;
}

int uri_get_number(const char *uri, const int index) {
    int pos = 0;
    int i = 0;
    if (!uri || uri[0] == '\0') return -1;
    int len = strlen(uri);
    while (pos < len && i < index) {
        while (pos < len && (uri[pos] < '0' || uri[pos] > '9')) pos++;
        // now we're at first number
        while (pos < len && i < index) {
            while(pos < len && (uri[pos] >= '0'&& uri[pos] <='9')) pos++;
            while(pos < len && (uri[pos] < '0' || uri[pos] > '9')) pos++;
            i++;
        }
    }
    return (pos < len && i == index) ? atoi(uri + pos) : -1;
}
    
void printable_ipaddress(struct sockaddr_in *addr, char *buf) {
    snprintf(buf, 16, "%d.%d.%d.%d", 
            (int)((addr->sin_addr.s_addr & 0xFF      )      ),
            (int)((addr->sin_addr.s_addr & 0xFF00    ) >>  8),
            (int)((addr->sin_addr.s_addr & 0xFF0000  ) >> 16),
            (int)((addr->sin_addr.s_addr & 0xFF000000) >> 24));
}


char *split_filename(char *path) {
    if (path == NULL || path[0] == '\0') return NULL;
    
    int pos = 0;
    char *last = NULL;
    while (path[pos] != '\0') {
        if (path[pos] == '/')
            last = path + pos;
        pos++;
    }
    if (last != NULL)
        *last++ = '\0';
    return last;
}
        
size_t token_count(const char *src, const char *delimiters) {
    size_t count = 0;   ///< keeps a running count of tokens found
    char lastWS = 1;    ///< flag: "was the last character whitespace?"

    while (*src) {
        if (strchr(delimiters, *src)) 
            lastWS = 1;
        else {
            if (lastWS) ++count;
            lastWS = 0;
        }
        ++src;
    }
    return count;
}

char ** tokenize(char **dest, char *src, const char *delimiters) {
    size_t N = token_count(src, delimiters);

    // we need one extra (N+1) for trailing NULL to create a valid C array
    dest = (char **)realloc(dest, sizeof(char *) * (1 + N));
    
    // modified from original to use re-entrant strtok_r
    char *current = src;
    dest[0] = strtok_r(current, delimiters, &current);
    for (size_t i = 1; i < N; i++)
        dest[i] = strtok_r(current, delimiters, &current);
    dest[N] = NULL;
    return dest;
}

int json_key_eq(const char *json, jsmntok_t *t, const char *s) { 
    if (t == NULL) return 0;
    size_t len = (size_t)(t->end - t->start);
    if (t->type == JSMN_STRING && 
                   strlen(s) == len &&
                   strncmp(json + t->start, s, len) == 0) return 1;
    return 0;
}

int last_delim_pos(char *buf, int size, char delim) {
    if (buf == NULL || size <= 0) return -1;
    int pos = size - 1;
    while (pos >= 0 && buf[pos--] != delim);
    return pos;
}

