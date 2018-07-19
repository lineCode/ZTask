#ifndef ztask_daemon_h
#define ztask_daemon_h

int daemon_init(const char *pidfile);
int daemon_exit(const char *pidfile);

#endif
