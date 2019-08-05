// Copyright 2009-2019 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2019, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <sst_config.h>
#include <sst/core/stringize.h>
#include <sst/core/params.h>
#include <sst/core/timeLord.h>

#include "hash.h"
#include "cacheController.h"
#include "util.h"
#include "cacheListener.h"
#include "mshr.h"
#include "memLinkBase.h"

using namespace SST::MemHierarchy;
using namespace std;

/* Main constructor for Cache */
Cache::Cache(ComponentId_t id, Params &params) : Component(id) {

    /* --------------- Output Class --------------- */
    out_ = new Output();
    out_->init("", params.find<int>("verbose", 1), 0, Output::STDOUT);

    d_ = new Output();
    d_->init("--->  ", params.find<int>("debug_level", 1), 0,(Output::output_location_t)params.find<int>("debug", 0));

    d2_ = new Output();
    d2_->init("", params.find<int>("debug_level", 1), 0,(Output::output_location_t)params.find<int>("debug", SST::Output::NONE));

    /* Debug filtering */
    std::vector<Addr> addrArr;
    params.find_array<Addr>("debug_addr", addrArr);
    for (std::vector<Addr>::iterator it = addrArr.begin(); it != addrArr.end(); it++)
        DEBUG_ADDR.insert(*it);

    bool found;

    /* Warn about deprecated parameters */
    checkDeprecatedParams(params);

    /* Pull out parameters that the cache keeps - the rest will be pulled as needed */
    // L1
    L1_ = params.find<bool>("L1", false);

    // Protocol
    std::string protStr = params.find<std::string>("coherence_protocol", "mesi");
    to_lower(protStr);
    if (protStr == "mesi") protocol_ = CoherenceProtocol::MESI;
    else if (protStr == "msi") protocol_ = CoherenceProtocol::MSI;
    else if (protStr == "none") protocol_ = CoherenceProtocol::NONE;
    else out_->fatal(CALL_INFO,-1, "%s, Invalid param: coherence_protocol - must be 'msi', 'mesi', or 'none'.\n", getName().c_str());

    // Type
    type_ = params.find<std::string>("cache_type", "inclusive");
    to_lower(type_);
    if (type_ != "inclusive" && type_ != "noninclusive" && type_ != "noninclusive_with_directory")
        out_->fatal(CALL_INFO, -1, "%s, Invalid param: cache_type - valid options are 'inclusive' or 'noninclusive' or 'noninclusive_with_directory'. You specified '%s'.\n", getName().c_str(), type_.c_str());

    // Latency
    accessLatency_ = params.find<uint64_t>("access_latency_cycles", 0, found);
    if (!found) out_->fatal(CALL_INFO, -1, "%s, Param not specified: access_latency_cycles - access time for cache.\n", getName().c_str());

    tagLatency_ = params.find<uint64_t>("tag_access_latency_cycles", accessLatency_);


    // Error check parameter combinations
    if (accessLatency_ < 1) out_->fatal(CALL_INFO,-1, "%s, Invalid param: access_latency_cycles - must be at least 1. You specified %" PRIu64 "\n",
            this->Component::getName().c_str(), accessLatency_);

    if (L1_ && type_ != "inclusive") {
        out_->fatal(CALL_INFO, -1, "%s, Invalid param: cache_type - must be 'inclusive' for an L1. You specified '%s'.\n", getName().c_str(), type_.c_str());
    } else if (!L1_ && protocol_ == CoherenceProtocol::NONE && type_ != "noninclusive") {
        out_->fatal(CALL_INFO, -1, "%s, Invalid param combo: cache_type and coherence_protocol - non-coherent caches are noninclusive. You specified: cache_type = '%s', coherence_protocol = '%s'\n",
                getName().c_str(), type_.c_str(), protStr.c_str());
    }

    /* Construct cache structures */
    cacheArray_ = createCacheArray(params);

    /* Banks */
    uint64_t banks = params.find<uint64_t>("banks", 0);
    bankStatus_.resize(banks, false);
    bankConflictBuffer_.resize(banks);
    cacheArray_->setBanked(banks);

    /* Create clock, deadlock timeout, etc. */
    createClock(params);

    /* Create MSHR */
    int mshrSize = createMSHR(params);

    /* Load prefetcher, listeners, if any */
    createListeners(params, mshrSize);


    allNoncacheableRequests_    = params.find<bool>("force_noncacheable_reqs", false);
    maxRequestsPerCycle_        = params.find<int>("max_requests_per_cycle",-1);
    string packetSize           = params.find<std::string>("min_packet_size", "8B");

    UnitAlgebra packetSize_ua(packetSize);
    if (!packetSize_ua.hasUnits("B")) {
        out_->fatal(CALL_INFO, -1, "%s, Invalid param: min_packet_size - must have units of bytes (B). Ex: '8B'. SI units are ok. You specified '%s'\n", this->Component::getName().c_str(), packetSize.c_str());
    }

    if (maxRequestsPerCycle_ == 0) {
        maxRequestsPerCycle_ = -1;  // Simplify compare
    }
    requestsThisCycle_ = 0;

    /* Configure links */
    configureLinks(params);

    /* Register statistics */
    registerStatistics();

    createCoherenceManager(params);
}


