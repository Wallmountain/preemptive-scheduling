#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ucontext.h>
#include "list.h"

static int preempt_count = 0;
static void preempt_disable(void) {
    preempt_count++;
}
static void preempt_enable(void) {
    preempt_count--;
}

static void local_irq_save(sigset_t *sig_set) {
    sigset_t block_set;
    sigfillset(&block_set);
    sigdelset(&block_set, SIGINT);
    sigprocmask(SIG_BLOCK, &block_set, sig_set);
}

static void local_irq_restore(sigset_t *sig_set) {
    sigprocmask(SIG_SETMASK, sig_set, NULL);
}


#define task_printf(...)     \
    ({                       \
        preempt_disable();   \
        printf(__VA_ARGS__); \
        preempt_enable();    \
    })

static void timer_create(unsigned int usecs) {
    ualarm(usecs, usecs);
}

static void timer_cancel(void) {
    ualarm(0, 0);
}

static void re_schedule(void) {
    sigset_t set;
    local_irq_save(&set);

    printf("signal\n");

    local_irq_restore(&set);
}

static void timer_handler(int signo, siginfo_t *info, ucontext_t *ctx) {
    if(preempt_count)
        return;

    re_schedule();
}

static void timer_init(void) {
    struct sigaction sa = {
        .__sigaction_handler = (void (*)(int)) timer_handler,
        .sa_flags = SA_SIGINFO};
    sigfillset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
}

struct task {
    jmp_buf env;
    struct list_head list;
};

static LIST_HEAD(tasklist);
static void (**tasks)(void *);
static int ntasks;
static jmp_buf sched;

static void task_add(struct list_head *tasklist, jmp_buf env)
{
    struct task *t = malloc(sizeof(*t));
    memcpy(t->env, env, sizeof(jmp_buf));
    INIT_LIST_HEAD(&t->list);
    list_add_tail(&t->list, tasklist);
}

static void task_switch(struct list_head *tasklist)
{
    jmp_buf env;

    if (!list_empty(tasklist)) {
        struct task *t = list_first_entry(tasklist, struct task, list);
        list_del(&t->list);
        memcpy(env, t->env, sizeof(jmp_buf));
        free(t);
        longjmp(env, 1);
    }
}

static void task_join(struct list_head *tasklist)
{
    jmp_buf env;

    while (!list_empty(tasklist)) {
        struct task *t = list_first_entry(tasklist, struct task, list);
        list_del(&t->list);
        memcpy(env, t->env, sizeof(jmp_buf));
        free(t);
        longjmp(env, 1);
    }
}

void schedule(void)
{
    static int i;

    srand(0xCAFEBABE ^ (uintptr_t) &schedule); /* Thanks to ASLR */

    setjmp(sched);

    while (ntasks-- > 0) {
        int n = rand() % 5;
        tasks[i++](&n);
        printf("Never reached\n");
    }

    task_join(&tasklist);
}

/* A task yields control n times */

void task0(void *arg)
{
    jmp_buf env;
    static int n;
    n = *(int *) arg;

    printf("Task 0: n = %d\n", n);

    if (setjmp(env) == 0) {
        task_add(&tasklist, env);
        longjmp(sched, 1);
    }

    static int i = 0;
    for (; i < n; i++) {
        if (setjmp(env) == 0) {
            task_add(&tasklist, env);
            task_switch(&tasklist);
        }
        printf("Task 0: resume %d\n", i);
    }

    printf("Task 0: complete %d\n", n);
    longjmp(sched, 1);
}

void task1(void *arg)
{
    jmp_buf env;
    static int n;
    n = *(int *) arg;

    printf("Task 1: n = %d\n", n);

    if (setjmp(env) == 0) {
        task_add(&tasklist, env);
        longjmp(sched, 1);
    }

    static int i = 0;
    for (; i < n; i++) {
        if (setjmp(env) == 0) {
            task_add(&tasklist, env);
            task_switch(&tasklist);
        }
        printf("Task 1: resume %d\n", i);
    }

    printf("Task 1: complete %d\n", n);
    longjmp(sched, 1);
}

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
int main(void)
{
    timer_init();

    void (*registered_task[])(void *) = {task0, task1};
    tasks = registered_task;
    ntasks = ARRAY_SIZE(registered_task);

    preempt_disable();
    timer_create(1000); /* 10 ms */
    preempt_enable();

    schedule();

    preempt_enable();
    timer_cancel();

    return 0;
}
