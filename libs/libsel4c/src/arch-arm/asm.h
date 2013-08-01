/* @LICENSE(NICTA_CORE) */

#ifndef __L4__ARM__ASM_H__
#define __L4__ARM__ASM_H__

#define BEGIN_PROC(name)			\
    .global name; 				\
    .align;					\
name:

#define END_PROC(name)				\
    ;

#endif /* __L4__ARM__ASM_H__ */
