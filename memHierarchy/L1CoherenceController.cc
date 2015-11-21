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
#include <vector>
#include "L1CoherenceController.h"
using namespace SST;
using namespace SST::MemHierarchy;

/*----------------------------------------------------------------------------------------------------------------------
 * L1 Coherence Controller 
 *---------------------------------------------------------------------------------------------------------------------*/
	    
/*
 * Public interface functions:
 *      handleEviction
 *      handleRequest
 *      handleResponse
 *      handleReplacement (not relevant for L1s)
 *      handleInvalidationRequest
 */
  
CacheAction L1CoherenceController::handleEviction(CacheLine* wbCacheLine, uint32_t groupId, string origRqstr, bool ignoredParam) {
    State state = wbCacheLine->getState();
    setGroupId(groupId);
   
    /* L1 specific code */
    if (wbCacheLine->isLocked()) {
        wbCacheLine->setEventsWaitingForLock(true);
        return STALL;
    }
    recordEvictionState(state);
    switch(state) {
        case I:
            return DONE;
        case S:
            inc_EvictionPUTSReqSent();
            sendWriteback(PutS, wbCacheLine, origRqstr);
            wbCacheLine->setState(I);    // wait for ack
            if (!LL_ && (LLC_ || writebackCleanBlocks_)) mshr_->insertWriteback(wbCacheLine->getBaseAddr());
            return DONE;
        case E:
            inc_EvictionPUTEReqSent();
            sendWriteback(PutE, wbCacheLine, origRqstr);
	    wbCacheLine->setState(I);    // wait for ack
            if (!LL_ && (LLC_ || writebackCleanBlocks_)) mshr_->insertWriteback(wbCacheLine->getBaseAddr());
	    return DONE;
        case M:
            inc_EvictionPUTMReqSent();
	    sendWriteback(PutM, wbCacheLine, origRqstr);
            wbCacheLine->setState(I);    // wait for ack
            if (!LL_ && (LLC_ || writebackCleanBlocks_)) mshr_->insertWriteback(wbCacheLine->getBaseAddr());
	    return DONE;
        case IS:
        case IM:
        case SM:
            return STALL;
        default:
	    d_->fatal(CALL_INFO,-1,"%s, Error: State is invalid during eviction: %s. Addr = 0x%" PRIx64 ". Time = %" PRIu64 "ns\n", 
                    name_.c_str(), StateString[state], wbCacheLine->getBaseAddr(), ((Component *)owner_)->getCurrentSimTimeNano());
    }
    return STALL; // Eliminate compiler warning
}


/**
 *  Handle request at bottomCC
 *  Obtain block if a cache miss
 *  Obtain needed coherence permission from lower level cache/memory if coherence miss
 */
CacheAction L1CoherenceController::handleRequest(MemEvent* event, CacheLine* cacheLine, bool replay){
    setGroupId(event->getGroupId());
    
    Command cmd = event->getCmd();

    switch(cmd) {
        case GetS:
            return handleGetSRequest(event, cacheLine, replay);
        case GetX:
        case GetSEx:
            return handleGetXRequest(event, cacheLine, replay);
        default:
	    d_->fatal(CALL_INFO,-1,"%s, Error: Received an unrecognized request: %s. Addr = 0x%" PRIx64 ", Src = %s. Time = %" PRIu64 "ns\n", 
                    name_.c_str(), CommandString[cmd], event->getBaseAddr(), event->getSrc().c_str(), ((Component *)owner_)->getCurrentSimTimeNano());
    }
    return STALL;    // Eliminate compiler warning
}



/**
 *  Handle replacement - Not relevant for L1s but required to implement 
 */
CacheAction L1CoherenceController::handleReplacement(MemEvent* event, CacheLine* cacheLine, MemEvent * reqEvent, bool replay) {
    d_->fatal(CALL_INFO,-1,"%s, Error: Received an unrecognized request: %s. Addr = 0x%" PRIx64 ", Src = %s. Time = %" PRIu64 "ns\n", 
            name_.c_str(), CommandString[event->getCmd()], event->getBaseAddr(), event->getSrc().c_str(), ((Component *)owner_)->getCurrentSimTimeNano());

    return IGNORE;
}


/**
 *  Handle invalidation - Inv, FetchInv, or FetchInvX
 *  Return: whether Inv was successful (true) or we are waiting on further actions (false). L1 returns true (no sharers/owners).
 */
CacheAction L1CoherenceController::handleInvalidationRequest(MemEvent * event, CacheLine * cacheLine, bool replay) {
    

    if (cacheLine == NULL) {
        stateStats_[event->getCmd()][I]++;
        if (mshr_->pendingWriteback(event->getBaseAddr())) {
            d_->debug(_L8_, "Treating Inv as AckPut, not sending AckInv\n");
            mshr_->removeWriteback(event->getBaseAddr());
            return DONE;
        } else {
            //if (event->getCmd() == Inv) sendAckInv(event->getBaseAddr(), event->getRqstr()); // When is this needed?
            return IGNORE;
        }
    }

    /* L1 specific code for gem5 integration */
    if (snoopL1Invs_) {
        MemEvent* snoop = new MemEvent((Component*)owner_, event->getAddr(), event->getBaseAddr(), Inv);
        uint64_t deliveryTime = (replay) ? timestamp_ + mshrLatency_ : timestamp_ + accessLatency_;
        Response resp = {snoop, deliveryTime, true};
        addToOutgoingQueueUp(resp);
    }

    cacheLine->atomicEnd();
    setGroupId(event->getGroupId());

    Command cmd = event->getCmd();
    switch (cmd) {
        case Inv: 
            return handleInv(event, cacheLine, replay);
        case Fetch:
            return handleFetch(event, cacheLine, replay);
        case FetchInv:
            return handleFetchInv(event, cacheLine, replay);
        case FetchInvX:
            return handleFetchInvX(event, cacheLine, replay);
        default:
	    d_->fatal(CALL_INFO,-1,"%s, Error: Received an unrecognized invalidation: %s. Addr = 0x%" PRIx64 ", Src = %s. Time = %" PRIu64 "ns\n", 
                    name_.c_str(), CommandString[cmd], event->getBaseAddr(), event->getSrc().c_str(), ((Component *)owner_)->getCurrentSimTimeNano());
    }
    return STALL; // eliminate compiler warning
}




