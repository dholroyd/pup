#include <stdio.h>

#define ABORTF(format, args...) do { fprintf(stderr, "%s(%s:%d) abort: " format "\n", __func__, __FILE__, __LINE__, ## args); abort(); } while (0)

#define ABORTF_ON(condition, format, args...) do { if (condition) { fprintf(stderr, "%s(%s:%d) abort: " format "\n", __func__, __FILE__, __LINE__, ## args); abort(); } } while (0)

#define ABORT_ON(condition, message) do { if (condition) { fprintf(stderr, "%s(%s:%d) abort: %s\n", __func__, __FILE__, __LINE__, message); abort(); } } while (0)