void Cache::createCoherenceManager(Params &params) {
    coherenceMgr_ = NULL;
    std::string inclusive = (type_ == "inclusive") ? "true" : "false";
    std::string protocol = (protocol_ == CoherenceProtocol::MESI) ? "true" : "false";
    isLL = true;
    silentEvict = true;
    lowerIsNoninclusive = false;
    expectWritebackAcks = false;
    Params coherenceParams;
    coherenceParams.insert("debug_level", params.find<std::string>("debug_level", "1"));
    coherenceParams.insert("debug", params.find<std::string>("debug", "0"));
    coherenceParams.insert("access_latency_cycles", std::to_string(accessLatency_));
    coherenceParams.insert("mshr_latency_cycles", std::to_string(mshrLatency_));
    coherenceParams.insert("tag_access_latency_cycles", std::to_string(tagLatency_));
    coherenceParams.insert("cache_line_size", params.find<std::string>("cache_line_size", "64"));
    coherenceParams.insert("protocol", protocol);   // Not used by all managers
    coherenceParams.insert("inclusive", inclusive); // Not used by all managers
    coherenceParams.insert("snoop_l1_invalidations", params.find<std::string>("snoop_l1_invalidations", "false")); // Not used by all managers
    coherenceParams.insert("request_link_width", params.find<std::string>("request_link_width", "0B"));
    coherenceParams.insert("response_link_width", params.find<std::string>("response_link_width", "0B"));
    coherenceParams.insert("min_packet_size", params.find<std::string>("min_packet_size", "8B"));

    bool prefetch = (statPrefetchRequest != nullptr);
    doInCoherenceMgr_ = false;

    if (!L1_) {
        if (protocol_ != CoherenceProtocol::NONE) {
            if (type_ == "inclusive") { 
                coherenceMgr_ = loadAnonymousSubComponent<CoherenceController>("memHierarchy.coherence.mesi_inclusive", "coherence", 0, 
                        ComponentInfo::INSERT_STATS, coherenceParams, coherenceParams, prefetch);
            } else if (type_ == "noninclusive") {
                coherenceMgr_ = loadAnonymousSubComponent<CoherenceController>("memHierarchy.coherence.mesi_private_noninclusive", "coherence", 0, 
                        ComponentInfo::INSERT_STATS, coherenceParams, coherenceParams, prefetch);
            } else {
                coherenceMgr_ = loadAnonymousSubComponent<CoherenceController>("memHierarchy.coherence.mesi_shared_noninclusive", "coherence", 0, 
                        ComponentInfo::INSERT_STATS, coherenceParams, coherenceParams, prefetch);
            }
        } else {
            coherenceMgr_ = loadAnonymousSubComponent<CoherenceController>("memHierarchy.coherence.incoherent", "coherence", 0, 
                    ComponentInfo::INSERT_STATS, coherenceParams, coherenceParams, prefetch);
        }
    } else {
        if (protocol_ != CoherenceProtocol::NONE) {
            coherenceMgr_ = loadAnonymousSubComponent<CoherenceController>("memHierarchy.coherence.mesi_l1", "coherence", 0, 
                    ComponentInfo::INSERT_STATS, coherenceParams, coherenceParams, prefetch);
            doInCoherenceMgr_ = true;
        } else {
            coherenceMgr_ = loadAnonymousSubComponent<CoherenceController>("memHierarchy.coherence.incoherent_l1", "coherence", 0, 
                    ComponentInfo::INSERT_STATS, coherenceParams, coherenceParams, prefetch);
        }
    }
    if (coherenceMgr_ == NULL) {
        out_->fatal(CALL_INFO, -1, "%s, Failed to load CoherenceController.\n", this->Component::getName().c_str());
    }

    coherenceMgr_->setLinks(linkUp_, linkDown_);
    coherenceMgr_->setMSHR(mshr_);
    coherenceMgr_->setCacheListener(listeners_);
    coherenceMgr_->setDebug(DEBUG_ADDR);
    coherenceMgr_->setOwnerName(getName());
    coherenceMgr_->setCacheArray(cacheArray_);

}


/*
 *  Configure links to components above (closer to CPU) and below (closer to memory)
 *  Check for connected ports to determine which links to use
 *  Valid port combos:
 *      high_network_0 & low_network_%d : connected to core/cache/bus above and cache/bus below
 *      high_network_0 & cache          : connected to core/cache/bus above and network talking to a cache below
 *      high_network_0 & directory      : connected to core/cache/bus above and network talking to a directory below
 *      directory                       : connected to a network talking to a cache above and a directory below (single network connection)
 *      cache & low_network_0           : connected to network above talking to a cache and core/cache/bus below
 */
