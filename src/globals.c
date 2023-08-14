#include "mscompress.h"

int verbose = 0;
int fds[3] = {-1, -1, -1};
long fd_pos[3] = {0, 0, 0};