// Copyright 2009-2013 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2013, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include "sst_config.h"
#include "sst/core/serialization/element.h"
#include <assert.h>

#include "sst/core/element.h"

#include "simpleRNGComponent.h"

using namespace SST;
using namespace SST::RNG;
using namespace SST::SimpleRNGComponent;

simpleRNGComponent::simpleRNGComponent(ComponentId_t id, Params_t& params) :
  Component(id) {

  rng_count = 0;
  rng_max_count = params.find_integer("count", 1000);

  if( params.find("rng") != params.end() ) {
        rng_type = params["rng"];

	if( params["rng"] == "mersenne" ) {
		std::cout << "Using Mersenne Random Number Generator..." << std::endl;
		unsigned int seed = 1447;

		if( params.find("seed") != params.end() ) {
			seed = params.find_integer("seed");
		}

	  	rng = new MersenneRNG(seed);
	} else if ( params["rng"] == "marsaglia" ) {
		std::cout << "Using Marsaglia Random Number Generator..." << std::endl;

  		unsigned int m_z = 0;
  		unsigned int  m_w = 0;

  		m_w = params.find_integer("seed_w", 0);
  		m_z = params.find_integer("seed_z", 0);

  		if(m_w == 0 || m_z == 0) {
			rng = new MarsagliaRNG();
  		} else {
			rng = new MarsagliaRNG(m_z, m_w);
  		}
	} else {
		std::cout << "RNG provided but unknown " << params["rng"] << ", so using Mersenne..." << std::endl;
		rng = new MersenneRNG(1447);
	}
  } else {
	std::cout << "No RNG provided, so using Mersenne..." << std::endl;
	rng = new MersenneRNG(1447);
  }

  // tell the simulator not to end without us
//  registerExit();  // Renamed Per Issue 70 - ALevine
  registerAsPrimaryComponent();
  primaryComponentDoNotEndSim();

  //set our clock
  registerClock( "1GHz", 
		 new Clock::Handler<simpleRNGComponent>(this, 
			&simpleRNGComponent::tick ) );
}

simpleRNGComponent::simpleRNGComponent() :
    Component(-1)
{
    // for serialization only
}

bool simpleRNGComponent::tick( Cycle_t ) {
	rng_count++;

	std::cout << "Random: " << rng_count << " of " << rng_max_count << ": " <<
		rng->nextUniform() << ", " << rng->generateNextUInt32() << ", " <<
		rng->generateNextUInt64() << ", " << rng->generateNextInt32() <<
		", " << rng->generateNextInt64()
		<< std::endl;

  	// return false so we keep going
  	if(rng_count == rng_max_count) {
//      unregisterExit();  // Renamed Per Issue 70 - ALevine
      primaryComponentOKToEndSim();
  		return true;
  	} else {
  		return false;
  	}
}

// Element Library / Serialization stuff

BOOST_CLASS_EXPORT(simpleRNGComponent)

static Component*
create_simpleRNGComponent(SST::ComponentId_t id, 
                  SST::Component::Params_t& params)
{
    return new simpleRNGComponent( id, params );
}

static const ElementInfoParam component_params[] = {
    { "seed_w", "" },
    { "seed_z", "" },
    { "seed", "" },
    { "rng", "" },
    { "count", "" },
    { NULL, NULL}
};

static const ElementInfoComponent components[] = {
    { "simpleRNGComponent",
      "Random number generation component",
      NULL,
      create_simpleRNGComponent,
      component_params
    },
    { NULL, NULL, NULL, NULL }
};

extern "C" {
    ElementLibraryInfo simpleRNGComponent_eli = {
        "simpleRNGComponent",
        "Random number generation component",
        components,
    };
}
