#ifndef _H_VANADIS_SIMT_DATA
#define _H_VANADIS_SIMT_DATA

#include <bitset>

#define WARP_SIZE 32
#define NUM_THREADS 64

#define NUM_WARPS ((NUM_THREADS) / (WARP_SIZE))

#define WARP_INT_REGS 32
#define WARP_FP_REGS 32

typedef std::bitset<WARP_SIZE> active_mask_t;

// Ni: set a global bool variable to check whether the threads have been created
// bool check_simt = false;

#endif
