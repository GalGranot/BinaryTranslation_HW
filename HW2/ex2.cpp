/*
ex1.cpp
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
#include "pin.H"

using std::cerr;
using std::dec;
using std::endl;
using std::hex;
using std::ofstream;
using std::string;
using std::vector;

ofstream outFile;

// Holds instruction count for a single procedure
typedef struct RtnCount
{
    string _name;
    string _image;
    ADDRINT _address;
    ADDRINT _img_address;
    RTN _rtn;
    UINT64 _rtnCount;
    UINT64 _icount;
    struct RtnCount* _next;
} RTN_COUNT;

typedef struct Loop
{
    ADDRINT loop_address;
    ADDRINT rtn_address;
    string  rtn_name;
    UINT64  count_seen;
    UINT64  icount_in_rtn;
    UINT64  rtn_count;
    UINT64  CountLoopInvoked;
    UINT64  diff_count;

    // struct Loop* next;
    
} LOOP_COUNT;


bool CompareRTN_COUNT_PTR(RTN_COUNT * rp1, RTN_COUNT * rp2)
{
    return rp1->_icount > rp2->_icount;
}

bool CompareLOOP_COUNT_PTR(LOOP_COUNT * lp1, LOOP_COUNT * lp2)
{
    return lp1->count_seen > lp2->count_seen;
}


void print_rtn_info(RTN_COUNT* rc)
	{
        outFile << rc->_image << ", "
		     << "0x" << hex << rc->_img_address << ", "
		     << rc->_name << ", "
		     << "0x" << hex << rc->_address << ", "
		     << dec << rc->_icount << ", "
		     << dec << rc->_rtnCount
		     << endl;

		return;
	};


void print_loop_info(LOOP_COUNT * lc)
{
    UINT64 mean_taken = 0;
    outFile << "0x" << lc->loop_address << ", "
            << lc->count_seen << ", "
            << lc->CountLoopInvoked << ", "
            << mean_taken << ", "
            << lc->diff_count << ", "
            << lc->rtn_name << ", "
            << lc->rtn_address << ", "
            << lc->icount_in_rtn << ", "
            << lc->rtn_count << ", "
            << endl;

    return;
}
// bla
// Linked list of instruction counts for each routine 
// RTN_COUNT* RtnList = 0;
vector<RTN_COUNT*> RtnVec;
// LOOP_COUNT* LoopList = 0; 
vector<LOOP_COUNT*> LoopVec;
//list<RTN_COUNT*> RtnVec;

// This function is called before every instruction is executed
VOID docount(UINT64* counter) { (*counter)++; }
VOID docount_inc_branch(INT32 taken, UINT64* counter){ if (taken) (*counter)++; }



/*VOID Instruction(INS ins, void* v)
{
    // we dont want to count returns!
    if (INS_IsRet(ins))
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inc_return, IARG_BRANCH_TAKEN, IARG_END);
    }
    // we dont want to count syscalls!
    else if (INS_IsSyscall(ins))
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inc_syscall, IARG_BRANCH_TAKEN, IARG_END);
    }
    // we WANT to count DirectControlFlow!
    else if (INS_IsDirectControlFlow(ins))
    {
        if (INS_IsCall(ins))
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inc_call, IARG_BRANCH_TAKEN, IARG_END);
        else
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inc_branch, IARG_BRANCH_TAKEN, IARG_END);
    }
    // we dont want to count IndirectControlFlow!
    else if (INS_IsIndirectControlFlow(ins))
    {
        if (INS_IsCall(ins))
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inc_call_indirect, IARG_BRANCH_TAKEN, IARG_END);
        else
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inc_branch_indirect, IARG_BRANCH_TAKEN, IARG_END);
    }
}*/


// Pin calls this function every time a new rtn is executed
VOID Routine(RTN rtn, VOID* v)
{
    // Allocate a counter for this routine
    // RTN_COUNT* rc = new RTN_COUNT;
    LOOP_COUNT* lc = new LOOP_COUNT;

    // The RTN goes away when the image is unloaded, so save it now
    // because we need it in the fini
    // lc->loop_address = IMG_StartAddress(image); 
    lc->rtn_name     = RTN_Name(rtn);
    // lc->_image    = IMG_Name(SEC_Img(RTN_Sec(rtn))).c_str();
    lc->rtn_address  = RTN_Address(rtn);
    // IMG		image		= IMG_FindByAddress(rc->_address);
    lc->count_seen   = 0;
    lc->icount_in_rtn   = 0;
    lc->rtn_count = 0;
    lc->CountLoopInvoked = 0;
    lc->diff_count = 0;


    LoopVec.push_back(lc);

    RTN_Open(rtn);

    // Insert a call at the entry point of a routine to increment the call count
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(lc->rtn_count), IARG_END);

    // For each instruction of the routine
    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
    {
        // we dont want to count returns!
        if (!INS_IsRet(ins) && !INS_IsSyscall(ins) && INS_IsDirectControlFlow(ins) && !INS_IsCall(ins))
        {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inc_branch, IARG_BRANCH_TAKEN, IARG_PTR, &(lc->rtn_count), IARG_END);
            // printf("found !INS_IsRet && !INS_IsSyscall && INS_IsDirectControlFlow\n");
        }
        // // we dont want to count syscalls!
        // else if (INS_IsSyscall(ins))
        // {
        //     INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inc_syscall, IARG_BRANCH_TAKEN, IARG_END);
        // }
        // // we WANT to count DirectControlFlow!
        // else if (INS_IsDirectControlFlow(ins))
        // {
        //     if (INS_IsCall(ins))
        //         INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inc_call, IARG_BRANCH_TAKEN, IARG_END);
        //     else
        //         INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inc_branch, IARG_BRANCH_TAKEN, IARG_END);
        // }
        // // Insert a call to docount to increment the instruction counter for this rtn
        // INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->_icount), IARG_END);
    }

    RTN_Close(rtn);
}

// This function is called when the application exits
// It prints the name and count for each procedure
VOID Fini(INT32 code, VOID* v)
{
    
    std::sort(LoopVec.begin(), LoopVec.end(), CompareLOOP_COUNT_PTR);
    //RtnVec.sort(CompareRTN_COUNT_PTR);

    for(LOOP_COUNT * lc : LoopVec)
    {
        
        if (lc->count_seen > 0)
        {
            print_loop_info(lc);
        }
        delete lc;
    }

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
    //INS_AddInstrumentFunction(Instruction, 0);

    // Register Routine function to be called to instrument rtn
    RTN_AddInstrumentFunction(Routine, 0);

    // Register Fini to be called when the application exits
    // PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