void Cache::configureLinks(Params &params) {
    linkUp_ = loadUserSubComponent<MemLinkBase>("cpulink");
    if (linkUp_)
        linkUp_->setRecvHandler(new Event::Handler<Cache>(this, &Cache::handleEvent));

    linkDown_ = loadUserSubComponent<MemLinkBase>("memlink");
    if (linkDown_)
        linkDown_->setRecvHandler(new Event::Handler<Cache>(this, &Cache::handleEvent));

    if (linkUp_ || linkDown_) {
        if (!linkUp_ || !linkDown_)
            out_->verbose(_L3_, "%s, Detected user defined subcomponent for either the cpu or mem link but not both. Assuming this component has just one link.\n", getName().c_str());
        if (!linkUp_)
            linkUp_ = linkDown_;
        if (!linkDown_)
            linkDown_ = linkUp_;

        // Check for cache slices and assign the NIC an appropriate region -> overrides the given one
        uint64_t sliceCount         = params.find<uint64_t>("num_cache_slices", 1);
        uint64_t sliceID            = params.find<uint64_t>("slice_id", 0);
        std::string slicePolicy     = params.find<std::string>("slice_allocation_policy", "rr");
        if (sliceCount == 1)
            sliceID = 0;
        else if (sliceCount > 1) {
            if (sliceID >= sliceCount) 
                out_->fatal(CALL_INFO,-1, "%s, Invalid param: slice_id - should be between 0 and num_cache_slices-1. You specified %" PRIu64 ".\n",
                        getName().c_str(), sliceID);
            if (slicePolicy != "rr") 
                out_->fatal(CALL_INFO,-1, "%s, Invalid param: slice_allocation_policy - supported policy is 'rr' (round-robin). You specified '%s'.\n",
                        getName().c_str(), slicePolicy.c_str());
        } else {
            d2_->fatal(CALL_INFO, -1, "%s, Invalid param: num_cache_slices - should be 1 or greater. You specified %" PRIu64 ".\n",
                    getName().c_str(), sliceCount);
        }

        bool gotRegion = false;
        bool found;
        region_.setDefault();
        region_.start = params.find<uint64_t>("addr_range_start", region_.start, found);
        gotRegion |= found;
        region_.end = params.find<uint64_t>("addr_range_end", region_.end, found);
        gotRegion |= found;
        std::string isize = params.find<std::string>("interleave_size", "0B", found);
        gotRegion |= found;
        std::string istep = params.find<std::string>("interleave_step", "0B", found);
        gotRegion |= found;

        if (!UnitAlgebra(isize).hasUnits("B")) {
            d2_->fatal(CALL_INFO, -1, "Invalid param(%s): interleave_size - must be specified in bytes with units (SI units OK). For example, '1KiB'. You specified '%s'\n",
                    getName().c_str(), isize.c_str());
        }
        if (!UnitAlgebra(istep).hasUnits("B")) {
            d2_->fatal(CALL_INFO, -1, "Invalid param(%s): interleave_step - must be specified in bytes with units (SI units OK). For example, '1KiB'. You specified '%s'\n",
                    getName().c_str(), istep.c_str());
        }
        region_.interleaveSize = UnitAlgebra(isize).getRoundedValue();
        region_.interleaveStep = UnitAlgebra(istep).getRoundedValue();

        if (!found && sliceCount > 1) {
            gotRegion = true;
            int lineSize = params.find<int>("cache_line_size", 64);
            if (slicePolicy == "rr") {
                region_.start = sliceID*lineSize;
                region_.end = (uint64_t) - 1;
                region_.interleaveSize = lineSize;
                region_.interleaveStep = sliceCount*lineSize;
            }
        }
        
        if (gotRegion) {
            linkDown_->setRegion(region_);
            linkUp_->setRegion(region_);
        } else {
            region_ = linkDown_->getRegion();
            linkUp_->setRegion(region_);
        }
        
        cacheArray_->setSliceAware(region_.interleaveSize, region_.interleaveStep);

        clockUpLink_ = linkUp_->isClocked();
        clockDownLink_ = linkDown_->isClocked();
        
        linkUp_->setName(getName());
        linkDown_->setName(getName());
        
        return;
    }

    
    bool highNetExists  = false;    // high_network_0 is connected -> direct link toward CPU (to bus or directly to other component)
    bool lowCacheExists = false;    // cache is connected -> direct link towards memory to cache
    bool lowDirExists   = false;    // directory is connected -> network link towards memory to directory
    bool lowNetExists   = false;    // low_network_%d port(s) are connected -> direct link towards memory (to bus or other component)

    highNetExists   = isPortConnected("high_network_0");
    lowCacheExists  = isPortConnected("cache");
    lowDirExists    = isPortConnected("directory");
    lowNetExists    = isPortConnected("low_network_0");

    /* Check for valid port combos */
    if (highNetExists) {
        if (!lowCacheExists && !lowDirExists && !lowNetExists)
            out_->fatal(CALL_INFO,-1,"%s, Error: no connected low ports detected. Please connect one of 'cache' or 'directory' or connect N components to 'low_network_n' where n is in the range 0 to N-1\n",
                    getName().c_str());
        if ((lowCacheExists && (lowDirExists || lowNetExists)) || (lowDirExists && lowNetExists))
            out_->fatal(CALL_INFO,-1,"%s, Error: multiple connected low port types detected. Please only connect one of 'cache', 'directory', or connect N components to 'low_network_n' where n is in the range 0 to N-1\n",
                    getName().c_str());
        if (isPortConnected("high_network_1"))
            out_->fatal(CALL_INFO,-1,"%s, Error: multiple connected high ports detected. Use the 'Bus' component to connect multiple entities to port 'high_network_0' (e.g., connect 2 L1s to a bus and connect the bus to the L2)\n",
                    getName().c_str());
    } else {
        if (!lowCacheExists && !lowDirExists)
            out_->fatal(CALL_INFO,-1,"%s, Error: no connected ports detected. Valid ports are high_network_0, cache, directory, and low_network_n\n",
                    getName().c_str());
    }
    region_.start = 0;
    region_.end = (uint64_t) - 1;
    region_.interleaveSize = 0;
    region_.interleaveStep = 0;

    // Fix up parameters for creating NIC - eventually we'll stop doing this
    bool found;
    if (fixupParam(params, "network_bw", "memNIC.network_bw"))
        out_->output(CALL_INFO, "Note (%s): Changed 'network_bw' to 'memNIC.network_bw' in params. Change your input file to remove this notice.\n", getName().c_str());
    if (fixupParam(params, "network_input_buffer_size", "memNIC.network_input_buffer_size"))
        out_->output(CALL_INFO, "Note (%s): Changed 'network_input_buffer_size' to 'memNIC.network_input_buffer_size' in params. Change your input file to remove this notice.\n", getName().c_str());
    if (fixupParam(params, "network_output_buffer_size", "memNIC.network_output_buffer_size"))
        out_->output(CALL_INFO, "Note (%s): Changed 'network_output_buffer_size' to 'memNIC.network_output_buffer_size' in params. Change your input file to remove this notice.\n", getName().c_str());
    if (fixupParam(params, "min_packet_size", "memNIC.min_packet_size"))
        out_->output(CALL_INFO, "Note (%s): Changed 'min_packet_size' to 'memNIC.min_packet_size'. Change your input file to remove this notice.\n", getName().c_str());

    std::string opalNode = params.find<std::string>("node", "0");
    std::string opalShMem = params.find<std::string>("shared_memory", "0");
    std::string opalSize = params.find<std::string>("local_memory_size", "0");

    Params nicParams = params.find_prefix_params("memNIC." );
    nicParams.insert("node", opalNode);
    nicParams.insert("shared_memory", opalShMem);
    nicParams.insert("local_memory_size", opalSize);

    Params memlink = params.find_prefix_params("memlink.");
    memlink.insert("port", "low_network_0");
    memlink.insert("node", opalNode);
    memlink.insert("shared_memory", opalShMem);
    memlink.insert("local_memory_size", opalSize);

    Params cpulink = params.find_prefix_params("cpulink.");
    cpulink.insert("port", "high_network_0");
    cpulink.insert("node", opalNode);
    cpulink.insert("shared_memory", opalShMem);
    cpulink.insert("local_memory_size", opalSize);

    /* Finally configure the links */
    if (highNetExists && lowNetExists) {

        d_->debug(_INFO_,"Configuring cache with a direct link above and below\n");

        linkDown_ = loadAnonymousSubComponent<MemLinkBase>("memHierarchy.MemLink", "memlink", 0, ComponentInfo::INSERT_STATS | ComponentInfo::SHARE_PORTS, memlink);
        linkDown_->setRecvHandler(new Event::Handler<Cache>(this, &Cache::handleEvent));


        linkUp_ = loadAnonymousSubComponent<MemLinkBase>("memHierarchy.MemLink", "cpulink", 0, ComponentInfo::INSERT_STATS | ComponentInfo::SHARE_PORTS, cpulink);
        linkUp_->setRecvHandler(new Event::Handler<Cache>(this, &Cache::handleEvent));
        clockUpLink_ = clockDownLink_ = false;
        /* Region given to each should be identical so doesn't matter which we pull but force them to be identical */
        region_ = linkDown_->getRegion();
        linkUp_->setRegion(region_);

    } else if (highNetExists && lowCacheExists) {

        d_->debug(_INFO_,"Configuring cache with a direct link above and a network link to a cache below\n");

        nicParams.find<std::string>("group", "", found);
        if (!found) nicParams.insert("group", "1");

        if (isPortConnected("cache_ack") && isPortConnected("cache_fwd") && isPortConnected("cache_data")) {
            nicParams.find<std::string>("req.port", "", found);
            if (!found) nicParams.insert("req.port", "cache");
            nicParams.find<std::string>("ack.port", "", found);
            if (!found) nicParams.insert("ack.port", "cache_ack");
            nicParams.find<std::string>("fwd.port", "", found);
            if (!found) nicParams.insert("fwd.port", "cache_fwd");
            nicParams.find<std::string>("data.port", "", found);
            if (!found) nicParams.insert("data.port", "cache_data");
            linkDown_ = loadAnonymousSubComponent<MemLinkBase>("memHierarchy.MemNICFour", "memlink", 0, ComponentInfo::INSERT_STATS | ComponentInfo::SHARE_PORTS, nicParams);
        } else {
            nicParams.find<std::string>("port", "", found);
            if (!found) nicParams.insert("port", "cache");
            linkDown_ = loadAnonymousSubComponent<MemLinkBase>("memHierarchy.MemNIC", "memlink", 0, ComponentInfo::INSERT_STATS | ComponentInfo::SHARE_PORTS, nicParams);
        }

        linkDown_->setRecvHandler(new Event::Handler<Cache>(this, &Cache::handleEvent));

        // Configure high link
        linkUp_ = loadAnonymousSubComponent<MemLinkBase>("memHierarchy.MemLink", "cpulink", 0, ComponentInfo::INSERT_STATS | ComponentInfo::SHARE_PORTS, cpulink);
        linkUp_->setRecvHandler(new Event::Handler<Cache>(this, &Cache::handleEvent));
        clockDownLink_ = true;
        clockUpLink_ = false;
        
        region_ = linkDown_->getRegion();
        linkUp_->setRegion(region_);

    } else if (lowCacheExists && lowNetExists) { // "lowCache" is really "highCache" now
        d_->debug(_INFO_,"Configuring cache with a network link to a cache above and a direct link below\n");

        nicParams.find<std::string>("group", "", found);
        if (!found) nicParams.insert("group", "1");

        if (isPortConnected("cache_ack") && isPortConnected("cache_fwd") && isPortConnected("cache_data")) {
            nicParams.find<std::string>("req.port", "", found);
            if (!found) nicParams.insert("req.port", "cache");
            nicParams.find<std::string>("ack.port", "", found);
            if (!found) nicParams.insert("ack.port", "cache_ack");
            nicParams.find<std::string>("fwd.port", "", found);
            if (!found) nicParams.insert("fwd.port", "cache_fwd");
            nicParams.find<std::string>("data.port", "", found);
            if (!found) nicParams.insert("data.port", "cache_data");
            linkUp_ = loadAnonymousSubComponent<MemLinkBase>("memHierarchy.MemNICFour", "cpulink", 0, ComponentInfo::INSERT_STATS | ComponentInfo::SHARE_PORTS, nicParams);
        } else {
            nicParams.find<std::string>("port", "", found);
            if (!found) nicParams.insert("port", "cache");
            linkUp_ = loadAnonymousSubComponent<MemLinkBase>("memHierarchy.MemNIC", "cpulink", 0, ComponentInfo::INSERT_STATS | ComponentInfo::SHARE_PORTS, nicParams);
        }

        linkUp_->setRecvHandler(new Event::Handler<Cache>(this, &Cache::handleEvent));

        // Configure high link
        linkDown_ = loadAnonymousSubComponent<MemLinkBase>("memHierarchy.MemLink", "memlink", 0, ComponentInfo::INSERT_STATS | ComponentInfo::SHARE_PORTS, memlink);
        linkDown_->setRecvHandler(new Event::Handler<Cache>(this, &Cache::handleEvent));
        clockUpLink_ = true;
        clockDownLink_ = false;
        
        /* Pull region off network link, really we should have the same region on both and it should be a cache property not link property... */
        region_ = linkUp_->getRegion();
        linkDown_->setRegion(region_);

    } else if (highNetExists && lowDirExists) {

        d_->debug(_INFO_,"Configuring cache with a direct link above and a network link to a directory below\n");

        nicParams.find<std::string>("group", "", found);
        if (!found) nicParams.insert("group", "2");

        if (isPortConnected("directory_ack") && isPortConnected("directory_fwd") && isPortConnected("directory_data")) {
            nicParams.find<std::string>("req.port", "", found);
            if (!found) nicParams.insert("req.port", "directory");
            nicParams.find<std::string>("ack.port", "", found);
            if (!found) nicParams.insert("ack.port", "directory_ack");
            nicParams.find<std::string>("fwd.port", "", found);
            if (!found) nicParams.insert("fwd.port", "directory_fwd");
            nicParams.find<std::string>("data.port", "", found);
            if (!found) nicParams.insert("data.port", "directory_data");
            linkDown_ = loadAnonymousSubComponent<MemLinkBase>("memHierarchy.MemNICFour", "memlink", 0, ComponentInfo::INSERT_STATS | ComponentInfo::SHARE_PORTS, nicParams);
        } else {
            nicParams.find<std::string>("port", "", found);
            if (!found) nicParams.insert("port", "directory");
            linkDown_ = loadAnonymousSubComponent<MemLinkBase>("memHierarchy.MemNIC", "memlink", 0, ComponentInfo::INSERT_STATS | ComponentInfo::SHARE_PORTS, nicParams);
        }
        // Configure low link
        linkDown_->setRecvHandler(new Event::Handler<Cache>(this, &Cache::handleEvent));

        // Configure high link
        linkUp_ = loadAnonymousSubComponent<MemLinkBase>("memHierarchy.MemLink", "cpulink", 0, ComponentInfo::INSERT_STATS | ComponentInfo::SHARE_PORTS, cpulink);
        linkUp_->setRecvHandler(new Event::Handler<Cache>(this, &Cache::handleEvent));
        clockDownLink_ = true;
        clockUpLink_ = false;
        
        region_ = linkDown_->getRegion();
        linkUp_->setRegion(region_);

    } else {    // lowDirExists

        d_->debug(_INFO_, "Configuring cache with a network to talk to both a cache above and a directory below\n");

        nicParams.find<std::string>("group", "", found);
        if (!found) nicParams.insert("group", "2");

        nicParams.find<std::string>("port", "", found);
        if (!found) nicParams.insert("port", "directory");

        // Configure low link
        // This NIC may need to account for cache slices. Check params.
        uint64_t cacheSliceCount    = params.find<uint64_t>("num_cache_slices", 1);
        uint64_t sliceID            = params.find<uint64_t>("slice_id", 0);
        string sliceAllocPolicy     = params.find<std::string>("slice_allocation_policy", "rr");
        if (cacheSliceCount == 1) sliceID = 0;
        else if (cacheSliceCount > 1) {
            if (sliceID >= cacheSliceCount) out_->fatal(CALL_INFO,-1, "%s, Invalid param: slice_id - should be between 0 and num_cache_slices-1. You specified %" PRIu64 ".\n",
                    getName().c_str(), sliceID);
            if (sliceAllocPolicy != "rr") out_->fatal(CALL_INFO,-1, "%s, Invalid param: slice_allocation_policy - supported policy is 'rr' (round-robin). You specified '%s'.\n",
                    getName().c_str(), sliceAllocPolicy.c_str());
        } else {
            d2_->fatal(CALL_INFO, -1, "%s, Invalid param: num_cache_slices - should be 1 or greater. You specified %" PRIu64 ".\n",
                    getName().c_str(), cacheSliceCount);
        }

        uint64_t addrRangeStart = 0;
        uint64_t addrRangeEnd = (uint64_t) - 1;
        uint64_t interleaveSize = 0;
        uint64_t interleaveStep = 0;

        if (cacheSliceCount > 1) {
            uint64_t lineSize = params.find<uint64_t>("cache_line_size", 64);
            if (sliceAllocPolicy == "rr") {
                addrRangeStart = sliceID*lineSize;
                interleaveSize = lineSize;
                interleaveStep = cacheSliceCount*lineSize;
            }
        }
        // Set region parameters
        nicParams.find<std::string>("addr_range_start", "", found);
        if (!found) nicParams.insert("addr_range_start", std::to_string(addrRangeStart));
        nicParams.find<std::string>("addr_range_end", "", found);
        if (!found) nicParams.insert("addr_range_end", std::to_string(addrRangeEnd));
        nicParams.find<std::string>("interleave_size", "", found);
        if (!found) nicParams.insert("interleave_size", std::to_string(interleaveSize) + "B");
        nicParams.find<std::string>("interleave_step", "", found);
        if (!found) nicParams.insert("interleave_step", std::to_string(interleaveStep) + "B");

        if (isPortConnected("directory_ack") && isPortConnected("directory_fwd") && isPortConnected("directory_data")) {
            nicParams.find<std::string>("req.port", "", found);
            if (!found) nicParams.insert("req.port", "directory");
            nicParams.find<std::string>("ack.port", "", found);
            if (!found) nicParams.insert("ack.port", "directory_ack");
            nicParams.find<std::string>("fwd.port", "", found);
            if (!found) nicParams.insert("fwd.port", "directory_fwd");
            nicParams.find<std::string>("data.port", "", found);
            if (!found) nicParams.insert("data.port", "directory_data");
            linkDown_ = loadAnonymousSubComponent<MemLinkBase>("memHierarchy.MemNICFour", "cpulink", 0, ComponentInfo::INSERT_STATS | ComponentInfo::SHARE_PORTS, nicParams);
        } else {
            nicParams.find<std::string>("port", "", found);
            if (!found) nicParams.insert("port", "directory");
            linkDown_ = loadAnonymousSubComponent<MemLinkBase>("memHierarchy.MemNIC", "cpulink", 0, ComponentInfo::INSERT_STATS | ComponentInfo::SHARE_PORTS, nicParams);
        }

        linkDown_->setRecvHandler(new Event::Handler<Cache>(this, &Cache::handleEvent));

        // Configure high link
        linkUp_ = linkDown_;
        clockDownLink_ = true;
        clockUpLink_ = false;
        
        region_ = linkDown_->getRegion();
        linkUp_->setRegion(region_);
    }
        
    linkUp_->setName(getName());
    linkDown_->setName(getName());
   
    cacheArray_->setSliceAware(region_.interleaveSize, region_.interleaveStep);

}

