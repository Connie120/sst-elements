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
    warp_inst(VanadisInstruction* inst, uint64_t wid, active_mask_t mask) :
        VanadisInstruction(*inst),
        _inst(inst),
        _wid(wid), 
        _mask(mask) {
            // simt_isa_int_regs_in = (count_isa_int_reg_in > 0) ? new uint16_t[count_isa_int_reg_in] : nullptr;
            // std::memset(isa_int_regs_in, 0, count_isa_int_reg_in);

            // simt_isa_int_regs_out = (count_isa_int_reg_out > 0) ? new uint16_t[count_isa_int_reg_out] : nullptr;
            // std::memset(isa_int_regs_out, 0, count_isa_int_reg_out);

            // simt_phys_fp_regs_in = (count_phys_fp_reg_in > 0) ? new uint16_t[count_phys_fp_reg_in] : nullptr;
            // std::memset(phys_fp_regs_in, 0, count_phys_fp_reg_in);

            // simt_phys_fp_regs_out = (count_phys_fp_reg_out > 0) ? new uint16_t[count_phys_fp_reg_out] : nullptr;
            // std::memset(phys_fp_regs_out, 0, count_phys_fp_reg_out);
        }

    uint64_t get_wid() { return _wid; }
    active_mask_t get_mask() { return _mask; }
    VanadisInstruction* get_inst() { return _inst; }

    void set_mask(active_mask_t next_mask) { _mask = next_mask; }

    warp_inst* clone() override { return new warp_inst(*this); }
    VanadisFunctionalUnitType getInstFuncType() const override { return _inst->getInstFuncType(); }

    const char* getInstCode() const override { return _inst->getInstCode(); }

    void execute(SST::Output* output, VanadisRegisterFile* regFile) override {}

    // uint16_t getISAIntRegIn_SIMT(const uint16_t index) const { return simt_isa_int_regs_in[index]; }
    // uint16_t getISAIntRegOut_SIMT(const uint16_t index) const { return simt_isa_int_regs_out[index]; }

    // uint16_t getISAFPRegIn_SIMT(const uint16_t index) const { return simt_isa_fp_regs_in[index]; }
    // uint16_t getISAFPRegOut_SIMT(const uint16_t index) const { return simt_isa_fp_regs_out[index]; }

private:
    VanadisInstruction* _inst;
    active_mask_t _mask;
    uint64_t _wid;

    // uint16_t* simt_isa_int_regs_in;
    // uint16_t* simt_isa_int_regs_out;

    // uint16_t* simt_isa_fp_regs_in;
    // uint16_t* simt_isa_fp_regs_out;

};

}  // namespace Vanadis
}  // namespace SST

#endif