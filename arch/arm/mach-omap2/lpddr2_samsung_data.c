/*
 * LPDDR2 data as per JESD209-2
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 *
 * Aneesh V <aneesh@ti.com>
 * Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <mach/emif.h>
#include <mach/lpddr2-samsung.h>

const struct lpddr2_timings timings_samsung_400_mhz = {
	.max_freq	= 400000000,
	.RL		= 6,
#ifdef OVERCLOCK_SAMSUNG_DDR_4G_S4
	.tRPab		= 13,
	.tRCD		= 13,
	.tWR		= 9,
	.tRASmin	= 23,
#else
	.tRPab		= 21,
	.tRCD		= 18,
	.tWR		= 15,
	.tRASmin	= 42,
#endif
	.tRRD		= 10,
	.tWTRx2		= 15,
	.tXSR		= 140,
	.tXPx2		= 15,
	.tRFCab		= 130,
	.tRTPx2		= 15,
	.tCKE		= 3,
	.tCKESR		= 15,
	.tZQCS		= 90,
	.tZQCL		= 360,
	.tZQINIT	= 1000,
	.tDQSCKMAXx2	= 11,
	.tRASmax	= 70,
	.tFAW		= 50
};

const struct lpddr2_timings timings_samsung_333_mhz = {
	.max_freq	= 333000000,
	.RL		= 5,
#ifdef OVERCLOCK_SAMSUNG_DDR_4G_S4
	.tRPab		= 13,
	.tRCD		= 13,
	.tWR		= 9,
	.tRASmin	= 23,
#else
	.tRPab		= 21,
	.tRCD		= 18,
	.tWR		= 15,
	.tRASmin	= 42,
#endif
	.tRRD		= 10,
	.tWTRx2		= 15,
	.tXSR		= 140,
	.tXPx2		= 15,
	.tRFCab		= 130,
	.tRTPx2		= 15,
	.tCKE		= 3,
	.tCKESR		= 15,
	.tZQCS		= 90,
	.tZQCL		= 360,
	.tZQINIT	= 1000,
	.tDQSCKMAXx2	= 11,
	.tRASmax	= 70,
	.tFAW		= 50
};

const struct lpddr2_timings timings_samsung_200_mhz = {
	.max_freq	= 200000000,
	.RL		= 3,
#ifdef OVERCLOCK_SAMSUNG_DDR_4G_S4
	.tRPab		= 13,
	.tRCD		= 13,
	.tWR		= 9,
	.tRASmin	= 23,
#else
	.tRPab		= 21,
	.tRCD		= 18,
	.tWR		= 15,
	.tRASmin	= 42,
#endif
	.tRRD		= 10,
	.tWTRx2		= 20,
	.tXSR		= 140,
	.tXPx2		= 15,
	.tRFCab		= 130,
	.tRTPx2		= 15,
	.tCKE		= 3,
	.tCKESR		= 15,
	.tZQCS		= 90,
	.tZQCL		= 360,
	.tZQINIT	= 1000,
	.tDQSCKMAXx2	= 11,
	.tRASmax	= 70,
	.tFAW		= 50
};

const struct lpddr2_min_tck min_tck_samsung = {
	.tRL		= 3,
	.tRP_AB		= 3,
	.tRCD		= 3,
	.tWR		= 3,
	.tRAS_MIN	= 3,
	.tRRD		= 2,
	.tWTR		= 2,
	.tXP		= 2,
	.tRTP		= 2,
	.tCKE		= 3,
	.tCKESR		= 3,
	.tFAW		= 8
};

struct lpddr2_device_info samsung_4G_S4 = {
	.device_timings = {
		&timings_samsung_200_mhz,
		&timings_samsung_333_mhz,
		&timings_samsung_400_mhz
	},
	.min_tck	= &min_tck_samsung,
	.type		= LPDDR2_TYPE_S4,
	.density	= LPDDR2_DENSITY_4Gb,
	.io_width	= LPDDR2_IO_WIDTH_32,
	.emif_ddr_selfrefresh_cycles = 262144,
};
