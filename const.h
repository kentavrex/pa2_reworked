#ifndef CONST_H
#define CONST_H

#include <stdint.h>
#include "banking.h"

typedef struct {
    int fd[2];
} Pipe;

static const int ERR = 1;

static const short WRITE = 1;

static const int OK = 0;

typedef struct {
    long num_process;
    Pipe** pipes;
    int8_t pid;
    timestamp_t last_time;
    balance_t balance;
    BalanceHistory history;
} Process;

static const short READ = 0;

#endif
