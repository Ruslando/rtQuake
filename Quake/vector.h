#pragma once

#ifndef __VECTOR_H
#define __VECTOR_H

#include <stdio.h>
#include <stdlib.h>
#define VECTOR_INIT_CAPACITY 6
#define UNDEFINE  -1
#define SUCCESS 0
#define VECTOR_INIT(vec) vector vec;\
 vector_init(&vec)
//Store and track the stored data
typedef struct sVectorList
{
    void** items;
    int capacity;
    int total;
} sVectorList;
//structure contain the function pointer
typedef struct sVector vector;
struct sVector
{
    sVectorList vectorList;
    //function pointers
    int (*pfVectorTotal)(vector*);
    int (*pfVectorResize)(vector*, int);
    int (*pfVectorPushBackReference)(vector*, void*);
    int (*pfVectorPushBackValue)(vector*, void*, size_t);
    int (*pfVectorSet)(vector*, int, void*);
    void* (*pfVectorGet)(vector*, int);
    int (*pfVectorDelete)(vector*, int);
    int (*pfVectorFree)(vector*);
};

int vectorTotal(vector* v);
int vectorResize(vector* v, int capacity);
int vectorPushBackReference(vector* v, void* item);
int vectorPushBackValue(vector* v, void* item, size_t size);
int vectorSet(vector* v, int index, void* item);
void* vectorGet(vector* v, int index);
int vectorDelete(vector* v, int index);
int vectorFree(vector* v);
void vector_init(vector* v);

#endif	/* __VECTOR_H */