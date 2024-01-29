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
    warp(uint16_t wid, active_mask_t mask, VanadisISATable* warp_ISATable, VanadisISATable* warp_recover_ISATable) :
        _wid(wid), 
        _mask(mask),
        warp_rrt(warp_ISATable),
        warp_recover_rrt(warp_recover_ISATable) {}

    uint16_t get_wid() { return _wid; }
    active_mask_t get_mask() { return _mask; }

    void set_mask(active_mask_t next_mask) { _mask = next_mask; }

public:
    VanadisISATable* warp_rrt;
    VanadisISATable* warp_recover_rrt;

private:
    active_mask_t _mask;
    uint16_t _wid;
};

}  // namespace Vanadis
}  // namespace SST

#endif
