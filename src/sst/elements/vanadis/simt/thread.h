#ifndef _H_VANADIS_THREAD
#define _H_VANADIS_THREAD

#include <cstdint>

namespace SST {
namespace Vanadis {

class thread_info {
public:
    thread_info(uint64_t tid, uint64_t pc, uint64_t wid) :
    _tid(tid),
    _pc(pc),
    _wid(wid) {}

    uint64_t get_tid() { return _tid; }
    uint64_t get_wid() { return _wid; }
    uint64_t get_pc() { return _pc; }
    void set_pc(uint64_t next_pc) { _pc = next_pc; }

private:
    uint64_t _tid;
    uint64_t _pc;
    uint64_t _wid;
};

}  // namespace Vanadis
}  // namespace SST

#endif
