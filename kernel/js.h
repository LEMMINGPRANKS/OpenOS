#ifndef OPENOS_JS_H
#define OPENOS_JS_H

#include <stdint.h>

// OpenJS: the OpenOS JavaScript-subset interpreter. A tree-walker in the
// kernel (no libc). Numbers are 64-bit integers, strings are heap-copied
// (never freed -- scripts are small), and runaway scripts are stopped by a
// step budget so `while(1){}` can't lock up the machine.
//
// Language: var/let/const, assignment, + - * / %, == != < > <= >= !,
// if/else, while, for(init;cond;step), function name(a,b){...}, return,
// break, continue, // comments, print(...) and console.log(...).

// Run source. Returns 0 on success; on failure returns -1 and copies a
// message (with line number) into err.
int js_run(const char *src, uint32_t len, char *err, int errmax);

#endif
