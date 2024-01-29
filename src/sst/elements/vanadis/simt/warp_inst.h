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
        _mask(mask) {}

    std::vector<VanadisInstruction*> memAccessInst;

    uint64_t get_wid() { return _wid; }
    active_mask_t get_mask() { return _mask; }
    VanadisInstruction* get_inst() { return _inst; }

    void set_mask(active_mask_t next_mask) { _mask = next_mask; }

    warp_inst* clone() override { return new warp_inst(*this); }
    VanadisFunctionalUnitType getInstFuncType() const override { return _inst->getInstFuncType(); }

    const char* getInstCode() const override { return _inst->getInstCode(); }

    void execute(SST::Output* output, VanadisRegisterFile* regFile) override {
        // Performance Simulation Only
        markExecuted(); 
    }

private:
    VanadisInstruction* _inst;
    active_mask_t _mask;
    uint64_t _wid;
};

class warp_inst_memAccess : public VanadisInstruction {
public:
    warp_inst_memAccess(VanadisInstruction* inst, uint64_t wid, uint16_t tid_in_warp) :
    VanadisInstruction(*inst),
    _inst(inst),
    _wid(wid),
    _tid_in_warp(tid_in_warp) {}

    VanadisInstruction* get_inst() { return _inst; }
    uint16_t get_tid() { return _tid_in_warp; }
    uint64_t get_wid() { return _wid; }

    warp_inst_memAccess* clone() override { return new warp_inst_memAccess(*this); }
    VanadisFunctionalUnitType getInstFuncType() const override { return _inst->getInstFuncType(); }

    const char* getInstCode() const override { return _inst->getInstCode(); }

    void execute(SST::Output* output, VanadisRegisterFile* regFile) override {}

private:
    uint16_t _tid_in_warp;
    VanadisInstruction* _inst;
    uint64_t _wid;
};

}  // namespace Vanadis
}  // namespace SST

#endif
