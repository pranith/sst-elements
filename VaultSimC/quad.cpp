// Copyright 2009-2015 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2015, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <sst_config.h>
#include <sst/core/serialization.h>
#include <sst/core/interfaces/stringEvent.h>
#include <sst/core/link.h>
#include <sst/core/params.h>

#include "quad.h"

using namespace SST::Interfaces;
using namespace SST::MemHierarchy;

quad::quad(ComponentId_t id, Params& params) : IntrospectedComponent( id ) 
{
    // Debug and Output Initialization
    out.init("", 0, 0, Output::STDOUT);

    int debugLevel = params.find_integer("debug_level", 0);
    dbg.init("@R:Quad::@p():@l " + getName() + ": ", debugLevel, 0, (Output::output_location_t)params.find_integer("debug", 0));
    if(debugLevel < 0 || debugLevel > 10) 
        dbg.fatal(CALL_INFO, -1, "Debugging level must be between 0 and 10. \n");

    // quad Params Initialization
    int ident = params.find_integer("quadID", -1);
    if (-1 == ident)
        dbg.fatal(CALL_INFO, -1, "quadID not defined\n");
    quadID = ident;

    numVaultPerQuad = params.find_integer("num_vault_per_quad", 4);
    
    // etc
    CacheLineSize = params.find_integer("cacheLineSize", 64);
    CacheLineSizeLog2 = log(CacheLineSize) / log(2);

    // clock
    std::string frequency;
    frequency = params.find_string("clock", "2.2 Ghz");
    registerClock(frequency, new Clock::Handler<quad>(this, &quad::clock));
    dbg.debug(_INFO_, "Making quad with id=%d & clock=%s\n", quadID, frequency.c_str());
}

bool quad::clock(Cycle_t currentCycle) {

}

void quad::finish() 
{

}