/* 
 * Listeners can be prefetchers, but could also be for statistic collection, trace generation, monitoring, etc. 
 * Prefetchers load into the 'prefetcher slot', listeners into the 'listener' slot
 */
void Cache::createListeners(Params &params, int mshrSize) {

    /* Configure prefetcher(s) */
    bool found;
    maxOutstandingPrefetch_     = params.find<uint64_t>("max_outstanding_prefetch", mshrSize / 2, found);
    dropPrefetchLevel_          = params.find<uint64_t>("drop_prefetch_mshr_level", mshrSize - 2, found);
    if (!found && mshrSize == 2) { // MSHR min size is 2
        dropPrefetchLevel_ = mshrSize - 1;
    } else if (found && dropPrefetchLevel_ >= mshrSize) {
        dropPrefetchLevel_ = mshrSize - 1; // Always have to leave one free for deadlock avoidance
    }

    SubComponentSlotInfo * lists = getSubComponentSlotInfo("prefetcher");
    if (lists) {
        int k = 0;
        for (int i = 0; i <= lists->getMaxPopulatedSlotNumber(); i++) {
            if (lists->isPopulated(i)) {
                listeners_.push_back(lists->create<CacheListener>(i, ComponentInfo::SHARE_NONE));
                listeners_[k]->registerResponseCallback(new Event::Handler<Cache>(this, &Cache::handlePrefetchEvent));
                k++;
            }
        }
    } else {
        std::string prefetcher = params.find<std::string>("prefetcher", "");
        Params prefParams;
        if (!prefetcher.empty()) {
            prefParams = params.find_prefix_params("prefetcher.");
            listeners_.push_back(loadAnonymousSubComponent<CacheListener>(prefetcher, "prefetcher", 0, ComponentInfo::INSERT_STATS, prefParams));
            listeners_[0]->registerResponseCallback(new Event::Handler<Cache>(this, &Cache::handlePrefetchEvent));
        }
    }
    if (!listeners_.empty()) {
        statPrefetchRequest = registerStatistic<uint64_t>("Prefetch_requests");
        statPrefetchDrop = registerStatistic<uint64_t>("Prefetch_drops");
    } else {
        statPrefetchRequest = nullptr;
        statPrefetchDrop = nullptr;
    }
    
    if (!listeners_.empty()) { // Have at least one prefetcher
        // Configure self link for prefetch/listener events
        // Delay prefetches by a cycle TODO parameterize - let user specify prefetch delay
        std::string frequency = params.find<std::string>("cache_frequency", "", found);
        prefetchDelay_ = params.find<SimTime_t>("prefetch_delay_cycles", 1);

        prefetchLink_ = configureSelfLink("Self", frequency, new Event::Handler<Cache>(this, &Cache::processPrefetchEvent));
    }

    /* Configure listener(s) */
    lists = getSubComponentSlotInfo("listener");
    if (lists) {
        for (int i = 0; i < lists->getMaxPopulatedSlotNumber(); i++) {
            if (lists->isPopulated(i))
                listeners_.push_back(lists->create<CacheListener>(i, ComponentInfo::SHARE_NONE));
        }
    } else if (listeners_.empty()) {
        Params emptyParams;
        listeners_.push_back(loadAnonymousSubComponent<CacheListener>("memHierarchy.emptyCacheListener", "listener", 0, ComponentInfo::SHARE_NONE, emptyParams));
    }
}

