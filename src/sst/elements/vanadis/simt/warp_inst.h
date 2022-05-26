#ifndef _H_VANADIS_WARP_INST
#define _H_VANADIS_WARP_INST

#include "simt/thread.h"
#include "inst/vinst.h"
#include "simt/simt_data_structure.h"

#include <cstdint>
#include <vector>
#include <bitset>

namespace SST {
namespace Vanadis {

class warp_inst : public VanadisInstruction {
public:
    warp_inst(VanadisInstruction const& inst, uint64_t wid, active_mask_t mask) :
        VanadisInstruction(inst),
        _wid(wid), 
        _mask(mask) {}

    uint64_t get_wid() { return _wid; }
    active_mask_t get_mask() { return _mask; }

    void set_mask(active_mask_t next_mask) { _mask = next_mask; }

    warp_inst* clone() override { return new warp_inst(*this); }
    VanadisFunctionalUnitType getInstFuncType() const override { return WARP_INST; }

    const char* getInstCode() const override { return "SIMT"; }

    void execute(SST::Output* output, VanadisRegisterFile* regFile) override {}

private:
    active_mask_t _mask;
    uint64_t _wid;
};

}  // namespace Vanadis
}  // namespace SST

#endif