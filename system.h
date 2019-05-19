#define system apl_system
#include <stdlib.h>
#undef system
#ifdef __cplusplus
extern "C"
#endif
int system(const char *);