int Cache::createMSHR(Params &params) {
    bool found;
    uint64_t defaultMshrLatency = 1;
    int mshrSize = params.find<int>("mshr_num_entries", -1);           //number of entries
    mshrLatency_ = params.find<uint64_t>("mshr_latency_cycles", defaultMshrLatency, found);

    if (mshrSize == -1) mshrSize = HUGE_MSHR; // Set in mshr.h
    if (mshrSize < 2) out_->fatal(CALL_INFO, -1, "Invalid param: mshr_num_entries - MSHR requires at least 2 entries to avoid deadlock. You specified %d\n", mshrSize);

    mshr_ = new MSHR(d_, mshrSize, this->getName(), DEBUG_ADDR);

    if (mshrLatency_ > 0 && found) return mshrSize;

    if (L1_) {
        mshrLatency_ = 1;
    } else {
        // Otherwise if mshrLatency isn't set or is 0, intrapolate from cache latency
        uint64 N = 200; // max cache latency supported by the intrapolation method
        int y[N];

        /* L2 */
        y[0] = 0;
        y[1] = 1;
        for(uint64 idx = 2;  idx < 12; idx++) y[idx] = 2;
        for(uint64 idx = 12; idx < 16; idx++) y[idx] = 3;
        for(uint64 idx = 16; idx < 26; idx++) y[idx] = 5;

        /* L3 */
        for(uint64 idx = 26; idx < 46; idx++) y[idx] = 19;
        for(uint64 idx = 46; idx < 68; idx++) y[idx] = 26;
        for(uint64 idx = 68; idx < N;  idx++) y[idx] = 32;

        if (accessLatency_ > N) {
            out_->fatal(CALL_INFO, -1, "%s, Error: cannot intrapolate MSHR latency if cache latency > 200. Set 'mshr_latency_cycles' or reduce cache latency. Cache latency: %" PRIu64 "\n",
                    getName().c_str(), accessLatency_);
        }
        mshrLatency_ = y[accessLatency_];
    }

    if (mshrLatency_ != defaultMshrLatency) {
        Output out("", 1, 0, Output::STDOUT);
        out.verbose(CALL_INFO, 1, 0, "%s: No MSHR lookup latency provided (mshr_latency_cycles)...intrapolated to %" PRIu64 " cycles.\n", getName().c_str(), mshrLatency_);
    }
    return mshrSize;
}

