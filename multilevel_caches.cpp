#include <iostream>
#include <fstream> 
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "pin.H"

using namespace std;

static bool l2_on = false;
static bool l1_victim_cache_on = false;
static bool l2_victim_cache_on = false;

class CacheModel
{
    protected:
        UINT32 logNumRows; // number of rows in the tag, validBit, accessed arrays
        UINT32 logBlockSize; // based on block size of cache (log2(blocksize)); block offset within the cache
        UINT32 associativity;
        UINT64 readReqs; // number of read requests
        UINT64 writeReqs; // write requests
        UINT64 readHits; // successful read requests - means that tag portion of address was matched in cache
        UINT64 writeHits; // successful write requests - means that tag portion of address was matched in cache
        UINT32** tag; // 2d array of all the tag bits of every address
        UINT32** addresses; // 2d array of all the tag bits of every address
        bool** validBit; // see if value exists within that set + tag
        UINT32** hits;
        UINT64 instrCount;
        UINT32 tagAddress;
        UINT32 index;

    public:
        //Constructor for a cache
        CacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
        {
            this->logNumRows = logNumRowsParam;
            this->logBlockSize = logBlockSizeParam;
            this->associativity = associativityParam;
            this->readReqs = 0;
            this->writeReqs = 0;
            this->readHits = 0;
            this->writeHits = 0;
            this->tag = new UINT32*[1u<<this->logNumRows];
            this->addresses = new UINT32*[1u<<this->logNumRows];
            this->validBit = new bool*[1u<<this->logNumRows];
            this->hits = new UINT32*[1u<<this->logNumRows]; // added
            this->instrCount = 0;
            this->tagAddress = 0;
            this->index = 0;

            for (UINT32 i = 0; i < 1u<<this->logNumRows; i++) {
                this->tag[i] = new UINT32[this->associativity]; // width of tag depends on how many can be saved
                this->addresses[i] = new UINT32[this->associativity]; // width of tag depends on how many can be saved
                this->validBit[i] = new bool[this->associativity]; // how many valid bits can be stored
                this->hits[i] = new UINT32[this->associativity]; // added
                for(UINT32 j = 0; j < this->associativity; j++){
                    this->validBit[i][j] = false; // false means miss
                    this->hits[i][j] = 0; // added
                }
            }
        }

        virtual void setTagAddress(UINT32 address) = 0;
        virtual void setIndex(UINT32 address) = 0;

        virtual bool readHit(UINT32 address) = 0;
        virtual bool writeHit(UINT32 address) = 0;

        virtual void update(UINT32 address) = 0;

        //Call this function to update the cache state whenever data is read
        virtual bool readReq(UINT32 address) = 0;

        //Call this function to update the cache state whenever data is written
        virtual bool writeReq(UINT32 address) = 0;

        //Do not modify this function
        void dumpResults(ofstream *outfile)
        {
            float readPercent = (float)(this->readHits)/(float)(this->readReqs) * 100;
            float writePercent = (float)(this->writeHits)/(float)(this->writeReqs) * 100;
        	*outfile << (this->readReqs) <<","<< (this->writeReqs) <<","<< (this->readHits) <<","<< (this->writeHits) <<"\n" << "Read Percent: " << readPercent << "%\n" << "Write Percent: " << writePercent << "%\nNumber of Instructions: " << this->instrCount << "\n\n";
        	// *outfile << readReqs <<","<< writeReqs <<","<< readHits <<","<< writeHits <<"\n";
        }
};

class LruCacheModel: public CacheModel
{
    protected:
        bool victimOn;
        LruCacheModel* victim_cache;

