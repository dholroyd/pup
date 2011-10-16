

#define ABORTF(format, args...) do { fprintf(stderr, "%s:%d " format "\n", __FILE__, __LINE__, ## args); abort(); } while (0)

#define ABORTF_ON(condition, format, args...) do { if (condition) { fprintf(stderr, "%s:%d " format "\n", __FILE__, __LINE__, ## args); abort(); } } while (0)

#define ABORT_ON(condition, message) do { if (condition) { fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, message); abort(); } } while (0)
