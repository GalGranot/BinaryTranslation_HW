/*
ex2.cpp
 */

 //
 // This tool counts the number of times a routine is executed and
 // the number of instructions executed in a routine, and outputs 
 // the routine's and instruction's name and address.
 //

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string.h>
#include <vector>
#include <map>
#include "pin.H"

using std::cerr;
using std::dec;
using std::hex;
using std::endl;
using std::cout;
using std::ofstream;
using std::string;
using std::map;
using std::vector;

ofstream outFile;

// Holds instruction count for a single procedure
class RTN_COUNT
{
public:
    ADDRINT rtnAddress;
    string rtnName;
    UINT64 rtnCallCount;
    UINT64 rtnInsCount;

    RTN_COUNT()
    {
        this->rtnName = "";
        this->rtnAddress = 0;
        rtnCallCount = 0;
        rtnInsCount = 0;
    }

    RTN_COUNT(ADDRINT rtnAddress)
    {
        this->rtnName = RTN_FindNameByAddress(rtnAddress);
        this->rtnAddress = rtnAddress;
        rtnCallCount = 0;
        rtnInsCount = 0;
    }

};


class LOOP_COUNT
{
public:
    ADDRINT loopAddress;
    UINT64  countSeen;
    UINT64  countLoopInvoked;
    UINT64  prevIterInsCount;
    UINT64  currIterInsCount;
    UINT64  diffCount;
    ADDRINT rtnLoopAddress;
    


    LOOP_COUNT() : loopAddress(0), countSeen(0), countLoopInvoked(0), 
    prevIterInsCount(0), currIterInsCount(0), diffCount(0), rtnLoopAddress(0) {}
    
    LOOP_COUNT(INS ins, ADDRINT loopTargetAddress, ADDRINT rtnLoopAdress)
    {
        this->loopAddress = loopTargetAddress;
        this->countSeen = 0;
        this->countLoopInvoked = 0;
        this->prevIterInsCount = 0;
        this->currIterInsCount = 0;
        this->diffCount = 0;
        this->rtnLoopAddress = rtnLoopAdress;
    }

};


map<ADDRINT, LOOP_COUNT> loopMap;
map<ADDRINT, RTN_COUNT> rtnMap;


bool CompareRTN_COUNT_PTR(RTN_COUNT* rp1, RTN_COUNT* rp2)
{
    return rp1->rtnInsCount > rp2->rtnInsCount;
}

bool CompareLOOP_COUNT_PTR(LOOP_COUNT lp1, LOOP_COUNT lp2)
{
    return lp1.countSeen > lp2.countSeen;
}


void printLoopInfo(LOOP_COUNT* lc)
{
    // cout << "printing to file" << endl;
    RTN_COUNT rtn = rtnMap[lc->rtnLoopAddress];
    double meanTaken = lc->countLoopInvoked ? lc->countSeen / lc->countLoopInvoked : 0;
    outFile << "0x" << hex << lc->loopAddress << ", "
        << dec << lc->countSeen << ", "
        << /*dec <<*/ lc->countLoopInvoked << ", "
        << meanTaken << ", "
        << /*dec <<*/ lc->diffCount << ", "
        << rtn.rtnName << ", "
        << "0x" << hex << lc->rtnLoopAddress << ", "
        << dec << rtn.rtnInsCount << ", "
        // << dec << (rtn.rtnCallCount ? rtn.rtnCallCount : 1)
        << rtn.rtnCallCount
        << endl;
    return;
}


VOID docount_by(UINT64* counter, UINT64 count) { (*counter) += count; }
VOID docount(UINT64* counter) { (*counter)++; }
VOID docount_rtn(UINT64 head, RTN_COUNT* rtn) 
{ 
    if (head)
    {
        cout << "before rtn->rtnCallCount = " << rtn->rtnCallCount<< endl;
        (rtn->rtnCallCount)++; 
        cout << "after rtn->rtnCallCount = " << rtn->rtnCallCount<< endl;
    }
    (rtn->rtnInsCount)++; 
}
VOID docount_branch_if_taken(INT32 taken, LOOP_COUNT* loop) 
{ 
    loop->countSeen++;

    if (!taken) 
    {         
        if ((loop->prevIterInsCount != 0) && (loop->currIterInsCount != loop->prevIterInsCount))
        {
            loop->diffCount++;
        }
        loop->prevIterInsCount = loop->currIterInsCount;
        loop->currIterInsCount = 0;
        // loop->countSeen--;
        loop->countLoopInvoked++;
    }
    else
    {
        loop->currIterInsCount++;
    }
}

