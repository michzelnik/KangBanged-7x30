#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "helpers/helpers.h"

static const char *cpu_vendor_table[X86_VENDOR_MAX] = {
	"Unknown", "GenuineIntel", "AuthenticAMD",
};

#if defined(__i386__) || defined(__x86_64__)

/* from gcc */
#include <cpuid.h>

/*
 * CPUID functions returning a single datum
 *
 * Define unsigned int cpuid_e[abcd]x(unsigned int op)
 */
#define cpuid_func(reg)					\
	unsigned int cpuid_##reg(unsigned int op)	\
	{						\
	unsigned int eax, ebx, ecx, edx;		\
	__cpuid(op, eax, ebx, ecx, edx);		\
	return reg;					\
	}
cpuid_func(eax);
cpuid_func(ebx);
cpuid_func(ecx);
cpuid_func(edx);

#endif /* defined(__i386__) || defined(__x86_64__) */

/* get_cpu_info
 *
 * Extract CPU vendor, family, model, stepping info from /proc/cpuinfo
 *
 * Returns 0 on success or a negativ error code
 *
 * TBD: Should there be a cpuid alternative for this if /proc is not mounted?
 */
int get_cpu_info(unsigned int cpu, struct cpupower_cpu_info *cpu_info)
{
	FILE *fp;
	char value[64];
	unsigned int proc, x;
	unsigned int unknown = 0xffffff;
	unsigned int cpuid_level, ext_cpuid_level;

	int ret = -EINVAL;

	cpu_info->vendor		= X86_VENDOR_UNKNOWN;
	cpu_info->family		= unknown;
	cpu_info->model			= unknown;
	cpu_info->stepping		= unknown;
	cpu_info->caps			= 0;

	fp = fopen("/proc/cpuinfo", "r");
	if (!fp)
		return -EIO;

	while (!feof(fp)) {
		if (!fgets(value, 64, fp))
			continue;
		value[63 - 1] = '\0';

		if (!strncmp(value, "processor\t: ", 12))
			sscanf(value, "processor\t: %u", &proc);

		if (proc != cpu)
			continue;

		/* Get CPU vendor */
		if (!strncmp(value, "vendor_id", 9)) {
			for (x = 1; x < X86_VENDOR_MAX; x++) {
				if (strstr(value, cpu_vendor_table[x]))
					cpu_info->vendor = x;
			}
		/* Get CPU family, etc. */
		} else if (!strncmp(value, "cpu family\t: ", 13)) {
			sscanf(value, "cpu family\t: %u",
			       &cpu_info->family);
		} else if (!strncmp(value, "model\t\t: ", 9)) {
			sscanf(value, "model\t\t: %u",
			       &cpu_info->model);
		} else if (!strncmp(value, "stepping\t: ", 10)) {
			sscanf(value, "stepping\t: %u",
			       &cpu_info->stepping);

			/* Exit -> all values must have been set */
			if (cpu_info->vendor == X86_VENDOR_UNKNOWN ||
			    cpu_info->family == unknown ||
			    cpu_info->model == unknown ||
			    cpu_info->stepping == unknown) {
				ret = -EINVAL;
				goto out;
			}

			ret = 0;
			goto out;
		}
	}
	ret = -ENODEV;
out:
	fclose(fp);
	/* Get some useful CPU capabilities from cpuid */
	if (cpu_info->vendor != X86_VENDOR_AMD &&
	    cpu_info->vendor != X86_VENDOR_INTEL)
		return ret;

	cpuid_level	= cpuid_eax(0);
	ext_cpuid_level	= cpuid_eax(0x80000000);

	/* Invariant TSC */
	if (ext_cpuid_level >= 0x80000007 &&
	    (cpuid_edx(0x80000007) & (1 << 8)))
		cpu_info->caps |= CPUPOWER_CAP_INV_TSC;

	/* Aperf/Mperf registers support */
	if (cpuid_level >= 6 && (cpuid_ecx(6) & 0x1))
		cpu_info->caps |= CPUPOWER_CAP_APERF;

	/* AMD Boost state enable/disable register */
	if (cpu_info->vendor == X86_VENDOR_AMD) {
		if (ext_cpuid_level >= 0x80000007 &&
		    (cpuid_edx(0x80000007) & (1 << 9)))
			cpu_info->caps |= CPUPOWER_CAP_AMD_CBP;
	}

	/* Intel's perf-bias MSR support */
	if (cpu_info->vendor == X86_VENDOR_INTEL) {
		if (cpuid_level >= 6 && (cpuid_ecx(6) & (1 << 3)))
			cpu_info->caps |= CPUPOWER_CAP_PERF_BIAS;
	}

	/*	printf("ID: %u - Extid: 0x%x - Caps: 0x%llx\n",
		cpuid_level, ext_cpuid_level, cpu_info->caps);
	*/
	return ret;
}
