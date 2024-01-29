#ifndef _H_VANADIS_SIMT_ROB
#define _H_VANADIS_SIMT_ROB

#include "simt/warp_inst.h"
#include "simt/simt_data_structure.h"

namespace SST {
namespace Vanadis {

class simt_rob_entry {
public:
    simt_rob_entry(warp_inst* ins, uint64_t wid, active_mask_t mask) :
    _ins(ins),
    _wid(wid),
    _mask(mask) {}
    
    warp_inst* get_ins() { return _ins; }
    uint64_t get_wid() { return _wid; }
    active_mask_t get_mask() { return _mask;}

private:
    warp_inst* _ins;
    uint64_t _wid;
    active_mask_t _mask;
};

}  // namespace Vanadis
}  // namespace SST

#endif
