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


#ifndef _H_EMBER_ALLREDUCE_MOTIF
#define _H_EMBER_ALLREDUCE_MOTIF

#include "mpi/embermpigen.h"

namespace SST {
namespace Ember {

class EmberAllreduceGenerator : public EmberMessagePassingGenerator {

public:
	EmberAllreduceGenerator(SST::Component* owner, Params& params);
    bool generate( std::queue<EmberEvent*>& evQ);

private:
    uint64_t  m_startTime;
    uint64_t  m_stopTime;
    uint64_t m_compute;
	uint32_t m_iterations;
	uint32_t m_count;
    void*    m_sendBuf;
    void*    m_recvBuf;
    uint32_t m_loopIndex;
};

}
}

#endif
