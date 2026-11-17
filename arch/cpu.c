#include <arch/cpu.h>

struct cpu harts[NCPU];

void init_cpu(unsigned long hartid)
{
	struct cpu *hart = &harts[hartid];
	hart->hartid = hartid;
	__asm__("mv tp, %[cpu]" : : [cpu] "r"(hart));
}
