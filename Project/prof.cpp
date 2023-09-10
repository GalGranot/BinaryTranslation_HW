//prof.cpp

/*=============================================================================
* fixme
=============================================================================*/
/*
* some edges get 0x0 address for some reason
* if trying to access rtn's names, all addresses go to 0x0
*/

/*=============================================================================
* include, using
=============================================================================*/
#include "pin.H"
extern "C" {
#include "xed-interface.h"
}
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <iomanip>
#include <fstream>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <values.h>
#include <vector>

using namespace std;

/*=============================================================================
* command line switches
=============================================================================*/
KNOB<BOOL>   KnobVerbose(KNOB_MODE_WRITEONCE,    "pintool",
    "verbose", "0", "Verbose run");

KNOB<BOOL>   KnobDumpTranslatedCode(KNOB_MODE_WRITEONCE,    "pintool",
    "dump_tc", "0", "Dump Translated Code");

KNOB<BOOL>   KnobDoNotCommitTranslatedCode(KNOB_MODE_WRITEONCE,    "pintool",
    "no_tc_commit", "0", "Do not commit translated code");

KNOB<BOOL> KnobProf(KNOB_MODE_WRITEONCE, "pintool", "prof", "0", "Enable profiling");
KNOB<BOOL> KnobOpt(KNOB_MODE_WRITEONCE, "pintool", "opt", "0", "Enable optimization by reordering");

/*=============================================================================
* classes
=============================================================================*/
class Edge
{
public:
    ADDRINT srcBBLHead;
    ADDRINT source;         // also srcBBLTail
    ADDRINT destination;    // also destBBLHead
    // ADDRINT destBBLTail;
    ADDRINT fallThrough;
    ADDRINT rtnAddress;
    UINT64 takenCount;
    UINT64 notTakenCount;
    bool singleSource;
    bool sharingTarget;

    Edge() : srcBBLHead(0), source(0), destination(0), fallThrough(0), rtnAddress(0), takenCount(0), notTakenCount(0), 
    singleSource(true), sharingTarget(false) {}

    Edge(ADDRINT srcBBLHead, ADDRINT source, ADDRINT destination, ADDRINT fallThrough, ADDRINT rtnAddress) : 
    srcBBLHead (srcBBLHead), source(source), destination(destination), fallThrough(fallThrough), rtnAddress(rtnAddress), 
    takenCount(0), singleSource(true), sharingTarget(false) {}
};

/*=============================================================================
* global variables
=============================================================================*/
ofstream outFile;
unordered_map<ADDRINT, Edge> edgesMap;

/*=============================================================================
* global functions
=============================================================================*/
void printEdge(const Edge& e)
{
    outFile << hex
        << "0x" << e.srcBBLHead << ","
        << "0x" << e.source << ","
        << "0x" << e.destination << ","
        << "0x" << e.fallThrough << ","
        << "0x" << e.rtnAddress << "," 
        << dec
        << e.takenCount << ","
        << e.notTakenCount << "," 
        << e.singleSource << "," 
        << e.sharingTarget << endl;
}

bool compareEdgePtr(const Edge& e1, const Edge& e2) { return e1.takenCount > e2.takenCount; }

void setSingleSource()
{
    for (const auto& pair : edgesMap)
    {
        const Edge& edge = pair.second;
        // if target is in the map, and shared by more than one edge
        if ( edgesMap.count(edge.destination) && edge.sharingTarget) 
        {
            edgesMap[edge.destination].singleSource = false; // because the target has more than 1 sources.
        }
    }
    return;
}

/*FIXME vector<Edge>*/ void findTargetEdges()
{
    // vector<Edge> result;
    unordered_map<ADDRINT, vector<Edge>> edgesByRtnMap;
    for (const auto& pair : edgesMap)
    {
        Edge edge = pair.second;
        // if ((edge.takenCount > edge.notTakenCount)) // commented out for debugging
            edgesByRtnMap[edge.rtnAddress].push_back(edge);
    }
    for (const auto& pair : edgesByRtnMap)
    {
        vector<Edge> rtnEdges = pair.second;
        if (rtnEdges.empty())
            continue;
        std::sort(rtnEdges.begin(), rtnEdges.end(), compareEdgePtr);
        for (const auto& e : rtnEdges)
            printEdge(e);
    }
}