/* Create the cache array */
CacheArray* Cache::createCacheArray(Params &params) {
    /* Get parameters and error check */
    bool found;
    std::string sizeStr = params.find<std::string>("cache_size", "", found);
    if (!found) out_->fatal(CALL_INFO, -1, "%s, Param not specified: cache_size\n", getName().c_str());

    uint64_t lineSize = params.find<uint64_t>("cache_line_size", 64);

    uint64_t assoc = params.find<uint64_t>("associativity", -1, found); // uint64_t to match cache size in case we have a fully associative cache
    if (!found) out_->fatal(CALL_INFO, -1, "%s, Param not specified: associativity\n", getName().c_str());

    
    uint64_t dEntries = params.find<uint64_t>("noninclusive_directory_entries", 0);
    uint64_t dAssoc = params.find<uint64_t>("noninclusive_directory_associativity", 1);


    /* Error check parameters and compute derived parameters */
    /* Fix up parameters */
    fixByteUnits(sizeStr);

    UnitAlgebra ua(sizeStr);
    if (!ua.hasUnits("B")) {
        out_->fatal(CALL_INFO, -1, "%s, Invalid param: cache_size - must have units of bytes(B). Ex: '32KiB'. SI units are ok. You specified '%s'.", getName().c_str(), sizeStr.c_str());
    }

    uint64_t cacheSize = ua.getRoundedValue();

    if (lineSize > cacheSize)
        out_->fatal(CALL_INFO, -1, "%s, Invalid param combo: cache_line_size cannot be greater than cache_size. You specified: cache_size = '%s', cache_line_size = '%" PRIu64 "'\n", 
                getName().c_str(), sizeStr.c_str(), lineSize);
    if (!isPowerOfTwo(lineSize)) out_->fatal(CALL_INFO, -1, "%s, cache_line_size - must be a power of 2. You specified '%" PRIu64 "'.\n", getName().c_str(), lineSize);

    uint64_t lines = cacheSize / lineSize;

    if (assoc < 1 || assoc > lines)
        out_->fatal(CALL_INFO, -1, "%s, Invalid param: associativity - must be at least 1 (direct mapped) and less than or equal to the number of cache lines (cache_size / cache_line_size). You specified '%" PRIu64 "'\n",
                getName().c_str(), assoc);

    if (type_ == "noninclusive_with_directory") { /* Error check dir params */
        if (dAssoc < 1 || dAssoc > dEntries)
            out_->fatal(CALL_INFO, -1, "%s, Invalid param: noninclusive_directory_associativity - must be at least 1 (direct mapped) and less than or equal to noninclusive_directory_entries. You specified '%" PRIu64 "'\n",
                    getName().c_str(), dAssoc);
        if (dEntries < 1)
            out_->fatal(CALL_INFO, -1, "%s, Invalid param: noninclusive_directory_entries - must be at least 1 if cache_type is noninclusive_with_directory. You specified '%" PRIu64 "'.\n", getName().c_str(), dEntries);
    }

    /* Build cache array */
    SubComponentSlotInfo* rslots = getSubComponentSlotInfo("replacement"); // May be multiple slots filled depending on how many arrays this cache manages
    ReplacementPolicy* rmgr;
    if (rslots && rslots->isPopulated(0))
        rmgr = rslots->create<ReplacementPolicy>(0, ComponentInfo::SHARE_NONE, lines, assoc);
    else { // Backwards compatability - user didn't declare policy in the input config
        std::string replacement = params.find<std::string>("replacement_policy", "lru");
        to_lower(replacement);
        rmgr = constructReplacementManager(replacement, lines, assoc, 0);
    }


    HashFunction * ht = loadUserSubComponent<HashFunction>("hash");
    if (!ht) {
        Params hparams;
        int hashFunc = params.find<int>("hash_function", 0);
        if (hashFunc == 1)      ht = loadAnonymousSubComponent<HashFunction>("memHierarchy.hash.linear", "hash", 0, ComponentInfo::SHARE_NONE, hparams);
        else if (hashFunc == 2) ht = loadAnonymousSubComponent<HashFunction>("memHierarchy.hash.xor", "hash", 0, ComponentInfo::SHARE_NONE, hparams);
        else                    ht = loadAnonymousSubComponent<HashFunction>("memHierarchy.hash.none", "hash", 0, ComponentInfo::SHARE_NONE, hparams);
    }

    if (type_ == "inclusive" || type_ == "noninclusive") {
        return new SetAssociativeArray(d_, lines, lineSize, assoc, rmgr, ht, !L1_);
    } else { //type_ == "noninclusive_with_directory" --> Already checked that this string is valid
        /* Construct */
        ReplacementPolicy* drmgr;
        if (rslots && rslots->isPopulated(1))
            drmgr = rslots->create<ReplacementPolicy>(1, ComponentInfo::SHARE_NONE, dEntries, dAssoc);
        else { // Backwards compatibility - user didn't declare policy in the input config
            std::string dReplacement = params.find<std::string>("noninclusive_directory_repl", "lru");
            to_lower(dReplacement);
            drmgr = constructReplacementManager(dReplacement, dEntries, dAssoc, 1);
        }
        return new DualSetAssociativeArray(d_, lineSize, ht, true, dEntries, dAssoc, drmgr, lines, assoc, rmgr);
    }
}

