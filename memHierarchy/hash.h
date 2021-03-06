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

/*
 * File:   hash.h
 * Author: Caesar De la Paz III
 * Email:  caesar.sst@gmail.com
 */


#ifndef HASH_H
#define	HASH_H

#include <sst_config.h>
#include <stdint.h>


namespace SST{ namespace MemHierarchy{

class HashFunction{
public:
    HashFunction() {};
    virtual ~HashFunction() {};

    virtual uint64_t hash(uint32_t _ID, uint64_t _value) = 0;
};

class SHA1HashFunction : public HashFunction {
private:
    uint64_t dynamicValues_;
    uint32_t* dynamicHashes_;
    int numFunctions_;
    int numPasses_;
public:
    SHA1HashFunction(int _numFunctions);
    uint64_t hash(uint32_t _ID, uint64_t _value);
};

class H3HashFunction : public HashFunction {
private:
    uint64_t* hashMatrixPt_;
    uint32_t resShift_;
    uint32_t numFunctions_;
public:
    H3HashFunction( uint32_t _numFunctions, uint32_t _outputBits, uint64_t _randomSeed = 123132127);
    uint64_t hash(uint32_t _ID, uint64_t _value);
};

/* Simplest ID hashing */
class PureIdHashFunction : public HashFunction {
public:
    inline uint64_t hash(uint32_t _ID, uint64_t _value) {
        return _value;
    }
};

}}
#endif	
/* HASH_H */