VOID docount_invoked(INT32 taken, UINT64* counter)
{
    if(taken)
    {
        (*counter)++; 
    }
}


VOID Trace(TRACE trace, VOID* v)
{
    RTN rtn = TRACE_Rtn(trace);
    if(!RTN_Valid(rtn))
        return;

    INS ins;
    BBL bbl;
    ADDRINT rtnAddress = RTN_Address(rtn);
    for( bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        ins = BBL_InsTail(bbl);
        ADDRINT insAddress = INS_Address(ins);
        // insert call for rtncount here?
        if(!IMG_IsMainExecutable(IMG_FindByAddress(insAddress)))
            continue;

        if (BBL_Original(bbl))
        {
            if (rtnMap.count(rtnAddress) == 0) // new routine, init it
            {
                //cout << "curr_rtn is : " << RTN_Name(rtn) << " inside if" << endl;
                RTN_COUNT rtnCount = RTN_COUNT(rtnAddress);
                // int counter = 0;
                rtnMap[rtnAddress] = rtnCount;
                    
                RTN_Open(rtn);
                // INS curr_ins = RTN_InsHead(rtn);
                // INS_InsertCall(curr_ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rtnMap[rtnAddress].rtnCallCount), IARG_END);

                for (INS curr_ins = RTN_InsHead(rtn); INS_Valid(curr_ins); curr_ins = INS_Next(curr_ins))
                {
                    INS_InsertCall(curr_ins, IPOINT_BEFORE, (AFUNPTR)docount_rtn, IARG_UINT64, (INS_Address(curr_ins) == RTN_Address(rtn)), IARG_PTR, &(rtnMap[rtnAddress]), IARG_END);
                    // counter++;
                    if (INS_IsDirectControlFlow(curr_ins) && INS_IsBranch(curr_ins))
                    {
                        ADDRINT loopTargetAddress = INS_DirectControlFlowTargetAddress(curr_ins);
                        if (loopTargetAddress > insAddress) //
                        {
                            if(loopMap.count(loopTargetAddress) != 0)
                            {
                                INS_InsertCall(curr_ins, IPOINT_BEFORE, (AFUNPTR)docount_invoked, IARG_BRANCH_TAKEN, IARG_PTR, &(loopMap[loopTargetAddress].countLoopInvoked), IARG_END);
                            }
                        }
                    }
                }
                RTN_Close(rtn);
                // rtnCount.rtnInsCount = counter;
            }
            // BBL_InsertCall(bbl, IPOINT_ANYWHERE, (AFUNPTR)docount, IARG_PTR, &(rtnMap[rtnAddress].rtnCallCount), IARG_END);
        }
        
        if (INS_IsDirectControlFlow(ins) && INS_IsBranch(ins))
        {
            ADDRINT loopTargetAddress = INS_DirectControlFlowTargetAddress(ins);
            if (loopTargetAddress < insAddress)
            {
                // RTN curr_rtn = INS_Rtn(ins);
                // if (!RTN_Valid(curr_rtn) /*|| !IMG_IsMainExecutable(IMG_FindByAddress(loopTargetAddress))*/ )
                // {
                //     continue;
                // }
                // cout << "curr_rtn is : " << RTN_Name(curr_rtn) << " at beginning" << endl;
                // RTN_Open(curr_rtn);
                // ADDRINT rtnAddress = RTN_Address(curr_rtn);
                // LOOP_COUNT loop(ins, loopTargetAddress, rtnLoopAddress);
                if (loopMap.count(loopTargetAddress) == 0) // new loop, init it
                {
                    loopMap[loopTargetAddress] = LOOP_COUNT(ins, loopTargetAddress, rtnAddress);
                    // LOOP_COUNT loop(ins, loopTargetAddress, rtnAddress);
                    // loopMap[loopTargetAddress] = loop;
                }

                //check if routine is new
                
                // ADDRINT rtnAddress = RTN_Address(curr_rtn);
                else //routine isn't new
                {
                    // if (ins == RTN_InsHead(curr_rtn))
                    // {
                    //     // (rtnMap[rtnAddress].rtnCallCount)++;
                    //     (rtnMap.at(rtnAddress).rtnCallCount)++;
                    // }
                }

                // for both new and old loops
                INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_branch_if_taken, IARG_BRANCH_TAKEN, IARG_PTR, &(loopMap[loopTargetAddress]), IARG_END);
                // RTN_Close(curr_rtn);
            }
            // loopMap[loopTargetAddress].countSeen++;
        }
    }
}


