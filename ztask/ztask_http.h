#ifndef ZTASK_HTTP_H
#define ZTASK_HTTP_H

void ztask_http_init();
int ztask_http_poll(void *p);
void *ztask_http_create();

#endif //ZTASK_HTTP_H