#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <pwd.h>
#include <grp.h>
#include "system.h"
#include "config.h"

#define SUPERUSER (uid_t)0
int flag_daemonize = 0;
static int pidfile_fd;
static char pidfile_str[256];

volatile pthread_t main_pid, signal_pid, 
    watcher_pid, scanner_pid, writer_pid; 

void staylocal(config_t *conf, char **argv) {
    int ret;
    // don't daemonize - but write pid file and enable logging to stdout
    snprintf(pidfile_str, 256, "/tmp/%u-testy.pid", conf->port);
   
    pidfile_fd = open(pidfile_str, O_RDONLY);
    if (pidfile_fd > 0) {
	    // pidfile exists - check if process is running.
	    // if no process running, delete pid file and keep going,
	    // otherwise exit with failure.
	char pid_str[16] = { 0 };
	ret = read(pidfile_fd, &pid_str, 16);
	close(pidfile_fd);
	pid_t prior = atoi(pid_str);
	ret = kill(prior, 0);
	if (ret == -1 && errno == ESRCH) {
		// no process - safe to delete pid and continue
	    ret = unlink(pidfile_str);
	} else {
            perror("process already running.  Terminating.");
	    exit(EXIT_FAILURE);
	}
    }

    pidfile_fd = open(pidfile_str, O_WRONLY | O_CREAT  | O_EXCL ,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );
    if (pidfile_fd < 0) {
        perror("error creating pidfile: ");
        exit(EXIT_FAILURE);
    }    
    char pid_str[10];
    snprintf(pid_str, 10, "%d", getpid()); 
    ret = write(pidfile_fd, pid_str, strlen(pid_str) + 1);
    openlog(NULL, LOG_PERROR, LOG_USER);
    
}

void daemonize(config_t *conf, char **argv) {
    pid_t pid, sid;
    int ret;
    snprintf(pidfile_str, 256, "/tmp/%u-testy.pid", conf->port);
    pidfile_fd = open(pidfile_str, O_RDONLY);
    if (pidfile_fd > 0) { 
// a pidfile exists already
// if we are executed as root, send a SIGTERM to
// the prior running process so we can run
// otherwise if we are a regular use, exit.
        if (geteuid() == SUPERUSER) {
            char pid_str[10] = { 0 };
            pidfile_fd = open(pidfile_str, O_RDONLY);
            ret = read(pidfile_fd, &pid_str, 10);   
            
            close(pidfile_fd);
            pid_t prior = atoi(pid_str);
            fprintf(stderr, "Trying to terminate prior instance [%d] ...\n",
                    prior);
            kill(prior, SIGTERM);
            close(pidfile_fd);
            sleep(2);              // wait...
            daemonize(conf, argv); // try again
            return;
        } else {
            fprintf(stderr, "pidfile '%s' exists, terminating.\n", pidfile_str);
            exit(EXIT_FAILURE);
        }
    } 
// daemonize
// fork twice to completely dissasociate from the calling session
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    sid = setsid();
    if (sid < 0) exit(EXIT_FAILURE);
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
// if we're run as superuser, lower priveledge to the user 
// specified in config file, or default to NOBODY.
// whatever user specified should be able to write to the db file
    if (geteuid() == SUPERUSER) {
        struct passwd *pwd = getpwnam(conf->userid);
        uid_t uid = pwd->pw_uid;
        gid_t gid = pwd->pw_gid;
        initgroups(conf->userid, gid);
        ret = setegid(gid);
        ret = seteuid(uid);
    }
    umask(0);
// now that we are a regular user, attempt to create pid (lock) file.
// failure means the process is running,
// or crashed in a way that left the pid file around.
    pidfile_fd = open(pidfile_str, O_WRONLY | O_CREAT  | O_EXCL ,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );
    if (pidfile_fd < 0) {
        perror("error creating pidfile: ");
        exit(EXIT_FAILURE);
    }    
// now that we have our final pid, write it to the pid file.
    char pid_str[10];
    snprintf(pid_str, 10, "%d", getpid()); 
    ret = write(pidfile_fd, pid_str, strlen(pid_str) + 1);
    if ((chdir("/")) < 0) exit(EXIT_FAILURE);
// close standard files by redirecting to /dev/null
    int nullfd = open("/dev/null", O_RDWR); 
    dup2(nullfd, STDIN_FILENO);
    dup2(nullfd, STDOUT_FILENO);
    dup2(nullfd, STDERR_FILENO);
    close(nullfd);
// from this point out, we use syslog() to output messages
    openlog(argv[0], LOG_PID | LOG_CONS, LOG_USER);
}

void cleanup() {
    close(pidfile_fd);
    remove(pidfile_str);
    syslog(LOG_INFO, "exiting.\n");
    closelog();
}

static void handle_signal(int sig) {
    LOGGER(LOG_INFO, "received signal %d - %s\n", sig, strsignal(sig));
    if (sig == SIGKILL || sig == SIGTERM || sig == SIGSTOP || sig == SIGINT) {
        LOGGER(LOG_INFO, "terminating...\n");
        // cancelling the signal thread will cause a graceful shutdown
        pthread_cancel(signal_pid);
    }
    if (sig == SIGHUP) {
        // we want to stop current tests but not exit
    }
}

static void signal_cleanup(void *arg) {
    // we are terminating because of a signal
    // inform the other threads to terminate.
    LOGGER(LOG_INFO, "cancelling threads...\n");

    pthread_cancel(main_pid);

    pthread_join(main_pid, NULL);
    
    

    exit(EXIT_SUCCESS);
}


static void *signal_thread(void *arg) {
    int cleanup_pop_val;
    signal_pid = pthread_self();
    pthread_detach(signal_pid);
    pthread_cleanup_push(signal_cleanup, NULL);
    struct sigaction act = {0};
    act.sa_handler = handle_signal;
    for (int i = 0; i < 32; i++)
        sigaction(i, &act, NULL);
    sigset_t mask;
    sigemptyset(&mask);
    int sig;
    while(1) {
        sigsuspend(&mask);
    }

    pthread_cleanup_pop(cleanup_pop_val);
}

void assign_signal_handler() {
    // first create the signal handler while no signals are blocked
    pthread_create((pthread_t *)&signal_pid, NULL, &signal_thread, NULL);
    pthread_detach(signal_pid);
    // now block all signals - all subsequent threads also will block
    sigset_t mask;
    sigfillset(&mask);
    sigprocmask(SIG_SETMASK, &mask, NULL);

}

