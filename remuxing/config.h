#ifndef CONFIG_H
#define CONFIG_H

#ifndef INT64_C
#ifndef UINT64_C
# if __WORDSIZE == 64
#  define INT64_C(c)    c ## L
#  define UINT64_C(c)   c ## UL
# else
#  define INT64_C(c)    c ## LL
#  define UINT64_C(c)   c ## ULL
# endif
#endif // UINT64_C
#endif // INT64_C

#endif // CONFIG_H