VOID Instruction(INS ins, VOID* V)
{
    RTN rtn = INS_Rtn(ins);
    ADDRINT insAddress = INS_Address(ins);
    LOOP_COUNT lc;
    RTN_COUNT rtnCount;
    
    if (!RTN_Valid(rtn))
    {
        return;
    }
    if (!IMG_IsMainExecutable(IMG_FindByAddress(insAddress)))
    {
        return;
    }

    ADDRINT rtnAddress = RTN_Address(rtn);

    if (rtnMap.count(rtnAddress) == 0) // new routine, init it
    {
        // cout << "curr_rtn is : " << RTN_Name(curr_rtn) << " inside if" << endl;
        rtnCount = RTN_COUNT(rtnAddress);
        rtnMap[rtnAddress] = rtnCount;
        // rtnMap[rtnAddress] = RTN_COUNT(rtnAddress);
        
    }
            // else //routine isn't new
            // {
            //     if (ins == RTN_InsHead(curr_rtn))
            //     {
            //         // (rtnMap[rtnAddress].rtnCallCount)++;
            //         (rtnMap.at(rtnAddress).rtnCallCount)++;
            //     }
            // }
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rtnMap[rtnAddress].rtnInsCount), IARG_END);

    if (rtnAddress == insAddress) // ++ relevant rtn's rtnCallCount for every ins corresponds to the first address of the routine
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rtnMap[rtnAddress].rtnCallCount), IARG_END);
    }
    //handle only if direct control flow and a branch
    if (INS_IsDirectControlFlow(ins) && INS_IsBranch(ins))
    {
        
        ADDRINT loopTargetAddress = INS_DirectControlFlowTargetAddress(ins);
        if (loopMap.count(loopTargetAddress) == 0) //new loop, init it
        {
            // ADDRINT rtnLoopAddress = RTN_Address(INS_Rtn(ins));
            // RTN curr_rtn = INS_Rtn(ins);
            // if (curr_rtn == RTN_Invalid() || !IMG_IsMainExecutable(IMG_FindByAddress(loopTargetAddress)) )
            // {
            //     return;
            // }
            // cout << "curr_rtn is : " << RTN_Name(curr_rtn) << " at beginning" << endl;
            // RTN_Open(rtn);
            // ADDRINT rtnAddress = RTN_Address(curr_rtn);
            // LOOP_COUNT loop(ins, loopTargetAddress, rtnLoopAddress);
            lc = LOOP_COUNT(ins, loopTargetAddress, rtnAddress);
            loopMap[loopTargetAddress] = lc;
            // loopMap[loopTargetAddress] = LOOP_COUNT(ins, loopTargetAddress, rtnAddress);

            //check if routine is new
            
            // ADDRINT rtnAddress = RTN_Address(curr_rtn);
        }
        else
        {
            if (loopTargetAddress > insAddress) //
            {
                INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_invoked, IARG_BRANCH_TAKEN, IARG_PTR, &(loopMap[loopTargetAddress].countLoopInvoked), IARG_END);
            }
        }

        // if jump back
        if (loopTargetAddress < insAddress)
        {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_branch_if_taken, IARG_BRANCH_TAKEN, IARG_PTR, &(loopMap[loopTargetAddress]), IARG_END);
        }

        // loopMap[loopTargetAddress].countSeen++;
        // ++ relevant rtn's rtnInsCount for every ins we see

        
    }

    //for both new and old loops


    if (loopMap.count(INS_NextAddress(ins)) != 0)
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(loopMap[INS_NextAddress(ins)].countLoopInvoked), IARG_END);
    }
}





