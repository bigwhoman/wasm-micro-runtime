/*
  MIT License

  Copyright (c) [2023] [Arjun Ramesh]

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#ifndef WALI_COPY_H
#define WALI_COPY_H

#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <signal.h>
#include <setjmp.h>

#include "wali.h"
#include "../interpreter/sigtable.h"

/** Memory Copy Macros **/
#define WR_FIELD(wptr, val, ty)         \
    ({                                  \
        memcpy(wptr, &val, sizeof(ty)); \
        wptr += sizeof(ty);             \
    })

#define WR_FIELD_ADDR(wptr, nptr)              \
    ({                                         \
        uint32_t wasm_addr = WADDR(nptr);      \
        if (!wasm_addr) {                      \
            VB("NULL Wasm Address generated"); \
        }                                      \
        WR_FIELD(wptr, wasm_addr, uint32_t);   \
    })

#define WR_FIELD_ARRAY(wptr, narr, ty, num)   \
    ({                                        \
        memcpy(wptr, narr, sizeof(ty) * num); \
        wptr += (sizeof(ty) * num);           \
    })

#define RD_FIELD(ptr, ty)              \
    ({                                 \
        ty val;                        \
        memcpy(&val, ptr, sizeof(ty)); \
        ptr += sizeof(ty);             \
        val;                           \
    })

#define RD_FIELD_ADDR(ptr)                        \
    ({                                            \
        uint32_t field = RD_FIELD(ptr, uint32_t); \
        MADDR(field);                             \
    })

#define RD_FIELD_ARRAY(dest, ptr, ty, num)    \
    ({                                        \
        memcpy(&dest, ptr, sizeof(ty) * num); \
        ptr += (sizeof(ty) * num);            \
    })
/** **/