/* Create a replacement manager */
ReplacementPolicy* Cache::constructReplacementManager(std::string policy, uint64_t lines, uint64_t assoc, int slot) {
    Params params;
    if (SST::strcasecmp(policy, "lru"))
        return loadAnonymousSubComponent<ReplacementPolicy>("memHierarchy.replacement.lru", "replacement", slot, ComponentInfo::SHARE_NONE, params, lines, assoc);

    if (SST::strcasecmp(policy, "lfu"))
        return loadAnonymousSubComponent<ReplacementPolicy>("memHierarchy.replacement.lfu", "replacement", slot, ComponentInfo::SHARE_NONE, params, lines, assoc);

    if (SST::strcasecmp(policy, "random"))
        return loadAnonymousSubComponent<ReplacementPolicy>("memHierarchy.replacement.rand", "replacement", slot, ComponentInfo::SHARE_NONE, params, lines, assoc);

    if (SST::strcasecmp(policy, "mru"))
        return loadAnonymousSubComponent<ReplacementPolicy>("memHierarchy.replacement.mru", "replacement", slot, ComponentInfo::SHARE_NONE, params, lines, assoc);

    if (SST::strcasecmp(policy, "nmru"))
        return loadAnonymousSubComponent<ReplacementPolicy>("memHierarchy.replacement.nmru", "replacement", slot, ComponentInfo::SHARE_NONE, params, lines, assoc);

    out_->fatal(CALL_INFO, -1, "%s, Invalid param: (directory_)replacement_policy - supported policies are 'lru', 'lfu', 'random', 'mru', and 'nmru'. You specified '%s'.\n",
            getName().c_str(), policy.c_str());

    return nullptr;
}

void Cache::createClock(Params &params) {
    /* Create clock */
    bool found;
    std::string frequency = params.find<std::string>("cache_frequency", "", found);
    if (!found)
        out_->fatal(CALL_INFO, -1, "%s, Param not specified: frequency - cache frequency.\n", getName().c_str());

    clockHandler_       = new Clock::Handler<Cache>(this, &Cache::clockTick);
    defaultTimeBase_    = registerClock(frequency, clockHandler_);

    registerTimeBase("2 ns", true);       //  TODO:  Is this right?

    clockIsOn_ = true;
    timestamp_ = 0;

    // Deadlock timeout
    SimTime_t maxNano = params.find<SimTime_t>("maxRequestDelay", 0);
    maxWaitTime_ = (Simulation::getSimulation()->getTimeLord()->getNano())->convertToCoreTime(maxNano); // Figure out how many core cycles maxNano is
    checkMaxWaitInterval_ = maxNano / 4;
    // Doubtful that this corner case will occur but just in case...
    if (maxNano > 0 && checkMaxWaitInterval_ == 0) checkMaxWaitInterval_ = maxNano;
    if (maxWaitTime_ > 0) {
        ostringstream oss;
        oss << checkMaxWaitInterval_;
        string interval = oss.str() + "ns";
        maxWaitWakeupExists_ = false;
        maxWaitSelfLink_ = configureSelfLink("maxWait", interval, new Event::Handler<Cache>(this, &Cache::maxWaitWakeup));
    } else {
        maxWaitWakeupExists_ = true;
    }
}

