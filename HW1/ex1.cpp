/*
 * Copyright (C) 2004-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//
// This tool counts the number of times a routine is executed and
// the number of instructions executed in a routine
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
using std::setw;
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

bool CompareRTN_COUNT_PTR(RTN_COUNT * rp1, RTN_COUNT * rp2)
{
    return rp1->_icount > rp2->_icount;
}

void print_rtn_info(RTN_COUNT* rc)
	{
		// IMG		image		= IMG_FindByAddress(this->rtn_address);
		// ADDRINT	img_address = IMG_StartAddress(rc->_image);
		// string	img_name	= IMG_Name(image);
		// string	rtn_name	= RTN_Name(this->rtn);
		// UINT32	inst_count	= RTN_NumIns(this->rtn);

		//outFile << rc->_image << ", ";
        outFile << rc->_image << ", ";
		outFile << "0x" << hex << rc->_img_address << ", ";
		outFile << rc->_name << ", ";
		outFile << "0x" << hex << rc->_address << ", ";
		outFile << dec << rc->_icount << ", ";
		outFile << dec << rc->_rtnCount;
		outFile << endl;

		return;
	};


// Linked list of instruction counts for each routine 
RTN_COUNT* RtnList = 0;
vector<RTN_COUNT*> RtnVec;

// This function is called before every instruction is executed
VOID docount(UINT64* counter) { (*counter)++; }

const char* StripPath(const char* path)
{
    const char* file = strrchr(path, '/');
    if (file)
        return file + 1;
    else
        return path;
}

// Pin calls this function every time a new rtn is executed
VOID Routine(RTN rtn, VOID* v)
{
    // Allocate a counter for this routine
    RTN_COUNT* rc = new RTN_COUNT;

    // The RTN goes away when the image is unloaded, so save it now
    // because we need it in the fini
    rc->_name     = RTN_Name(rtn);
    //rc->_image    = StripPath(IMG_Name(SEC_Img(RTN_Sec(rtn))).c_str());
    rc->_image    = IMG_Name(SEC_Img(RTN_Sec(rtn))).c_str();
    rc->_address  = RTN_Address(rtn);
    IMG		image		= IMG_FindByAddress(rc->_address);
    rc->_img_address = IMG_StartAddress(image);
    rc->_icount   = 0;
    rc->_rtnCount = 0;

    // Add to list of routines
    rc->_next = RtnList;
    RtnList   = rc;

    RtnVec.push_back(rc);

    RTN_Open(rtn);

    // Insert a call at the entry point of a routine to increment the call count
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->_rtnCount), IARG_END);

    // For each instruction of the routine
    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
    {
        // Insert a call to docount to increment the instruction counter for this rtn
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->_icount), IARG_END);
    }

    RTN_Close(rtn);
}

// This function is called when the application exits
// It prints the name and count for each procedure
VOID Fini(INT32 code, VOID* v)
{
    outFile << setw(23) << "Procedure"
            << " " << setw(15) << "Image"
            << " " << setw(18) << "Image Address"
            << " " << setw(18) << "RTN Address"
            << " " << setw(12) << "Calls"
            << " " << setw(12) << "Instructions" << endl;


    std::sort(RtnVec.begin(), RtnVec.end(), CompareRTN_COUNT_PTR);

    for(RTN_COUNT * rc : RtnVec)
    {
        
        if (rc->_icount > 0)
        {
            print_rtn_info(rc);
        }
        //     outFile << setw(23) << rc->_name << " " << setw(15) << rc->_image << " " << setw(18) << hex << rc->_address << dec
        //             << " " << setw(12) << rc->_rtnCount << " " << setw(12) << rc->_icount << endl;
    }

    // for (RTN_COUNT* rc = RtnList; rc; rc = rc->_next)
    // {
        
    //     if (rc->_icount > 0)
    //     {
    //         print_rtn_info(rc);
    //     }
    //     //     outFile << setw(23) << rc->_name << " " << setw(15) << rc->_image << " " << setw(18) << hex << rc->_address << dec
    //     //             << " " << setw(12) << rc->_rtnCount << " " << setw(12) << rc->_icount << endl;
    // }
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

    outFile.open("proccount.out");

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
