struct harbor;

struct harbor *harbor_create(void);
void
harbor_release(struct harbor *h);
int
harbor_init(struct harbor *h, struct ztask_context *ctx, const char * args, const size_t sz);
