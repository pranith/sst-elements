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


#ifndef MEMORYMODEL_H
#define MEMORYMODEL_H

#include "McSimDefs.h"
#include "CycleTracker.h"

namespace McNiagara{
//-------------------------------------------------------------------
/// @brief Memory model class
///
/// Memory model should:
/// - keep track of how many loads and stores are outstanding in order
///   to stall if buffers are full
/// - should sample st2ld hist on a load to see if load is satisfied
///   from store buffer
/// - should use memory hierarchy probabilities to sim memory access
///   and report latency
/// - TODO: currently, memory model is not doing anything with ld-ld
///   and st-st distances, maybe it should. It does keep the mem queue
///   and stalls accordingly if mem ops happen close to each other, but
///   this might not capture the true distribution
//-------------------------------------------------------------------
class MemoryModel
{
   // Memory operation types
   enum MemOpType {MEMLOAD, MEMSTORE};
   
   // Various constant costs
   class Cost {
     public:
      static const unsigned int LoadAfterLoad = 1;
      static const unsigned int LoadFromSTB = 2;
      static const unsigned int AverageStoreLatency = 25;
      static const unsigned int StoreAfterStore = 2;
   };
   
   class Config {
     public:
      static const unsigned int StoreBufferSize = 8;
   };
   
   /// Memory operation list node type
   struct MemoryOp 
   {
      InstructionNumber insnNum;
      Address address;
      unsigned int numBytes;
      CycleCount issueCycle;
      CycleCount satisfiedCycle;
      MemOpType op;
      struct MemoryOp *next;
   };

 public:
   MemoryModel();
   ~MemoryModel();
   void initLatencies(unsigned int latTLB, unsigned int latL1, unsigned int latL2, 
                      unsigned int latMem);
   void initProbabilities(double pSTBHit, double pL1Hit, double pL2Hit,
                          double pTLBMiss, double pICHit, double pIL2Hit,
                          double pITLBMiss);
   void getDataLoadStats(unsigned long long *numLoads,
                         unsigned long long *numSTBHits,
                         unsigned long long *numL1Hits,
                         unsigned long long *numL2Hits,
                         unsigned long long *numMemoryHits,
                         unsigned long long *numTLBMisses);
   void getInstLoadStats(unsigned long long *numILoads,
                         unsigned long long *numICHits,
                         unsigned long long *numIL2Hits,
                         unsigned long long *numIMemoryHits,
                         unsigned long long *numITLBMisses);
   void getStoreStats(unsigned long long *numStores );
   CycleCount serveLoad(CycleCount currentCycle, Address address,
                        unsigned int numBytes, CycleTracker::CycleReason *reason);
   CycleCount serveILoad(CycleCount currentCycle, Address address,
                         unsigned int numBytes, CycleTracker::CycleReason *reason);
   CycleCount serveStore(CycleCount currentCycle, Address address,
                         unsigned int numBytes, CycleTracker::CycleReason *reason);
 private:
   int addToMemoryQ(CycleCount whenSatisfied, MemOpType type);
   unsigned int numberInMemoryQ(MemOpType memOp);
   double purgeMemoryQ(CycleCount upToCycle);

   MemoryOp *memQHead, *memQTail, *lastLoad, *lastStore;
   unsigned int numLoadsInQ, numStoresInQ;
   unsigned int latencyL1, latencyL2, latencyMem, latencyTLB;
   double pSTBHit, pL1Hit, pL2Hit, pTLBMiss, pICHit, pIL2Hit, pITLBMiss;
   unsigned long long numL1Hits, numL2Hits, numMemoryHits, numTLBMisses;
   unsigned long long numICHits, numIL2Hits, numIMemoryHits, numITLBMisses;
   unsigned long long numSTBHits, numStores, numLoads, numILoads;
};
}//end namespace McNiagara
#endif
