#ifndef ASSERTS_H_INCLUDED
#define ASSERTS_H_INCLUDED

#include <stdio.h>

#define ASSERT_S(what, text...) { if (!(what)) fprintf(stderr, text); }
#define ASSERT(what) if (!(what)) { fprintf(stderr, "Assert failed (%s:%d)\n", __FILE__, __LINE__); }

#endif
