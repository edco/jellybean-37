/*
	setjmp.h
	stubs for future use.
*/

#ifndef _SETJMP_H_
#define _SETJMP_H_

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"

#include "_ansi.h"
#include <machine/setjmp.h>

_BEGIN_STD_C

#ifdef __GNUC__
void	_EXFUN(longjmp, (jmp_buf __jmpb, int __retval))
__attribute__((__noreturn__));
#else
void	_EXFUN(longjmp, (jmp_buf __jmpb, int __retval));
#endif
int	_EXFUN(setjmp, (jmp_buf __jmpb));

_END_STD_C

#pragma GCC diagnostic pop

#endif /* _SETJMP_H_ */