CacheAction L1CoherenceController::handleResponse(MemEvent * respEvent, CacheLine * cacheLine, MemEvent * reqEvent) {
    Command cmd = respEvent->getCmd();
    switch (cmd) {
        case GetSResp:
        case GetXResp:
            handleDataResponse(respEvent, cacheLine, reqEvent);
            break;
        case AckPut:
            recordStateEventCount(respEvent->getCmd(), I);
            stateStats_[respEvent->getCmd()][I]++;
            mshr_->removeWriteback(respEvent->getBaseAddr());
            break;
        default:
            d_->fatal(CALL_INFO, -1, "%s, Error: Received unrecognized response: %s. Addr = 0x%" PRIx64 ", Src = %s. Time = %" PRIu64 "ns\n",
                    name_.c_str(), CommandString[cmd], respEvent->getBaseAddr(), respEvent->getSrc().c_str(), ((Component*)owner_)->getCurrentSimTimeNano());
    }
    return DONE;
}

/*------------------------------------------------------------------------------------------------
 *  Private event handlers
 *      HandleGetS
 *      HandleGetX
 *      handleDataResponse
 *      handleInv
 *      handleFetch
 *      handleFetchInv
 *      handleFetchInvX
 *
 *------------------------------------------------------------------------------------------------*/


CacheAction L1CoherenceController::handleGetSRequest(MemEvent* event, CacheLine* cacheLine, bool replay){
    State state = cacheLine->getState();
    vector<uint8_t>* data = cacheLine->getData();
    
    bool shouldRespond = !(event->isPrefetch() && (event->getRqstr() == name_));
    stateStats_[event->getCmd()][state]++; 
    recordStateEventCount(event->getCmd(), state);
    switch (state) {
        case I:
            forwardMessage(event, cacheLine->getBaseAddr(), cacheLine->getSize(), NULL);
            inc_GETSMissIS(event);
            cacheLine->setState(IS);
            return STALL;
        case S:
            inc_GETSHit(event);
            if (!shouldRespond) return DONE;
            if (event->isLoadLink()) cacheLine->atomicStart();
            sendResponseUp(event, S, data, replay);
            return DONE;
        case E:
        case M:
            inc_GETSHit(event);
            if (!shouldRespond) return DONE;
            if (event->isLoadLink()) cacheLine->atomicStart();
            sendResponseUp(event, S, data, replay);
            return DONE;
        default:
            d_->fatal(CALL_INFO,-1,"%s, Error: Handling a GetS request but coherence state is not valid and stable. Addr = 0x%" PRIx64 ", Cmd = %s, Src = %s, State = %s. Time = %" PRIu64 "ns\n",
                    name_.c_str(), event->getBaseAddr(), CommandString[event->getCmd()], event->getSrc().c_str(), 
                    StateString[state], ((Component *)owner_)->getCurrentSimTimeNano());

    }
    return STALL;    // eliminate compiler warning
}


/*
 *  Return: whether event was handled or is waiting for further responses
 */
CacheAction L1CoherenceController::handleGetXRequest(MemEvent* event, CacheLine* cacheLine, bool replay) {
    State state = cacheLine->getState();
    Command cmd = event->getCmd();
    vector<uint8_t>* data = cacheLine->getData();
    
    stateStats_[event->getCmd()][state]++;
    recordStateEventCount(event->getCmd(), state);
    switch (state) {
        case I:
            inc_GETXMissIM(event);
            forwardMessage(event, cacheLine->getBaseAddr(), cacheLine->getSize(), NULL);
            cacheLine->setState(IM);
            return STALL;
        case S:
            inc_GETXMissSM(event);
            forwardMessage(event, cacheLine->getBaseAddr(), cacheLine->getSize(), NULL);
            cacheLine->setState(SM);
            return STALL;
        case E:
            cacheLine->setState(M);
        case M:
            if (cmd == GetX) {
                /* L1s write back immediately */
                if (!event->isStoreConditional() || cacheLine->isAtomic()) {
                    cacheLine->setData(event->getPayload(), event);
                    if (DEBUG_ALL || DEBUG_ADDR == cacheLine->getBaseAddr()) {
                        printData(cacheLine->getData(), true);
                    }
                }
                /* Handle GetX as unlock (store-unlock) */
                if (event->queryFlag(MemEvent::F_LOCKED)) {
            	    if (!cacheLine->isLocked()) {  // Sanity check - can't unlock an already unlocked line 
                        d_->fatal(CALL_INFO, -1, "%s, Error: Unlock request to an already unlocked cache line. Addr = 0x%" PRIx64 ". Time = %" PRIu64 "ns\n", 
                                name_.c_str(), event->getBaseAddr(), ((Component *)owner_)->getCurrentSimTimeNano());
   	            }
                    cacheLine->decLock();
                }
            } else {
                /* Handle GetSEx - Load-lock */
                cacheLine->incLock(); 
            }
            
            if (event->isStoreConditional()) sendResponseUp(event, M, data, replay, cacheLine->isAtomic());
            else sendResponseUp(event, M, data, replay);

            if (cmd == GetSEx) inc_GetSExReqsReceived(replay);
            inc_GETXHit(event);
            return DONE;
         default:
            d_->fatal(CALL_INFO, -1, "%s, Error: Received %s int unhandled state %s. Addr = 0x%" PRIx64 ", Src = %s. Time = %" PRIu64 "ns\n",
                    name_.c_str(), CommandString[cmd], StateString[state], event->getBaseAddr(), event->getSrc().c_str(), ((Component*)owner_)->getCurrentSimTimeNano());
    }
    return STALL; // Eliminate compiler warning
}