/* Check for deprecated parameters and warn/fatal */
void Cache::checkDeprecatedParams(Params &params) {
    Output out("", 1, 0, Output::STDOUT);
    bool found;

    /* Standard error messages */
    std::string defError = "This parameter is no longer neccessary.";
    std::string autoDetectError = "The value of this parameter is now auto-detected.";

    std::map<std::string,std::string> depMap;

    /* Deprecated parameters */
    depMap["network_address"] = autoDetectError;

    for (std::map<std::string,std::string>::iterator it = depMap.begin(); it != depMap.end(); it++) {
        params.find<std::string>(it->first, "", found);
        if (found) {
            out.output("%s, ** Found deprecated parameter: %s ** %s Remove this parameter from your input deck to eliminate this message.\n", getName().c_str(), it->first.c_str(), it->second.c_str());
        }
    }
}

void Cache::registerStatistics() {
    Statistic<uint64_t>* def_stat = registerStatistic<uint64_t>("default_stat");
    for (int i = 0; i < (int)Command::LAST_CMD; i++)
        stat_eventRecv[i] = def_stat;

    statTotalEventsReceived         = registerStatistic<uint64_t>("TotalEventsReceived");
    statTotalEventsReplayed         = registerStatistic<uint64_t>("TotalEventsReplayed");
    statNoncacheableEventsReceived  = registerStatistic<uint64_t>("TotalNoncacheableEventsReceived");
    statCacheHits                   = registerStatistic<uint64_t>("CacheHits");
    statGetSHitOnArrival            = registerStatistic<uint64_t>("GetSHit_Arrival");
    statGetXHitOnArrival            = registerStatistic<uint64_t>("GetXHit_Arrival");
    statGetSXHitOnArrival           = registerStatistic<uint64_t>("GetSXHit_Arrival");
    statGetSHitAfterBlocked         = registerStatistic<uint64_t>("GetSHit_Blocked");
    statGetXHitAfterBlocked         = registerStatistic<uint64_t>("GetXHit_Blocked");
    statGetSXHitAfterBlocked        = registerStatistic<uint64_t>("GetSXHit_Blocked");
    statCacheMisses                 = registerStatistic<uint64_t>("CacheMisses");
    statGetSMissOnArrival           = registerStatistic<uint64_t>("GetSMiss_Arrival");
    statGetXMissOnArrival           = registerStatistic<uint64_t>("GetXMiss_Arrival");
    statGetSXMissOnArrival          = registerStatistic<uint64_t>("GetSXMiss_Arrival");
    statGetSMissAfterBlocked        = registerStatistic<uint64_t>("GetSMiss_Blocked");
    statGetXMissAfterBlocked        = registerStatistic<uint64_t>("GetXMiss_Blocked");
    statGetSXMissAfterBlocked       = registerStatistic<uint64_t>("GetSXMiss_Blocked");
    stat_eventRecv[(int)Command::GetS]      = registerStatistic<uint64_t>("GetS_recv");
    stat_eventRecv[(int)Command::GetX]      = registerStatistic<uint64_t>("GetX_recv");
    stat_eventRecv[(int)Command::GetSX]     = registerStatistic<uint64_t>("GetSX_recv");
    stat_eventRecv[(int)Command::GetSResp]  = registerStatistic<uint64_t>("GetSResp_recv");
    stat_eventRecv[(int)Command::GetXResp]  = registerStatistic<uint64_t>("GetXResp_recv");
    stat_eventRecv[(int)Command::PutS]      = registerStatistic<uint64_t>("PutS_recv");
    stat_eventRecv[(int)Command::PutM]      = registerStatistic<uint64_t>("PutM_recv");
    stat_eventRecv[(int)Command::PutE]      = registerStatistic<uint64_t>("PutE_recv");
    stat_eventRecv[(int)Command::Fetch]     = registerStatistic<uint64_t>("Fetch_recv");
    stat_eventRecv[(int)Command::FetchInv]  = registerStatistic<uint64_t>("FetchInv_recv");
    stat_eventRecv[(int)Command::FetchInvX] = registerStatistic<uint64_t>("FetchInvX_recv");
    stat_eventRecv[(int)Command::ForceInv]  = registerStatistic<uint64_t>("ForceInv_recv");
    stat_eventRecv[(int)Command::Inv]       = registerStatistic<uint64_t>("Inv_recv");
    stat_eventRecv[(int)Command::NACK]      = registerStatistic<uint64_t>("NACK_recv");
    stat_eventRecv[(int)Command::AckInv]    = registerStatistic<uint64_t>("AckInv_recv");
    stat_eventRecv[(int)Command::AckPut]    = registerStatistic<uint64_t>("AckPut_recv");
    stat_eventRecv[(int)Command::FetchResp] = registerStatistic<uint64_t>("FetchResp_recv");
    stat_eventRecv[(int)Command::FetchXResp] = registerStatistic<uint64_t>("FetchXResp_recv");
    stat_eventRecv[(int)Command::CustomReq] = registerStatistic<uint64_t>("CustomReq_recv");
    stat_eventRecv[(int)Command::CustomResp] = registerStatistic<uint64_t>("CustomResp_recv");
    stat_eventRecv[(int)Command::CustomAck] = registerStatistic<uint64_t>("CustomAck_recv");
    stat_eventRecv[(int)Command::FlushLine] = registerStatistic<uint64_t>("FlushLine_recv");
    stat_eventRecv[(int)Command::FlushLineInv] = registerStatistic<uint64_t>("FlushLineInv_recv");
    stat_eventRecv[(int)Command::FlushLineResp] = registerStatistic<uint64_t>("FlushLineResp_recv");
    stat_eventRecv[(int)Command::Put] = registerStatistic<uint64_t>("Put_recv");
    stat_eventRecv[(int)Command::Get] = registerStatistic<uint64_t>("Get_recv");
    stat_eventRecv[(int)Command::AckMove] = registerStatistic<uint64_t>("AckMove_recv");
    statMSHROccupancy               = registerStatistic<uint64_t>("MSHR_occupancy");
    statBankConflicts               = registerStatistic<uint64_t>("Bank_conflicts");
}
