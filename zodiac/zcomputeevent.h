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


#ifndef _H_ZODIAC_COMPUTE_EVENT
#define _H_ZODIAC_COMPUTE_EVENT

#include "zevent.h"

using namespace SST::Hermes;

namespace SST {
namespace Zodiac {

class ZodiacComputeEvent : public ZodiacEvent {

	public:
		ZodiacComputeEvent(double timeSeconds);
		double getComputeDuration();
		double getComputeDurationNano();
		ZodiacEventType getEventType();

	private:
		double computeTime;

};

}
}

#endif