/**
 *  Handle a GetXResp or GetSResp from a lower level in the hierarchy
 */
void L1CoherenceController::handleDataResponse(MemEvent* responseEvent, CacheLine* cacheLine, MemEvent* origRequest){
    
    bool shouldRespond = !(origRequest->isPrefetch() && (origRequest->getRqstr() == name_));
    
    State state = cacheLine->getState();
    stateStats_[responseEvent->getCmd()][state]++; 
    recordStateEventCount(responseEvent->getCmd(), state);
    
    switch (state) {
        case IS:
            cacheLine->setData(responseEvent->getPayload(), responseEvent);
            if (DEBUG_ALL || DEBUG_ADDR == cacheLine->getBaseAddr()) {
                printData(cacheLine->getData(), true);
            }
            if (responseEvent->getGrantedState() == E) cacheLine->setState(E);
            else if (responseEvent->getGrantedState() == M) cacheLine->setState(M);
            else cacheLine->setState(S);
            inc_GETSHit(origRequest);
            if (!shouldRespond) break;
            if (origRequest->isLoadLink()) cacheLine->atomicStart();
            sendResponseUp(origRequest, S, cacheLine->getData(), true);
            break;
        case IM:
            cacheLine->setData(responseEvent->getPayload(), responseEvent);
            if (DEBUG_ALL || DEBUG_ADDR == cacheLine->getBaseAddr()) {
                printData(cacheLine->getData(), true);
            }
        case SM:
            cacheLine->setState(M);
            if (origRequest->getCmd() == GetX) {
                if (!origRequest->isStoreConditional() || cacheLine->isAtomic()) {
                    cacheLine->setData(origRequest->getPayload(), origRequest);
                    if (DEBUG_ALL || DEBUG_ADDR == cacheLine->getBaseAddr()) {
                        printData(cacheLine->getData(), true);
                    }
                }
                /* Handle GetX as unlock (store-unlock) */
                if (origRequest->queryFlag(MemEvent::F_LOCKED)) {
            	    if (!cacheLine->isLocked()) {  // Sanity check - can't unlock an already unlocked line 
                        d_->fatal(CALL_INFO, -1, "%s, Error: Unlock request to an already unlocked cache line. Addr = 0x%" PRIx64 ". Time = %" PRIu64 "ns\n", 
                                name_.c_str(), origRequest->getBaseAddr(), ((Component *)owner_)->getCurrentSimTimeNano());
   	            }
                    cacheLine->decLock();
                }
            } else {
                /* Handle GetSEx - Load-lock */
                cacheLine->incLock(); 
                inc_GetSExReqsReceived(true);
            }
            
            if (origRequest->isStoreConditional()) sendResponseUp(origRequest, M, cacheLine->getData(), true, cacheLine->isAtomic());
            else sendResponseUp(origRequest, M, cacheLine->getData(), true);

            inc_GETXHit(origRequest);
            break;
        default:
            d_->fatal(CALL_INFO, -1, "%s, Error: Response received but state is not handled. Addr = 0x%" PRIx64 ", Cmd = %s, Src = %s, State = %s. Time = %" PRIu64 "ns\n",
                    name_.c_str(), responseEvent->getBaseAddr(), CommandString[responseEvent->getCmd()], 
                    responseEvent->getSrc().c_str(), StateString[state], ((Component *)owner_)->getCurrentSimTimeNano());
    }
}

CacheAction L1CoherenceController::handleInv(MemEvent* event, CacheLine* cacheLine, bool replay) {
    State state = cacheLine->getState();
    stateStats_[event->getCmd()][state]++;
    recordStateEventCount(event->getCmd(), state);

    switch(state) {
        case I:
        case IS:
        case IM:
            //sendAckInv(event->getBaseAddr(), event->getRqstr());
            //inc_InvalidatePUTSReqSent();
            return IGNORE;  // Our Put raced with the invalidation and will serve as the AckInv when it arrives
        case S:
            sendAckInv(event->getBaseAddr(), event->getRqstr());
            inc_InvalidatePUTSReqSent();
            cacheLine->setState(I);
            return DONE;
        case SM:
            sendAckInv(event->getBaseAddr(), event->getRqstr());
            inc_InvalidatePUTSReqSent();
            cacheLine->setState(IM);
            return DONE;
        default:
	    d_->fatal(CALL_INFO,-1,"%s, Error: Received an invalidation in an unhandled state: %s. Addr = 0x%" PRIx64 ", Src = %s, State = %s. Time = %" PRIu64 "ns\n", 
                    name_.c_str(), CommandString[event->getCmd()], event->getBaseAddr(), event->getSrc().c_str(), StateString[state], ((Component *)owner_)->getCurrentSimTimeNano());
    }
    return STALL;
}



CacheAction L1CoherenceController::handleFetchInv(MemEvent * event, CacheLine * cacheLine, bool replay) {
    State state = cacheLine->getState();

    if (cacheLine->isLocked()) {
        cacheLine->setEventsWaitingForLock(true); 
        return STALL;
    }
    
    stateStats_[event->getCmd()][state]++; 
    recordStateEventCount(event->getCmd(), state);
    
    switch (state) {
        case I:
        case IS:
        case IM:
            return IGNORE;
        case SM:
            inc_FetchInvReqSent();
            sendResponseDown(event, cacheLine, replay);
            cacheLine->setState(IM);
            return DONE;
        case S:
        case E:
        case M:
            inc_FetchInvReqSent();
            sendResponseDown(event, cacheLine, replay);
            cacheLine->setState(I);
            return DONE;
        default:
            d_->fatal(CALL_INFO,-1,"%s, Error: Received FetchInv but state is unhandled. Addr = 0x%" PRIx64 ", Src = %s, State = %s. Time = %" PRIu64 "ns\n", 
                        name_.c_str(), event->getAddr(), event->getSrc().c_str(), StateString[state], ((Component *)owner_)->getCurrentSimTimeNano());    

    }
    return STALL;
}

