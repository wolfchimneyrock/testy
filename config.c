#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include "confuse.h"
#include "config.h"

config_t config;

#define DEFAULT_INT(x, v) { \
    if (x == -1) x = v;     \
}

#define DEFAULT_STR(x, v) { \
    if (!x) x = strdup(v);  \
}

void get_config(config_t *config, char *cfg_file) {
	cfg_opt_t opts[] = {
		CFG_SIMPLE_STR("uid",          &(config->userid)),
		CFG_SIMPLE_INT("port",         &(config->port)),
		CFG_SIMPLE_INT("threads",      &(config->threads)),
		CFG_SIMPLE_INT("timeout",      &(config->timeout)),
		CFG_END()
	};
	cfg_t *cfg;
    

    // use sane defaults if not already set by cmdline parameters

    DEFAULT_INT(config->port,     8111);
    DEFAULT_INT(config->threads,  1);
    DEFAULT_INT(config->timeout,  1800);
    DEFAULT_INT(config->fullscan, 0);
    DEFAULT_INT(config->buffercap, 100);
    DEFAULT_INT(config->verbose,  0);

        // DAAPPER_DBFILE
    DEFAULT_STR(config->dbfile, "/tmp/songs.db");

	char *cfg_path = cfg_file ? cfg_file : "/etc/testy.conf";
    printf("Using config file '%s'\n", cfg_file);
	setlocale(LC_MESSAGES, "");
	setlocale(LC_CTYPE, "");

	cfg = cfg_init(opts, 0);
	cfg_parse(cfg, cfg_path);

}


