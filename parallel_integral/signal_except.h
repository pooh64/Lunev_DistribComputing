#ifndef SIGNAL_EXCEPT_H_
#define SIGNAL_EXCEPT_H_

#include <setjmp.h>

/* Signal handler restore buffer */
extern jmp_buf sig_exc_buf;

#endif /* SIGNAL_EXCEPT_H_ */