CacheAction L1CoherenceController::handleFetchInvX(MemEvent * event, CacheLine * cacheLine, bool replay) {
    State state = cacheLine->getState();
    
    if (cacheLine->isLocked()) {
        cacheLine->setEventsWaitingForLock(true); 
        return STALL;
    }
    stateStats_[event->getCmd()][state]++; 
    recordStateEventCount(event->getCmd(), state);
    
    switch (state) {
        case I:
        case IS:
        case IM:
        case SM:
            return IGNORE;
        case E:
        case M:
            inc_FetchInvXReqSent();
            sendResponseDown(event, cacheLine, replay);
            cacheLine->setState(S);
            return DONE;
        default:
            d_->fatal(CALL_INFO,-1,"%s, Error: Received FetchInvX but state is unhandled. Addr = 0x%" PRIx64 ", Src = %s, State = %s. Time = %" PRIu64 "ns\n", 
                    name_.c_str(), event->getAddr(), event->getSrc().c_str(), StateString[state], ((Component *)owner_)->getCurrentSimTimeNano());
    }
    return STALL;
}


CacheAction L1CoherenceController::handleFetch(MemEvent * event, CacheLine * cacheLine, bool replay) {
    State state = cacheLine->getState();
    
    if (cacheLine->isLocked()) {
        cacheLine->setEventsWaitingForLock(true); 
        return STALL;
    }
    
    stateStats_[event->getCmd()][state]++; 
    recordStateEventCount(event->getCmd(), state);
    
    switch (state) {
        case I:
        case IS:
        case IM:
            return IGNORE;
        case SM:
        case S:
            sendResponseDown(event, cacheLine, replay);
            return DONE;
        default:
            d_->fatal(CALL_INFO,-1,"%s, Error: Received Fetch but state is unhandled. Addr = 0x%" PRIx64 ", Src = %s, State = %s. Time = %" PRIu64 "ns\n", 
                    name_.c_str(), event->getAddr(), event->getSrc().c_str(), StateString[state], ((Component *)owner_)->getCurrentSimTimeNano());
    }
    return STALL;
}



/*---------------------------------------------------------------------------------------------------
 * Helper Functions
 *--------------------------------------------------------------------------------------------------*/
/*
 *  Return type of coherence miss
 *  0:  Hit
 *  1:  NP/I
 *  2:  Wrong state (e.g., S but GetX request)
 *  3:  Right state but owners/sharers need to be invalidated or line is in transition
 */
int L1CoherenceController::isCoherenceMiss(MemEvent* event, CacheLine* cacheLine) {
    Command cmd = event->getCmd();
    State state = cacheLine->getState();
    if (cmd == GetSEx) cmd = GetX;  // for our purposes these are equal

    if (state == I) return 1;
    if (event->isPrefetch() && event->getRqstr() == name_) return 0;
    
    switch (state) {
        case S:
            if (cmd == GetS) return 0;
            return 2;
        case E:
        case M:
            return 0;
        case IS:
        case IM:
        case SM:
            return 3;
        default:
            return 0;   // this is profiling so don't die on unhandled state
    }
}


/*********************************************
 *  Methods for sending & receiving messages
 *********************************************/

/*
 *  Handles: responses to fetch invalidates
 *  Latency: cache access to read data for payload  
 */
void L1CoherenceController::sendResponseDown(MemEvent* event, CacheLine* cacheLine, bool replay){
    MemEvent *responseEvent = event->makeResponse();
    responseEvent->setPayload(*cacheLine->getData());
    //printf("Sending response down: ");
    if (DEBUG_ALL || DEBUG_ADDR == cacheLine->getBaseAddr()) {
        printData(cacheLine->getData(), false);
    }
    responseEvent->setSize(cacheLine->getSize());
    
    if (cacheLine->getState() == M) responseEvent->setDirty(true);

    uint64 deliveryTime = replay ? timestamp_ + mshrLatency_ : timestamp_ + accessLatency_;
    Response resp  = {responseEvent, deliveryTime, true};
    addToOutgoingQueue(resp);
    
    if (DEBUG_ALL || DEBUG_ADDR == event->getBaseAddr()) { 
    d_->debug(_L3_,"Sending Response at cycle = %" PRIu64 ", Cmd = %s, Src = %s\n", deliveryTime, CommandString[responseEvent->getCmd()], responseEvent->getSrc().c_str());
    }
}


void L1CoherenceController::sendResponseUp(MemEvent * event, State grantedState, std::vector<uint8_t>* data, bool replay, bool finishedAtomically) {
    Command cmd = event->getCmd();
    MemEvent * responseEvent = event->makeResponse(grantedState);
    responseEvent->setDst(event->getSrc());
    bool noncacheable = event->queryFlag(MemEvent::F_NONCACHEABLE);
     
    if (!noncacheable) {
        /* Only return the desire word */
        Addr base    = (event->getAddr()) & ~(((Addr)lineSize_) - 1);
        Addr offset  = event->getAddr() - base;
        if (cmd != GetX) {
            responseEvent->setPayload(event->getSize(), &data->at(offset));
        } else {
            /* If write (GetX) and LLSC set, then check if operation was Atomic */
  	    if (finishedAtomically) responseEvent->setAtomic(true);
            else responseEvent->setAtomic(false);
        }
    } else {
        responseEvent->setPayload(*data);
    }
    //printf("Sending data up to core: ");
    if (DEBUG_ALL || DEBUG_ADDR == event->getBaseAddr()) {
        printData(data, false);
    }

    // Debugging
    uint64_t deliveryTime = timestamp_ + (replay ? mshrLatency_ : accessLatency_);
    Response resp = {responseEvent, deliveryTime, true};
    addToOutgoingQueueUp(resp);
    
    if (DEBUG_ALL || DEBUG_ADDR == event->getBaseAddr()) d_->debug(_L3_,"Sending Response at cycle = %" PRIu64 ". Current Time = %" PRIu64 ", Addr = %" PRIx64 ", Dst = %s, Size = %i, Granted State = %s\n", deliveryTime, timestamp_, event->getAddr(), responseEvent->getDst().c_str(), responseEvent->getSize(), StateString[responseEvent->getGrantedState()]);
}