// Pin calls this function every time a new rtn is executed

VOID Routine(RTN rtn, VOID* v)
{
    /*
    // Allocate a counter for this routine
    // RTN_COUNT* rc = new RTN_COUNT;
    RTN_COUNT rc;
    // LOOP_COUNT* lc = new LOOP_COUNT;


    // The RTN goes away when the image is unloaded, so save it now
    // because we need it in the fini
    // lc->loopAddress = IMG_StartAddress(image); 
    rc.rtnName = RTN_Name(rtn);
    rc.rtnAddress = RTN_Address(rtn);
    rc.rtnCallCount = 0;
    rc.rtnInsCount = 0;

    rtnMap[rc.rtnAddress] = rc;

    RTN_Open(rtn);

    // Insert a call at the entry point of a routine to increment the call count
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)docount_by, IARG_PTR, &(rtnMap[rc.rtnAddress].rtnCallCount), IARG_UINT64, 1, IARG_END);


    // For each instruction of the routine
    // for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
    // {
    //     // we dont want to count returns!
    //     if (!INS_IsRet(ins) && !INS_IsSyscall(ins) && INS_IsDirectControlFlow(ins) && !INS_IsCall(ins))
    //     {
    //         //INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inc_branch, IARG_BRANCH_TAKEN, IARG_PTR, &(lc->rtn_count), IARG_END);
    //         // printf("found !INS_IsRet && !INS_IsSyscall && INS_IsDirectControlFlow\n");
    //     }
    //     // // we dont want to count syscalls!
    //     // else if (INS_IsSyscall(ins))
    //     // {
    //     //     INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inc_syscall, IARG_BRANCH_TAKEN, IARG_END);
    //     // }
    //     // // we WANT to count DirectControlFlow!
    //     // else if (INS_IsDirectControlFlow(ins))
    //     // {
    //     //     if (INS_IsCall(ins))
    //     //         INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inc_call, IARG_BRANCH_TAKEN, IARG_END);
    //     //     else
    //     //         INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inc_branch, IARG_BRANCH_TAKEN, IARG_END);
    //     // }
    //     // // Insert a call to docount to increment the instruction counter for this rtn
    //     // INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->rtnInsCount), IARG_END);
    // }

    RTN_Close(rtn);
    */

   
}



// This function is called when the application exits
// It prints the name and count for each procedure
VOID Fini(INT32 code, VOID* v)
{

    vector<LOOP_COUNT> vec;
    for (auto it : loopMap)
    {
        // cout << "pushing " << rtnMap[it.second.rtnLoopAddress].rtnName << endl;
        vec.push_back((it.second));
    }
    std::sort(vec.begin(), vec.end(), CompareLOOP_COUNT_PTR);
    // std::sort(LoopVec.begin(), LoopVec.end(), CompareLOOP_COUNT_PTR);
    //RtnVec.sort(CompareRTN_COUNT_PTR);
    // cout << "started fini before for" << endl;
    for (auto lc : vec)
    {
        if (lc.countSeen == 0 || lc.countLoopInvoked == 0)
        {
            continue;
        }
        printLoopInfo(&lc);
        // delete lc;
    }
    // cout << "in fini after for" << endl;

    outFile.close();
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "This Pintool counts the number of times a routine is executed" << endl;
    cerr << "and the number of instructions executed in a routine" << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    // Initialize symbol table code, needed for rtn instrumentation
    PIN_InitSymbols();

    outFile.open("loop-count.csv");

    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();

    // Register Instruction function to be called to instrument ins
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Instruction function to be called to instrument ins
    // TRACE_AddInstrumentFunction(Trace, 0);

    // Register Routine function to be called to instrument rtn
    //RTN_AddInstrumentFunction(Routine, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}