/** Debug Macro **/
#define PRINT_BYTES(var, num)           \
    {                                   \
        printf(#var " bytes: ");        \
        char *v = (char *)var;          \
        for (int i = 0; i < num; i++) { \
            printf("%02X ", v[i]);      \
        }                               \
        printf("\n");                   \
    }

/* Copy pselect6 sigmask structure */
extern inline void *
copy_pselect6_sigmask(wasm_exec_env_t exec_env, Addr wasm_psel_sm,
                      long *sm_struct)
{
    /* Libc stores the address in a long (64-bit). Cannot use RD_FIELD_ADDR
     * since it reads 32-bit values */
    long sigmask_addr = RD_FIELD(wasm_psel_sm, long);
    sm_struct[0] = (long)MADDR(sigmask_addr);
    sm_struct[1] = RD_FIELD(wasm_psel_sm, long);
    return sm_struct;
}

/* Copy iovec structure */
extern inline struct iovec *
copy_iovec(wasm_exec_env_t exec_env, Addr wasm_iov, int iov_cnt)
{
    if (wasm_iov == NULL) {
        return NULL;
    }
    struct iovec *new_iov =
        (struct iovec *)malloc(iov_cnt * sizeof(struct iovec));
    for (int i = 0; i < iov_cnt; i++) {
        new_iov[i].iov_base = RD_FIELD_ADDR(wasm_iov);
        new_iov[i].iov_len = RD_FIELD(wasm_iov, int32_t);
    }
    return new_iov;
}

/* Copy epoll_event structure */
extern inline struct epoll_event *
copy_epoll_event(wasm_exec_env_t exec_env, Addr wasm_epoll,
                 struct epoll_event *n_epoll)
{
    if (wasm_epoll == NULL) {
        return NULL;
    }
    n_epoll->events = RD_FIELD(wasm_epoll, uint32_t);
    n_epoll->data.u64 = RD_FIELD(wasm_epoll, uint64_t);
    return n_epoll;
}

extern inline void
copy2wasm_epoll_event(wasm_exec_env_t exec_env, Addr wasm_epoll,
                      struct epoll_event *n_epoll)
{
    if (n_epoll == NULL) {
        return;
    }
    WR_FIELD(wasm_epoll, n_epoll->events, uint32_t);
    WR_FIELD(wasm_epoll, n_epoll->data.u64, uint64_t);
    return;
}

/* Copy msghdr structure */
extern inline struct msghdr *
copy_msghdr(wasm_exec_env_t exec_env, Addr wasm_msghdr)
{
    if (wasm_msghdr == NULL) {
        return NULL;
    }
    struct msghdr *msg = (struct msghdr *)malloc(sizeof(struct msghdr));
    msg->msg_name = RD_FIELD_ADDR(wasm_msghdr);
    msg->msg_namelen = RD_FIELD(wasm_msghdr, unsigned);

    Addr wasm_iov = RD_FIELD_ADDR(wasm_msghdr);
    msg->msg_iovlen = RD_FIELD(wasm_msghdr, int);

    RD_FIELD(wasm_msghdr, int); // pad1

    msg->msg_control = RD_FIELD_ADDR(wasm_msghdr);
    msg->msg_controllen = RD_FIELD(wasm_msghdr, unsigned);

    RD_FIELD(wasm_msghdr, int); // pad2
    msg->msg_flags = RD_FIELD(wasm_msghdr, int);

    msg->msg_iov = copy_iovec(exec_env, wasm_iov, msg->msg_iovlen);
    return msg;
}

// ASM restorer function '__libc_restore_rt'.
extern void
__libc_restore_rt();

/* Copy sigaction back to WASM */
extern inline void
copy2wasm_old_ksigaction(int signo, Addr wasm_act, struct k_sigaction *act)
{
    FuncPtr_t old_wasm_funcptr;
    if (act->handler == SIG_DFL) {
        old_wasm_funcptr = WASM_SIG_DFL;
    }
    else if (act->handler == SIG_IGN) {
        old_wasm_funcptr = WASM_SIG_IGN;
    }
    else if (act->handler == SIG_ERR) {
        old_wasm_funcptr = WASM_SIG_ERR;
    }
    else {
        old_wasm_funcptr = wali_sigtable[signo].func_table_idx;
        VB("Save old sigaction handler -- Tbl[%d]", old_wasm_funcptr);
    }
    WR_FIELD(wasm_act, old_wasm_funcptr, FuncPtr_t);
    WR_FIELD(wasm_act, act->flags, unsigned long);
    WR_FIELD(wasm_act, act->restorer, FuncPtr_t);
    WR_FIELD_ARRAY(wasm_act, act->mask, unsigned, 2);
}

/* Copy sigaction to native: Function pointers are padded */
extern inline struct k_sigaction *
copy_ksigaction(wasm_exec_env_t exec_env, Addr wasm_act,
                struct k_sigaction *act, void (*common_handler)(int),
                FuncPtr_t *target_wasm_funcptr, char *debug_str)
{
    if (wasm_act == NULL) {
        return NULL;
    }

    FuncPtr_t wasm_handler_funcptr = RD_FIELD(wasm_act, FuncPtr_t);
    if (wasm_handler_funcptr == (FuncPtr_t)(WASM_SIG_DFL)) {
        act->handler = SIG_DFL;
        strcpy(debug_str, "SIG_DFL");
    }
    else if (wasm_handler_funcptr == (FuncPtr_t)(WASM_SIG_IGN)) {
        act->handler = SIG_IGN;
        strcpy(debug_str, "SIG_IGN");
    }
    else if (wasm_handler_funcptr == (FuncPtr_t)(WASM_SIG_ERR)) {
        act->handler = SIG_ERR;
        strcpy(debug_str, "SIG_ERR");
    }
    else {
        /* Setup common handler */
        act->handler = common_handler;
        *target_wasm_funcptr = wasm_handler_funcptr;
        strcpy(debug_str, "Wasm SIG");
    }

    act->flags = RD_FIELD(wasm_act, unsigned long);

    RD_FIELD(wasm_act, FuncPtr_t);
    act->restorer = __libc_restore_rt;

    RD_FIELD_ARRAY(act->mask, wasm_act, unsigned, 2);
    return act;
}

/* Copy sigstack structure */
extern inline stack_t *
copy_sigstack(wasm_exec_env_t exec_env, Addr wasm_sigstack, stack_t *ss)
{
    if (!wasm_sigstack) {
        return NULL;
    }
    ss->ss_sp = RD_FIELD_ADDR(wasm_sigstack);
    ss->ss_flags = RD_FIELD(wasm_sigstack, int);
    ss->ss_size = RD_FIELD(wasm_sigstack, uint32_t);
    return ss;
}

/* Copy array of strings (strings are not malloced)*/
extern inline char **
copy_stringarr(wasm_exec_env_t exec_env, Addr wasm_arr)
{
    if (!wasm_arr) {
        return NULL;
    }
    int num_strings = 0;
    /* Find num elems */
    Addr arr_it = wasm_arr;
    char *str;
    while ((str = (char *)RD_FIELD_ADDR(arr_it))) {
        num_strings++;
    }
    char **stringarr = (char **)malloc((num_strings + 1) * sizeof(char *));
    for (int i = 0; i < num_strings; i++) {
        stringarr[i] = (char *)RD_FIELD_ADDR(wasm_arr);
    }
    stringarr[num_strings] = NULL;
    return stringarr;
}

extern _Noreturn void
__libc_longjmp_asm(__libc_sigjmp_buf, int);
extern int
__libc_sigsetjmp_asm(__libc_sigjmp_buf, int);
#define __libc_siglongjmp __libc_longjmp_asm

/* Copy jmpbuf struct to WASM for setjmp */
extern inline void
copy2wasm_jmp_buf(wasm_exec_env_t exec_env, Addr wasm_buf,
                  struct __libc_jmp_buf_tag *buf)
{
    WR_FIELD_ARRAY(wasm_buf, buf->__jb, unsigned long, 8);
    WR_FIELD(wasm_buf, buf->__fl, unsigned long);
    WR_FIELD_ARRAY(wasm_buf, buf->__ss, unsigned long, (128 / sizeof(long)));
}

/* Copy jmpbuf struct to native for setjmp */
extern inline struct __libc_jmp_buf_tag *
copy_jmp_buf(wasm_exec_env_t exec_env, Addr wasm_jmp_buf)
{
    if (!wasm_jmp_buf) {
        return NULL;
    }
    struct __libc_jmp_buf_tag *buf =
        (struct __libc_jmp_buf_tag *)malloc(sizeof(struct __libc_jmp_buf_tag));
    RD_FIELD_ARRAY(buf->__jb, wasm_jmp_buf, unsigned long, 8);
    buf->__fl = RD_FIELD(wasm_jmp_buf, unsigned long);
    RD_FIELD_ARRAY(buf->__ss, wasm_jmp_buf, unsigned long,
                   (128 / sizeof(long)));
    return buf;
}

/** Architecture-specific copies **/
#if __has_include("copy_arch.h")
#include "copy_arch.h"
#endif

#endif