/*=============================================================================
* pintool functions
=============================================================================*/
VOID doCountEdge(INT32 taken, VOID* address)
{
    Edge* edgePtr = (Edge*)address;
    if (taken)
        (*edgePtr).takenCount++;
    else
        (*edgePtr).notTakenCount++;
}


VOID Trace(TRACE trc, VOID* v)
{
    IMG img = IMG_FindByAddress(TRACE_Address(trc));
    if (!IMG_Valid(img) || !IMG_IsMainExecutable(img))
        return;

    for (BBL bbl = TRACE_BblHead(trc); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        if (!BBL_HasFallThrough(bbl)) // FIXME: should we really filter out jumps that do not have a fallthrough?
            continue;
        INS insTail = BBL_InsTail(bbl);
        INS insHead = BBL_InsHead(bbl);

        if (!INS_IsDirectControlFlow(insTail)) // dont take into account because there is no branching, or target might not be constant.
            continue;
        
        ADDRINT tailAddress = INS_Address(insTail);
        ADDRINT headAddress = INS_Address(insHead);

        if (edgesMap.find(headAddress) == edgesMap.end()) // edge not found. create it.
        {
            ADDRINT insFallThroughAddress = INS_NextAddress(insTail);
            ADDRINT targetAddress = INS_DirectControlFlowTargetAddress(insTail);

            //ignore edges which connect different rtns. 
            // this also ignores calls (?)
            RTN sourceRtn = RTN_FindByAddress(tailAddress);
            RTN targetRtn = RTN_FindByAddress(targetAddress);
            if (RTN_Id(sourceRtn) != RTN_Id(targetRtn)) // FIXME: maybe we also want to filter out recursions?
                continue;

            Edge edge(headAddress, tailAddress, targetAddress, insFallThroughAddress, RTN_Address(INS_Rtn(insTail)));
            for (auto& pair : edgesMap)
            {
                Edge& currEdge = pair.second;
                if (currEdge.destination == targetAddress) // means that we saw another edge that arrives to the same bbl
                {
                    // if target is self, do not consider it as sharing.
                    if (currEdge.destination != currEdge.srcBBLHead && targetAddress != headAddress)
                    {
                        currEdge.sharingTarget = true; // mark both edges which share the same target.
                        edge.sharingTarget = true;
                        // edgesMap[targetAddress].singleSource = false; // because the target has more than 1 sources.
                    }
                }
            }
            edgesMap[headAddress] = edge;
        }
        else //edge found
        {
            ; // for now does nothing.
        }

        INS_InsertCall(insTail, IPOINT_BEFORE, (AFUNPTR)doCountEdge, IARG_BRANCH_TAKEN, IARG_PTR, &(edgesMap[headAddress]), IARG_END);
    }
}

INT32 Usage()
{
    cerr << "This Pintool counts the number of times a routine is executed" << endl;
    cerr << "and the number of instructions executed in a routine" << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

VOID FiniProf(INT32 code, VOID* v)
{
    outFile << "source bbl head,source,destination,fallthrough,rtn address,taken count,not taken count,single source, sharing target" << endl;
    setSingleSource(); 
    findTargetEdges(); 
}

/*=============================================================================
* main
=============================================================================*/
int main(int argc, char* argv[])
{
    PIN_InitSymbols();
    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();

    if (KnobProf)
    {
        outFile.open("edge-count.csv");
        TRACE_AddInstrumentFunction(Trace, 0);

        // Register FiniProf to be called when the application exits
        PIN_AddFiniFunction(FiniProf, 0);
    }

    // Register Instruction function to be called to instrument ins
    //INS_AddInstrumentFunction(Instruction, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}