/*
 *  Handles: sending writebacks
 *  Latency: cache access + tag to read data that is being written back and update coherence state
 */
void L1CoherenceController::sendWriteback(Command cmd, CacheLine* cacheLine, string origRqstr) {
    MemEvent* writeback = new MemEvent((SST::Component*)owner_, cacheLine->getBaseAddr(), cacheLine->getBaseAddr(), cmd);
    writeback->setDst(getDestination(cacheLine->getBaseAddr()));
    if (cmd == PutM || writebackCleanBlocks_) {
        writeback->setSize(cacheLine->getSize());
        writeback->setPayload(*cacheLine->getData());
        //printf("Sending writeback data: ");
        if (DEBUG_ALL || DEBUG_ADDR == cacheLine->getBaseAddr()) {
            printData(cacheLine->getData(), false);
        }
    }

    writeback->setRqstr(origRqstr);
    if (cacheLine->getState() == M) writeback->setDirty(true);
    
    
    uint64 deliveryTime = timestamp_ + accessLatency_;
    Response resp = {writeback, deliveryTime, false};
    addToOutgoingQueue(resp);
    if (DEBUG_ALL || DEBUG_ADDR == cacheLine->getBaseAddr()) d_->debug(_L3_,"Sending Writeback at cycle = %" PRIu64 ", Cmd = %s. With%s data.\n", deliveryTime, CommandString[cmd], ((cmd == PutM || writebackCleanBlocks_) ? "" : "out"));
}


/*
 *  Handles: sending AckInv responses
 *  Latency: cache access + tag to update coherence state
 */
void L1CoherenceController::sendAckInv(Addr baseAddr, string origRqstr) {
    MemEvent* ack = new MemEvent((SST::Component*)owner_, baseAddr, baseAddr, AckInv);
    ack->setDst(getDestination(baseAddr));
    ack->setRqstr(origRqstr);
    
    uint64 deliveryTime = timestamp_ + accessLatency_;
    Response resp = {ack, deliveryTime, false};
    addToOutgoingQueue(resp);
    if (DEBUG_ALL || DEBUG_ADDR == baseAddr) d_->debug(_L3_,"Sending AckInv at cycle = %" PRIu64 "\n", deliveryTime);
}



/********************
 * Helper functions
 ********************/

void L1CoherenceController::printData(vector<uint8_t> * data, bool set) {
/*    if (set)    printf("Setting data (%zu): 0x", data->size());
    else        printf("Getting data (%zu): 0x", data->size());
    
    for (unsigned int i = 0; i < data->size(); i++) {
        printf("%02x", data->at(i));
    }
    printf("\n");*/
}


/*
 *  Print stats
 */
