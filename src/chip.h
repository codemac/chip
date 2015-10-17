#ifndef __CHIP_H_
#define __CHIP_H_

/* spawn a new coroutine */
void spawn(void (start)(void*), void *data);

/* yield to the scheduler */
void sched(void);

/* chip defines main() */
int main(void);

#endif /* __CHIP_H_ */