    public:
        LruCacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam, bool victimOn)
            : CacheModel(logNumRowsParam, logBlockSizeParam, associativityParam)
        {
            this->victimOn = victimOn;
            if (this->victimOn) {
                this->victim_cache = new LruCacheModel(0, logBlockSizeParam, 32, false);
            }
        }

        void setTagAddress(UINT32 address){
            UINT32 offset = 2 + this->logBlockSize; // block offset + word offset
            UINT32 adjustedAddr = address >> offset;

            this->tagAddress = adjustedAddr >> this->logNumRows; // gets the tag portion
        }

        void setIndex(UINT32 address){
            UINT32 offset = 2 + this->logBlockSize; // block offset + word offset
            UINT32 adjustedAddr = address >> offset;

            UINT32 maxNumRows = 1u << this->logNumRows; // number of rows (and by virtue the max index)
            this->index = adjustedAddr % maxNumRows; // gets the index in the cache for the address
        }

        bool readHit(UINT32 address){
            bool hit = false;
            UINT32 tagAddress = this->tagAddress;
            UINT32 index = this->index;
            for (UINT32 i = 0; i < this->associativity; i++) {
                // has a hit
                if (this->validBit[index][i] == true && this->tag[index][i] == tagAddress) { // check if it matches the value in tag
                    hit = true; // means that the tag 2D array has the adjusted address
                    this->readHits++; // increment
                    // stores at which point in the instructions that this tag address was hit
                    // hits[][] used for the Least Recently Used portion of logic
                    this->hits[index][i] = ++(this->instrCount); // storing the instruction count for the number of times the address is being hit
                    break;
                }
            }
            return hit;
        }

        bool writeHit(UINT32 address){
            bool hit = false;
            UINT32 tagAddress = this->tagAddress;
            UINT32 index = this->index;
            for (UINT32 i = 0; i < this->associativity; i++) {
                // has a hit
                if (this->validBit[index][i] == true && this->tag[index][i] == tagAddress) { // check if it matches the value in tag
                    hit = true; // means that the tag 2D array has the adjusted address
                    this->writeHits++; // increment
                    // stores at which point in the instructions that this tag address was hit
                    // hits[][] used for the Least Recently Used portion of logic
                    this->hits[index][i] = ++(this->instrCount); // storing the instruction count for the number of times the address is being hit
                    break;
                }
            }
            return hit;
        }

        void update(UINT32 address){
            // hits[][] stores when the last time that address was hit
            UINT32 min = this->hits[index][0];
            UINT32 minIndex = 0;
            UINT32 tagAddress = this->tagAddress;
            UINT32 index = this->index;

            // try to find the cache to store the address in (its tag and validBit and its associated index)
            // finds cache with least recently used hit cache
            for(UINT32 i = 1; i < this->associativity; i++){ // iterate through all the hits[index] array and if its less than the index min
                // lower the hits[index][i] the longer ago it was used (so least recently used)
                if(this->hits[index][i] < min){
                    min = this->hits[index][i];
                    minIndex = i;
                }
            }

            // storing address in cache
            if (this->validBit[index][minIndex]) {
                if (this->victimOn) {
                    this->victim_cache->setTagAddress(addresses[index][minIndex]);
                    this->victim_cache->setIndex(addresses[index][minIndex]);
                    this->victim_cache->update(addresses[index][minIndex]);
                }
            } else {
                this->validBit[index][minIndex] = true; // now would be a hit if it gets a that specific index
            }
            this->tag[index][minIndex] = tagAddress;
            this->addresses[index][minIndex] = address;
        }

        // Cache = 
        // index: [valid - 1 bit][tag][data from MM]
        // address = [tag][index][block offset]

        /**
         * @brief read request from the processor to the cache and then update the approrpiate cache metadata according to the cache type
         * 
         * @param address
         */
        bool readReq(UINT32 address){
            this->readReqs++; // increment number of read requests

            bool hit = this->readHit(address);

            // if not in cache
            if (!hit) {
                if (this->victimOn) {
                    // check victim cache for address
                    this->victim_cache->rReqIncrease();
                    this->victim_cache->setTagAddress(address);
                    this->victim_cache->setIndex(address);
                    hit = this->victim_cache->readHit(address);
                }
                this->update(address);
            }
            return hit;
        }
        
        /**
         * @brief write request from the processor to the cahce and then will update the appropriate metadata according to the cache type
         * 
         * @param address 
         */
        bool writeReq(UINT32 address)
        {
            this->writeReqs++; // increment number of write requests

            bool hit = this->writeHit(address);

            // if not in cache
            if (!hit) {
                if (this->victimOn) {
                    // check victim cache for address
                    this->victim_cache->wReqIncrease();
                    this->victim_cache->setTagAddress(address);
                    this->victim_cache->setIndex(address);
                    hit = this->victim_cache->writeHit(address);
                }
                this->update(address);
            }
            return hit;
        }

        void dumpVictim(ofstream *outfile) {
            this->victim_cache->dumpResults(outfile);
        }

        void rReqIncrease() {
            this->readReqs++;
        }

        void wReqIncrease() {
            this->writeReqs++;
        }
};

static LruCacheModel* cache_L1;
static LruCacheModel* cache_L2;