void L1CoherenceController::printStats(int statLocation, vector<int> groupIds, map<int, CtrlStats> ctrlStats, uint64_t upgradeLatency, 
        uint64_t lat_GetS_IS, uint64_t lat_GetS_M, uint64_t lat_GetX_IM, uint64_t lat_GetX_SM,
        uint64_t lat_GetX_M, uint64_t lat_GetSEx_IM, uint64_t lat_GetSEx_SM, uint64_t lat_GetSEx_M){
    Output* dbg = new Output();
    dbg->init("", 0, 0, (Output::output_location_t)statLocation);
    dbg->output(CALL_INFO,"\n------------------------------------------------------------------------\n");
    dbg->output(CALL_INFO,"--- Cache Stats\n");
    dbg->output(CALL_INFO,"--- Name: %s\n", name_.c_str());
    dbg->output(CALL_INFO,"--- Overall Statistics\n");
    dbg->output(CALL_INFO,"------------------------------------------------------------------------\n");

    for (unsigned int i = 0; i < groupIds.size(); i++) {
        uint64_t totalMisses =  ctrlStats[groupIds[i]].newReqGetSMisses_ + ctrlStats[groupIds[i]].newReqGetXMisses_ + ctrlStats[groupIds[i]].newReqGetSExMisses_ +
                                ctrlStats[groupIds[i]].blockedReqGetSMisses_ + ctrlStats[groupIds[i]].blockedReqGetXMisses_ + ctrlStats[groupIds[i]].blockedReqGetSExMisses_;
        uint64_t totalHits =    ctrlStats[groupIds[i]].newReqGetSHits_ + ctrlStats[groupIds[i]].newReqGetXHits_ + ctrlStats[groupIds[i]].newReqGetSExHits_ +
                                ctrlStats[groupIds[i]].blockedReqGetSHits_ + ctrlStats[groupIds[i]].blockedReqGetXHits_ + ctrlStats[groupIds[i]].blockedReqGetSExHits_;

        uint64_t totalRequests = totalHits + totalMisses;
        double hitRatio = ((double)totalHits / ( totalHits + totalMisses)) * 100;
        
        if(i != 0) {
            dbg->output(CALL_INFO,"------------------------------------------------------------------------\n");
            dbg->output(CALL_INFO,"--- Cache Stats\n");
            dbg->output(CALL_INFO,"--- Name: %s\n", name_.c_str());
            dbg->output(CALL_INFO,"--- Group Statistics, Group ID = %i\n", groupIds[i]);
            dbg->output(CALL_INFO,"------------------------------------------------------------------------\n");
        }
        dbg->output(CALL_INFO,"- Total data requests:                           %" PRIu64 "\n", totalRequests);
        dbg->output(CALL_INFO,"     GetS:                                       %" PRIu64 "\n", 
                ctrlStats[groupIds[i]].newReqGetSHits_ + ctrlStats[groupIds[i]].newReqGetSMisses_ + 
                ctrlStats[groupIds[i]].blockedReqGetSHits_ + ctrlStats[groupIds[i]].blockedReqGetSMisses_);                                  
        dbg->output(CALL_INFO,"     GetX:                                       %" PRIu64 "\n", 
                ctrlStats[groupIds[i]].newReqGetXHits_ + ctrlStats[groupIds[i]].newReqGetXMisses_ + 
                ctrlStats[groupIds[i]].blockedReqGetXHits_ + ctrlStats[groupIds[i]].blockedReqGetXMisses_);                                  
        dbg->output(CALL_INFO,"     GetSEx:                                     %" PRIu64 "\n", 
                ctrlStats[groupIds[i]].newReqGetSExHits_ + ctrlStats[groupIds[i]].newReqGetSExMisses_ + 
                ctrlStats[groupIds[i]].blockedReqGetSExHits_ + ctrlStats[groupIds[i]].blockedReqGetSExMisses_);                                  
        dbg->output(CALL_INFO,"- Total misses:                                  %" PRIu64 "\n", totalMisses);
        // Report misses at the time a request was handled -> "blocked" indicates request was blocked by another pending request before being handled
        dbg->output(CALL_INFO,"     GetS, miss on arrival:                      %" PRIu64 "\n", ctrlStats[groupIds[i]].newReqGetSMisses_);
        dbg->output(CALL_INFO,"     GetS, miss after being blocked:             %" PRIu64 "\n", ctrlStats[groupIds[i]].blockedReqGetSMisses_);
        dbg->output(CALL_INFO,"     GetX, miss on arrival:                      %" PRIu64 "\n", ctrlStats[groupIds[i]].newReqGetXMisses_);
        dbg->output(CALL_INFO,"     GetX, miss after being blocked:             %" PRIu64 "\n", ctrlStats[groupIds[i]].blockedReqGetXMisses_);
        dbg->output(CALL_INFO,"     GetSEx, miss on arrival:                    %" PRIu64 "\n", ctrlStats[groupIds[i]].newReqGetSExMisses_);
        dbg->output(CALL_INFO,"     GetSEx, miss after being blocked:           %" PRIu64 "\n", ctrlStats[groupIds[i]].blockedReqGetSExMisses_);
        dbg->output(CALL_INFO,"- Total hits:                                    %" PRIu64 "\n", totalHits);
        dbg->output(CALL_INFO,"     GetS, hit on arrival:                       %" PRIu64 "\n", ctrlStats[groupIds[i]].newReqGetSHits_);
        dbg->output(CALL_INFO,"     GetS, hit after being blocked:              %" PRIu64 "\n", ctrlStats[groupIds[i]].blockedReqGetSHits_);
        dbg->output(CALL_INFO,"     GetX, hit on arrival:                       %" PRIu64 "\n", ctrlStats[groupIds[i]].newReqGetXHits_);
        dbg->output(CALL_INFO,"     GetX, hit after being blocked:              %" PRIu64 "\n", ctrlStats[groupIds[i]].blockedReqGetXHits_);
        dbg->output(CALL_INFO,"     GetSEx, hit on arrival:                     %" PRIu64 "\n", ctrlStats[groupIds[i]].newReqGetSExHits_);
        dbg->output(CALL_INFO,"     GetSEx, hit after being blocked:            %" PRIu64 "\n", ctrlStats[groupIds[i]].blockedReqGetSExHits_);
        dbg->output(CALL_INFO,"- Hit ratio:                                     %.3f%%\n", hitRatio);
        dbg->output(CALL_INFO,"- Miss ratio:                                    %.3f%%\n", 100 - hitRatio);
        dbg->output(CALL_INFO,"------------ Coherence transitions for misses -------------\n");
        dbg->output(CALL_INFO,"- GetS   I->S:                                   %" PRIu64 "\n", ctrlStats[groupIds[i]].GetS_IS);
        dbg->output(CALL_INFO,"- GetX   I->M:                                   %" PRIu64 "\n", ctrlStats[groupIds[i]].GetX_IM);
        dbg->output(CALL_INFO,"- GetX   S->M:                                   %" PRIu64 "\n", ctrlStats[groupIds[i]].GetX_SM);
        dbg->output(CALL_INFO,"- GetSEx I->M:                                   %" PRIu64 "\n", ctrlStats[groupIds[i]].GetSE_IM);
        dbg->output(CALL_INFO,"- GetSEx S->M:                                   %" PRIu64 "\n", ctrlStats[groupIds[i]].GetSE_SM);
        dbg->output(CALL_INFO,"------------ Replacements and evictions -------------------\n");
        dbg->output(CALL_INFO,"- PutS sent due to eviction:                     %" PRIu64 "\n", stats_[groupIds[i]].EvictionPUTSReqSent_);
        dbg->output(CALL_INFO,"- PutE sent due to eviction:                     %" PRIu64 "\n", stats_[groupIds[i]].EvictionPUTEReqSent_);
        dbg->output(CALL_INFO,"- PutM sent due to eviction:                     %" PRIu64 "\n", stats_[groupIds[i]].EvictionPUTMReqSent_);
        dbg->output(CALL_INFO,"------------ Other stats ----------------------------------\n");
        dbg->output(CALL_INFO,"- Inv stalled because LOCK held:                 %" PRIu64 "\n", ctrlStats[groupIds[i]].InvWaitingForUserLock_);
        dbg->output(CALL_INFO,"- Requests received (incl coherence traffic):    %" PRIu64 "\n", ctrlStats[groupIds[i]].TotalRequestsReceived_);
        dbg->output(CALL_INFO,"- Requests handled by MSHR (MSHR hits):          %" PRIu64 "\n", ctrlStats[groupIds[i]].TotalMSHRHits_);
        dbg->output(CALL_INFO,"- NACKs sent (MSHR Full, Down):                  %" PRIu64 "\n", stats_[groupIds[i]].NACKsSentDown_);
        dbg->output(CALL_INFO,"- NACKs sent (MSHR Full, Up):                    %" PRIu64 "\n", stats_[groupIds[i]].NACKsSentUp_);
        dbg->output(CALL_INFO,"------------ Latency stats --------------------------------\n");
        dbg->output(CALL_INFO,"- Avg Miss Latency (cyc):                        %" PRIu64 "\n", upgradeLatency);
        if (ctrlStats[groupIds[0]].GetS_IS > 0) 
            dbg->output(CALL_INFO,"- Latency GetS   I->S:                           %" PRIu64 "\n", (lat_GetS_IS / ctrlStats[groupIds[0]].GetS_IS));
        if (ctrlStats[groupIds[0]].GetX_IM > 0)
            dbg->output(CALL_INFO,"- Latency GetX   I->M:                           %" PRIu64 "\n", (lat_GetX_IM / ctrlStats[groupIds[0]].GetX_IM));
        if (ctrlStats[groupIds[0]].GetX_SM > 0)
            dbg->output(CALL_INFO,"- Latency GetX   S->M:                           %" PRIu64 "\n", (lat_GetX_SM / ctrlStats[groupIds[0]].GetX_SM));
        if (ctrlStats[groupIds[0]].GetSE_IM > 0)
            dbg->output(CALL_INFO,"- Latency GetSEx I->M:                           %" PRIu64 "\n", (lat_GetSEx_IM / ctrlStats[groupIds[0]].GetSE_IM));
        if (ctrlStats[groupIds[0]].GetSE_SM > 0)
            dbg->output(CALL_INFO,"- Latency GetSEx S->M:                           %" PRIu64 "\n", (lat_GetSEx_SM / ctrlStats[groupIds[0]].GetSE_SM));
    }
    dbg->output(CALL_INFO,"------------ State and event stats ---------------------------\n");
    for (int i = 0; i < LAST_CMD; i++) {
        for (int j = 0; j < LAST_CMD; j++) {
            if (stateStats_[i][j] == 0) continue;
            dbg->output(CALL_INFO,"%s, %s:        %" PRIu64 "\n", CommandString[i], StateString[j], stateStats_[i][j]);
        }
    }
}

