#ifndef _H_VANADIS_WARP
#define _H_VANADIS_WARP

#include "simt/thread.h"
#include "inst/vinst.h"
#include "simt/simt_data_structure.h"

#include "inst/isatable.h"

#include <cstdint>
#include <vector>
#include <bitset>

namespace SST {
namespace Vanadis {

class warp {
public:
    warp(uint64_t wid, active_mask_t mask) :
        _wid(wid), 
        _mask(mask),
        warp_rrt(warp_ISATable) {}

    uint64_t get_wid() { return _wid; }
    active_mask_t get_mask() { return _mask; }

    void set_mask(active_mask_t next_mask) { _mask = next_mask; }

public:
    VanadisISATable* warp_rrt;

private:
    active_mask_t _mask;
    uint64_t _wid;
};

}  // namespace Vanadis
}  // namespace SST

#endif