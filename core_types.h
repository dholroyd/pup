#ifndef _CORE_TYPES_H
#define _CORE_TYPES_H

struct PupObject;

struct RuntimeEnv;
#define ENV struct RuntimeEnv *env

typedef struct PupObject *(PupMethod)(ENV, struct PupObject *, long, struct PupObject **);

struct PupClass;

#endif