void L1CoherenceController::printStatsForMacSim(int statLocation, vector<int> groupIds, map<int, CtrlStats> ctrlStats, uint64_t upgradeLatency, 
        uint64_t lat_GetS_IS, uint64_t lat_GetS_M, uint64_t lat_GetX_IM, uint64_t lat_GetX_SM,
        uint64_t lat_GetX_M, uint64_t lat_GetSEx_IM, uint64_t lat_GetSEx_SM, uint64_t lat_GetSEx_M){
    stringstream ss;
    ss << name_.c_str() << ".stat.out";
    string filename = ss.str();

    ofstream ofs;
    ofs.exceptions(std::ofstream::eofbit | std::ofstream::failbit | std::ofstream::badbit);
    ofs.open(filename.c_str(), std::ios_base::out);

    for (unsigned int i = 0; i < groupIds.size(); ++i) {
        uint64_t totalMisses =  ctrlStats[groupIds[i]].newReqGetSMisses_ + ctrlStats[groupIds[i]].newReqGetXMisses_ + ctrlStats[groupIds[i]].newReqGetSExMisses_ +
                                ctrlStats[groupIds[i]].blockedReqGetSMisses_ + ctrlStats[groupIds[i]].blockedReqGetXMisses_ + ctrlStats[groupIds[i]].blockedReqGetSExMisses_;
        uint64_t totalHits =    ctrlStats[groupIds[i]].newReqGetSHits_ + ctrlStats[groupIds[i]].newReqGetXHits_ + ctrlStats[groupIds[i]].newReqGetSExHits_ +
                                ctrlStats[groupIds[i]].blockedReqGetSHits_ + ctrlStats[groupIds[i]].blockedReqGetXHits_ + ctrlStats[groupIds[i]].blockedReqGetSExHits_;

        uint64_t totalRequests = totalHits + totalMisses;
        double hitRatio = ((double)totalHits / ( totalHits + totalMisses)) * 100;

        writeTo(ofs, name_, string("Total_data_requests"), totalRequests);
        writeTo(ofs, name_, string("GetS"),
                ctrlStats[groupIds[i]].newReqGetSHits_ + ctrlStats[groupIds[i]].newReqGetSMisses_ +
                ctrlStats[groupIds[i]].blockedReqGetSHits_ + ctrlStats[groupIds[i]].blockedReqGetSMisses_);
        writeTo(ofs, name_, string("GetX"),
                ctrlStats[groupIds[i]].newReqGetXHits_ + ctrlStats[groupIds[i]].newReqGetXMisses_ +
                ctrlStats[groupIds[i]].blockedReqGetXHits_ + ctrlStats[groupIds[i]].blockedReqGetXMisses_);
        writeTo(ofs, name_, string("GetSEx"),
                ctrlStats[groupIds[i]].newReqGetSExHits_ + ctrlStats[groupIds[i]].newReqGetSExMisses_ +
                ctrlStats[groupIds[i]].blockedReqGetSExHits_ + ctrlStats[groupIds[i]].blockedReqGetSExMisses_);

        writeTo(ofs, name_, string("Total_misses"), totalMisses);
        // Report misses at the time a request was handled -> "blocked" indicates request was blocked by another pending request before being handled
        writeTo(ofs, name_, string("GetS_miss_on_arrival"),            ctrlStats[groupIds[i]].newReqGetSMisses_);
        writeTo(ofs, name_, string("GetS_miss_after_being_blocked"),   ctrlStats[groupIds[i]].blockedReqGetSMisses_);
        writeTo(ofs, name_, string("GetX_miss_on_arrival"),            ctrlStats[groupIds[i]].newReqGetXMisses_);
        writeTo(ofs, name_, string("GetX_miss_after_being_blocked"),   ctrlStats[groupIds[i]].blockedReqGetXMisses_);
        writeTo(ofs, name_, string("GetSEx_miss_on_arrival"),          ctrlStats[groupIds[i]].newReqGetSExMisses_);
        writeTo(ofs, name_, string("GetSEx_miss_after_being_blocked"), ctrlStats[groupIds[i]].blockedReqGetSExMisses_);

        writeTo(ofs, name_, string("Total_hits"), totalHits);
        writeTo(ofs, name_, string("GetS_hit_on_arrival"),            ctrlStats[groupIds[i]].newReqGetSHits_);
        writeTo(ofs, name_, string("GetS_hit_after_being_blocked"),   ctrlStats[groupIds[i]].blockedReqGetSHits_);
        writeTo(ofs, name_, string("GetX_hit_on_arrival"),            ctrlStats[groupIds[i]].newReqGetXHits_);
        writeTo(ofs, name_, string("GetX_hit_after_being_blocked"),   ctrlStats[groupIds[i]].blockedReqGetXHits_);
        writeTo(ofs, name_, string("GetSEx_hit_on_arrival"),          ctrlStats[groupIds[i]].newReqGetSExHits_);
        writeTo(ofs, name_, string("GetSEx_hit_after_being_blocked"), ctrlStats[groupIds[i]].blockedReqGetSExHits_);

        writeTo(ofs, name_, string("Hit_ratio"), hitRatio);
        writeTo(ofs, name_, string("Miss_ratio"), 100 - hitRatio);

        // Coherence transitions for misses 
        writeTo(ofs, name_, string("GetS_I->S"),   ctrlStats[groupIds[i]].GetS_IS);
        writeTo(ofs, name_, string("GetX_I->M"),   ctrlStats[groupIds[i]].GetX_IM);
        writeTo(ofs, name_, string("GetX_S->M"),   ctrlStats[groupIds[i]].GetX_SM);
        writeTo(ofs, name_, string("GetSEx_I->M"), ctrlStats[groupIds[i]].GetSE_IM);
        writeTo(ofs, name_, string("GetSEx_S->M"), ctrlStats[groupIds[i]].GetSE_SM);

        // Replacements and evictions
        writeTo(ofs, name_, string("PutS_sent_due_to_eviction"), stats_[groupIds[i]].EvictionPUTSReqSent_);
        writeTo(ofs, name_, string("PutE_sent_due_to_eviction"), stats_[groupIds[i]].EvictionPUTEReqSent_);
        writeTo(ofs, name_, string("PutM_sent_due_to_eviction"), stats_[groupIds[i]].EvictionPUTMReqSent_);

        // Other stats
        writeTo(ofs, name_, string("Inv_stalled_because_LOCK_held"),               ctrlStats[groupIds[i]].InvWaitingForUserLock_);
        writeTo(ofs, name_, string("Requests_received_(incl_coherence_traffic)"),  ctrlStats[groupIds[i]].TotalRequestsReceived_);
        writeTo(ofs, name_, string("Requests_handled_by_MSHR_(MSHR_hits)"),        ctrlStats[groupIds[i]].TotalMSHRHits_);
        writeTo(ofs, name_, string("NACKs_sent_(MSHR_Full,Down)"),                 stats_[groupIds[i]].NACKsSentDown_);
        writeTo(ofs, name_, string("NACKs_sent_(MSHR_Full,Up)"),                   stats_[groupIds[i]].NACKsSentUp_);

        writeTo(ofs, name_, string("Pref_Requests_received"),  ctrlStats[groupIds[i]].newPrefReqs_);

        // Latency stats
        writeTo(ofs, name_, string("Avg_Miss_Latency_(cyc)"), upgradeLatency);
        if (ctrlStats[groupIds[0]].GetS_IS  > 0) writeTo(ofs, name_, string("Latency_GetS_I->S"),   (lat_GetS_IS / ctrlStats[groupIds[0]].GetS_IS));
        if (ctrlStats[groupIds[0]].GetX_IM  > 0) writeTo(ofs, name_, string("Latency_GetX_I->M"),   (lat_GetX_IM / ctrlStats[groupIds[0]].GetX_IM));
        if (ctrlStats[groupIds[0]].GetX_SM  > 0) writeTo(ofs, name_, string("Latency_GetX_S->M"),   (lat_GetX_SM / ctrlStats[groupIds[0]].GetX_SM));
        if (ctrlStats[groupIds[0]].GetSE_IM > 0) writeTo(ofs, name_, string("Latency_GetSEx_I->M"), (lat_GetSEx_IM / ctrlStats[groupIds[0]].GetSE_IM));
        if (ctrlStats[groupIds[0]].GetSE_SM > 0) writeTo(ofs, name_, string("Latency_GetSEx_S->M"), (lat_GetSEx_SM / ctrlStats[groupIds[0]].GetSE_SM));
    }

    // State and event stats
#include <boost/format.hpp>
    for (int i = 0; i < LAST_CMD; i++) {
        for (int j = 0; j < LAST_CMD; j++) {
            if (stateStats_[i][j] == 0) continue;
            writeTo(ofs, name_, str(boost::format("%1%_%2%") % CommandString[i] % StateString[j]), stateStats_[i][j]);
        }
    }
    ofs.close();
}

