
/*********************************************/
//binary translation, first exercise
/*********************************************/

/*********************************************/
//includes, usings, global vars
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string.h>
#include <vector>
#include "pin.H"

//usings
using std::cerr;
using std::dec;
using std::endl;
using std::hex;
using std::ofstream;
using std::setw;
using std::string;
using std::vector;

//global vars
ofstream outFile;

/*********************************************/


typedef struct routine_counter
{
    string _name;
    string _image;
    ADDRINT _address;
    ADDRINT imgAddress;
    RTN _rtn;
    UINT64 rtnCtr;
    UINT64 _icount;
    struct routine_counter* _next;
} ROUTINE_COUNT;

/*********************************************/
//global functions

//used for sorting descending
bool routineCompare(ROUTINE_COUNT* routine1, ROUTINE_COUNT* routine2)
{
    return routine1->_icount > routine2->_icount;
}

void printRtnInfo(ROUTINE_COUNT* rc)
{
    outFile << rc->_image << ", ";
	outFile << "0x" << hex << rc->imgAddress << ", ";
	outFile << rc->_name << ", ";
	outFile << "0x" << hex << rc->_address << ", ";
	outFile << dec << rc->_icount << ", ";
	outFile << dec << rc->rtnCtr;
	outFile << endl;
    return;
}


ROUTINE_COUNT* RtnList = 0;
vector<ROUTINE_COUNT*> RtnVec;

// This function is called before every instruction is executed
VOID docount(UINT64* counter)
{
    (*counter)++;
}

/*********************************************/


/*********************************************/
//pintool functions

VOID Routine(RTN rtn, VOID* v)
{
    ROUTINE_COUNT* rc = new ROUTINE_COUNT;


    rc->_name     = RTN_Name(rtn);
    rc->_image    = IMG_Name(SEC_Img(RTN_Sec(rtn))).c_str();
    rc->_address  = RTN_Address(rtn);
    IMG	image	= IMG_FindByAddress(rc->_address);
    rc->imgAddress = IMG_StartAddress(image);
    rc->_icount   = 0;
    rc->rtnCtr = 0;

    rc->_next = RtnList;
    RtnList   = rc;

    RtnVec.push_back(rc);

    RTN_Open(rtn);

    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->rtnCtr), IARG_END);

    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->_icount), IARG_END);
    }

    RTN_Close(rtn);
}

VOID Fini(INT32 code, VOID* v)
{
    outFile << setw(23) << "Procedure"
            << " " << setw(15) << "Image"
            << " " << setw(18) << "Image Address"
            << " " << setw(18) << "RTN Address"
            << " " << setw(12) << "Calls"
            << " " << setw(12) << "Instructions" << endl;


    std::sort(RtnVec.begin(), RtnVec.end(), routineCompare);

    for(ROUTINE_COUNT * rc : RtnVec)
    { 
        if (rc->_icount > 0)
        {
            printRtnInfo(rc);
        }
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

    outFile.open("rtn-output.csv");

    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();

    // Register Routine to be called to instrument rtn
    RTN_AddInstrumentFunction(Routine, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
