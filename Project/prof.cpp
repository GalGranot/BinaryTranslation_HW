//prof.cpp

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

/*======================================================================*/
/* commandline switches                                                 */
/*======================================================================*/
KNOB<BOOL>   KnobVerbose(KNOB_MODE_WRITEONCE,    "pintool",
    "verbose", "0", "Verbose run");

KNOB<BOOL>   KnobDumpTranslatedCode(KNOB_MODE_WRITEONCE,    "pintool",
    "dump_tc", "0", "Dump Translated Code");

KNOB<BOOL>   KnobDoNotCommitTranslatedCode(KNOB_MODE_WRITEONCE,    "pintool",
    "no_tc_commit", "0", "Do not commit translated code");

KNOB<BOOL> KnobProf(KNOB_MODE_WRITEONCE, "pintool", "prof", "0", "Enable profiling");

/*=============================================================================
* classes
=============================================================================*/

class Edge
{
public:
    ADDRINT source;
    ADDRINT destination;
    ADDRINT fallThrough;
    ADDRINT rtnAddress;
    UINT64 takenCount;
    UINT64 notTakenCount;
    bool singleSource;

    Edge()
    {
        this->source = 0;
        this->destination = 0;
        this->fallThrough = 0;
        this->rtnAddress = 0;
        this->takenCount = 0;
        this->notTakenCount = 0;
        this->singleSource = 0;
    }
    Edge(ADDRINT source, ADDRINT destination, ADDRINT fallThrough, ADDRINT rtnAddress) : source(source), destination(destination),
        fallThrough(fallThrough), rtnAddress(rtnAddress), takenCount(0), singleSource(true) {}
};

/*=============================================================================
* global variables
=============================================================================*/
ofstream outFile;
unordered_map<ADDRINT, Edge> edgesMap;

/*=============================================================================
* global functions
=============================================================================*/
bool compareEdgePtr(const Edge& e1, const Edge& e2) { return e1.takenCount > e2.takenCount; }

void printEdge(const Edge& e)
{
    outFile << "0x" << hex << e.source << ", "
        << "0x" << e.destination << ", "
        << "0x" << e.fallThrough << ", "
        << "0x" << e.rtnAddress << ", " << dec
        << e.takenCount << ", "
        << e.notTakenCount << ", " << e.singleSource << endl;
}


/*FIXME vector<Edge>*/ void findTargetEdges()
{
    vector<Edge> result;
    unordered_map<ADDRINT, vector<Edge>> edgesByRtnMap;
    for (const auto& pair : edgesMap)
    {
        Edge edge = pair.second;
        if ((edge.takenCount > edge.notTakenCount))
            edgesByRtnMap[edge.rtnAddress].push_back(edge);
    }
    for (const auto& pair : edgesByRtnMap)
    {
        vector<Edge> rtnEdges = pair.second;
        if (rtnEdges.empty())
            continue;
        std::sort(rtnEdges.begin(), rtnEdges.end(), compareEdgePtr);
        for (const auto& e : rtnEdges)
        {
            printEdge(e);
        }
    }
}

VOID doCountEdge(INT32 taken, VOID* address)
{
    Edge* edgePtr = (Edge*)address;
    if (taken)
        (*edgePtr).takenCount++;
    else
        (*edgePtr).notTakenCount++;
}

/*=============================================================================
* pintool functions
=============================================================================*/
VOID Trace(TRACE trc, VOID* v)
{
    IMG img = IMG_FindByAddress(TRACE_Address(trc));
    if (!IMG_Valid(img) || !IMG_IsMainExecutable(img))
        return;

    for (BBL bbl = TRACE_BblHead(trc); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        if (!BBL_HasFallThrough(bbl))
            continue;
        INS insTail = BBL_InsTail(bbl);
        if (!INS_IsDirectControlFlow(insTail))
            continue;
        ADDRINT tailAddress = INS_Address(insTail);
        if (edgesMap.find(tailAddress) != edgesMap.end())
        {
            ADDRINT insFallThroughAddress = INS_NextAddress(insTail);
            ADDRINT targetAddress = INS_DirectControlFlowTargetAddress(insTail);
            Edge edge(tailAddress, targetAddress, insFallThroughAddress, RTN_Address(INS_Rtn(insTail)));
            for (const auto& pair : edgesMap)
            {
                Edge currEdge = pair.second;
                if (currEdge.destination == targetAddress)
                {
                    currEdge.singleSource = false;
                    edge.singleSource = false;
                }
            }
            edgesMap[tailAddress] = edge;
        }
        INS_InsertCall(insTail, IPOINT_BEFORE, (AFUNPTR)doCountEdge, IARG_BRANCH_TAKEN, IARG_PTR, &(edgesMap[tailAddress]), IARG_END);
    }
}

INT32 Usage()
{
    cerr << "This Pintool counts the number of times a routine is executed" << endl;
    cerr << "and the number of instructions executed in a routine" << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

VOID Fini(INT32 code, VOID* v)
{
    outFile << "Source,Destination,Fallthrough,Routine Address,Taken Count,Not Taken Count\n";
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
    }
    // Register Instruction function to be called to instrument ins
    //INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}