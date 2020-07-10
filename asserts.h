#ifndef ASSERTS_H_INCLUDED
#define ASSERTS_H_INCLUDED

#ifdef DEBUG
	#include <stdio.h>

	#define EXPECT(expr) \
		{ if (!(expr)) fprintf(stderr, "EXPECT failed (%s:%d): %s\n", __FILE__, __LINE__, #expr); }

	#define EXPECT_S(expr, text...) \
		{ if (!(expr)) { \
			fprintf(stderr, "EXPECT failed (%s:%d): ", __FILE__, __LINE__); \
			fprintf(stderr, text); \
			fprintf(stderr, "\n");}}

	#define EXPECT_EQUAL(left, right) \
		{ long long l = (long long)(left), r = (long long)(right); \
			if (l != r) fprintf(stderr, "EXPECT_EQUAL failed (%s:%d): %lld != %lld\n",\
				__FILE__, __LINE__, l, r); }
#else
	#define EXPECT(expr) {expr;}
	#define EXPECT_S(expr, text...) {expr;}
	#define EXPECT_EQUAL(left, right) {(left) == (right);}
#endif

#endif