//Cache analysis routine
void cacheLoad(UINT32 address)
{
    //Here the address is aligned to a word boundary
    address = (address >> 2) << 2;

    // set the tag address and index for every address and cache
    cache_L1->setTagAddress(address);
    cache_L1->setIndex(address);
    bool isHit = cache_L1->readReq(address);

    if (l2_on && !isHit) {
        cache_L2->setTagAddress(address);
        cache_L2->setIndex(address);
        cache_L2->readReq(address);
    }
}

//Cache analysis routine
void cacheStore(UINT32 address)
{
    //Here the address is aligned to a word boundary
    address = (address >> 2) << 2;

    // set the tag address and index for every address and cache
    cache_L1->setTagAddress(address);
    cache_L1->setIndex(address);
    bool isHit = cache_L1->writeReq(address);

    if (l2_on && !isHit) {
        cache_L2->setTagAddress(address);
        cache_L2->setIndex(address);
        cache_L2->writeReq(address);
    }
}

// // This knob will set the outfile name
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
			    "o", "results.out", "specify optional output file name");

// Levels of Caches -----------------------------------------------------
/**
 * @brief Level 1 Cache - About 32KB (a little more)
 * 
 */

// This knob will set the param logMemSize
KNOB<UINT32> KnoblogMemSize_L1(KNOB_MODE_WRITEONCE, "pintool",
                "m_L1", "16", "specify the log of physical memory size in bytes");

// This knob will set the cache param logNumRows
KNOB<UINT32> KnobLogNumRows_L1(KNOB_MODE_WRITEONCE, "pintool",
                "r_L1", "10", "specify the log of number of rows in the cache");

// This knob will set the cache param logBlockSize
KNOB<UINT32> KnobLogBlockSize_L1(KNOB_MODE_WRITEONCE, "pintool",
                "b_L1", "5", "specify the log of block size of the cache in bytes");

// This knob will set the cache param associativity
KNOB<UINT32> KnobAssociativity_L1(KNOB_MODE_WRITEONCE, "pintool",
                "a_L1", "1", "specify the associativity of the cache");

/**
 * @brief Level 2 Cache - About 512KB (a little more)
 * 
 */

// This knob will set the param logMemSize
KNOB<UINT32> KnoblogMemSize_L2(KNOB_MODE_WRITEONCE, "pintool",
                "m_L2", "16", "specify the log of physical memory size in bytes");

// This knob will set the cache param logNumRows
KNOB<UINT32> KnobLogNumRows_L2(KNOB_MODE_WRITEONCE, "pintool",
                "r_L2", "12", "specify the log of number of rows in the cache");

// This knob will set the cache param logBlockSize
KNOB<UINT32> KnobLogBlockSize_L2(KNOB_MODE_WRITEONCE, "pintool",
                "b_L2", "5", "specify the log of block size of the cache in bytes");

// This knob will set the cache param associativity
KNOB<UINT32> KnobAssociativity_L2(KNOB_MODE_WRITEONCE, "pintool",
                "a_L2", "4", "specify the associativity of the cache");

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
    if (INS_IsMemoryRead(ins))
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)cacheLoad, IARG_MEMORYREAD_EA, IARG_END);
    if (INS_IsMemoryWrite(ins))
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)cacheStore, IARG_MEMORYWRITE_EA, IARG_END);
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    ofstream outfile;
    outfile.open(KnobOutputFile.Value().c_str());
    outfile.setf(ios::showbase);
    outfile << "physical index physical tag: \n\n";
    outfile << "Level 1 Cache: ";
    cache_L1->dumpResults(&outfile);
    if (l1_victim_cache_on) {
        outfile << "L1 Victim Cache: ";
        cache_L1->dumpVictim(&outfile);
    }
    if (l2_on) {
        outfile << "Level 2 Cache: ";
        cache_L2->dumpResults(&outfile);
    }
    if (l2_victim_cache_on) {
        outfile << "L2 Victim Cache: ";
        cache_L2->dumpVictim(&outfile);
    }

    outfile.close();
}

// argc, argv are the entire command line, including pin -t <toolname> -- ...
int main(int argc, char * argv[])
{
    // Initialize pin
    PIN_Init(argc, argv);

    cache_L1 = new LruCacheModel(KnobLogNumRows_L1.Value(), KnobLogBlockSize_L1.Value(), KnobAssociativity_L1.Value(), l1_victim_cache_on);

    if (l2_on) {
        cache_L2 = new LruCacheModel(KnobLogNumRows_L2.Value(), KnobLogBlockSize_L2.Value(), KnobAssociativity_L2.Value(), l2_victim_cache_on);
    }

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}