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
typedef struct RtnCount
{
    string _name;
    ADDRINT _address;
    UINT64 _rtnCount;
    UINT64 _icount;
    //string _image;
    //ADDRINT _img_address;
    //RTN _rtn;
} RTN_COUNT;

/*typedef struct Loop
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
    
} LOOP_COUNT;*/

class LOOP_COUNT 
{

public: 
    ADDRINT loop_address;
    UINT64  count_seen;
    UINT64  CountLoopInvoked;
    UINT64  mean_taken;
    UINT64  diff_count;
    //string  rtn_name;
    ADDRINT rtn_address;
    //UINT64  icount_in_rtn;
    //UINT64  rtn_count;

    LOOP_COUNT() : loop_address(0), count_seen(0), 
         /*icount_in_rtn(0), rtn_count(0),*/ CountLoopInvoked(0), diff_count(0), rtn_address(0)  {}

    LOOP_COUNT(INS ins, ADDRINT loop_address) : loop_address(loop_address), count_seen(0), 
        /*icount_in_rtn(0), rtn_count(0),*/ CountLoopInvoked(0), diff_count(0), rtn_address(0)  
        {
            // RTN rtn = INS_Rtn(ins);
            // this->rtn_name = RTN_Name(rtn);
            // this->rtn_address = RTN_Address(rtn);
        }

};


map<ADDRINT, LOOP_COUNT> map_loop;
map<ADDRINT, RTN_COUNT> map_routine;


bool CompareRTN_COUNT_PTR(RTN_COUNT * rp1, RTN_COUNT * rp2)
{
    return rp1->_icount > rp2->_icount;
}

bool CompareLOOP_COUNT_PTR(LOOP_COUNT lp1, LOOP_COUNT lp2)
{
    return lp1.count_seen > lp2.count_seen;
}


// void print_rtn_info(RTN_COUNT* rc)
// 	{
//         outFile << rc->_image << ", "
// 		     << "0x" << hex << rc->_img_address << ", "
// 		     << rc->_name << ", "
// 		     << "0x" << hex << rc->_address << ", "
// 		     << dec << rc->_icount << ", "
// 		     << dec << rc->_rtnCount
// 		     << endl;

// 		return;
// 	};


void print_loop_info(LOOP_COUNT * lc)
{
    RTN_COUNT rtn = map_routine[lc->rtn_address];
    // cout << "curr rtn name is " << rtn._name << " and rtn_address is " << lc->rtn_address 
    // << " _address is " << rtn._address << endl;
    outFile << "0x" << lc->loop_address << ", "
            << lc->count_seen << ", "
            << lc->CountLoopInvoked << ", "
            << lc->mean_taken << ", "
            << lc->diff_count << ", "
            << rtn._name << ", "
            << lc->rtn_address << ", "
            << rtn._icount << ", "
            << rtn._rtnCount
            << endl;

    return;
}


// This function is called before every instruction is executed
VOID docount(UINT64* counter) { (*counter)++; }
VOID docount_branch_if_taken(INT32 taken, UINT64* counter){ if (taken) (*counter)++; }


VOID process_Routine(ADDRINT rtn_address)
{
    RTN_COUNT rc;
    UINT64      counter = 0;
    RTN_COUNT* rc_p;
    RTN rtn  = RTN_FindByAddress(rtn_address);

    // The RTN goes away when the image is unloaded, so save it now
    // because we need it in the fini
    rc._name     = RTN_Name(rtn);
    rc._address  = rtn_address;
    rc._rtnCount   = 1;

    RTN_Open(rtn);

    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
    {
        // Insert a call to docount to increment the instruction counter for this rtn
        // INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->_icount), IARG_END);
        counter++;
    }

    rc._icount = counter;

    map_routine[rc._address] = rc;

    RTN_Close(rtn);

}


VOID Instruction(INS ins, VOID* V)
{
    if (/*!INS_IsRet(ins) && !INS_IsSyscall(ins) &&*/ INS_IsDirectControlFlow(ins) && !INS_IsCall(ins))
        {
            if (INS_DirectControlFlowTargetAddress(ins) < INS_Address(ins))
            {
                LOOP_COUNT* loop;
                ADDRINT loop_target_addr = INS_DirectControlFlowTargetAddress(ins);
                auto it = map_loop.find(loop_target_addr); 
                auto& lc = it;
                if (lc == map_loop.end()) // for a new loop
                {
                    loop = new LOOP_COUNT(ins, loop_target_addr);
                    map_loop[loop_target_addr] = *loop;
                    delete loop;
                    loop = &(map_loop[loop_target_addr]);
                    loop->rtn_address = RTN_Address(INS_Rtn(ins));
                    if (!(map_routine.count(loop->rtn_address)))
                    {
                        process_Routine(loop->rtn_address);
                    }
                    else
                    {
                        //map_routine[loop->rtn_address]._rtnCount;
                    }
                    // lc = map_loop[loop_target_addr].second;
                }
                else
                {
                    loop = &(lc->second);
                }
                INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_branch_if_taken, IARG_BRANCH_TAKEN, IARG_PTR, &(loop->count_seen), IARG_END);   
            }   
            // printf("found !INS_IsRet && !INS_IsSyscall && INS_IsDirectControlFlow\n");
        }
}





// Pin calls this function every time a new rtn is executed
VOID Routine(RTN rtn, VOID* v)
{
    // Allocate a counter for this routine
    // RTN_COUNT* rc = new RTN_COUNT;
    RTN_COUNT rc;
    // LOOP_COUNT* lc = new LOOP_COUNT;


    // The RTN goes away when the image is unloaded, so save it now
    // because we need it in the fini
    // lc->loop_address = IMG_StartAddress(image); 
    rc._name     = RTN_Name(rtn);
    rc._address  = RTN_Address(rtn);
    rc._rtnCount   = 0;
    rc._icount = 0;

    map_routine[rc._address] = rc;

    RTN_Open(rtn);

    // Insert a call at the entry point of a routine to increment the call count
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(map_routine[rc._address]._rtnCount), IARG_END);
    

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
    //     // INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->_icount), IARG_END);
    // }

    RTN_Close(rtn);
}



// This function is called when the application exits
// It prints the name and count for each procedure
VOID Fini(INT32 code, VOID* v)
{

    vector<LOOP_COUNT> vec;
    for(auto it : map_loop)
    {
        vec.push_back((it.second));
    }
    std::sort(vec.begin(), vec.end(), CompareLOOP_COUNT_PTR);
    // std::sort(LoopVec.begin(), LoopVec.end(), CompareLOOP_COUNT_PTR);
    //RtnVec.sort(CompareRTN_COUNT_PTR);

    for(auto lc : vec)
    {
        
        if (lc.count_seen > 0)
        {
            print_loop_info(&lc);
        }
        // delete lc;
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
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Routine function to be called to instrument rtn
    RTN_AddInstrumentFunction(Routine, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
