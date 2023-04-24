/* ===================================================================== */
/* includes, namespaces, global vars */
/* ===================================================================== */
#include "pin.H"
#include <iostream>
#include <fstream>
#include <list>
#include <algorithm>

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::list;

/* ===================================================================== */
/* stuff from old file, figure out if it's relevant */
/* ===================================================================== */

/*
INT32 Usage()
{
    cerr <<
        "This tool prints out the number of dynamic instructions executed to stderr.\n"
        "\n";

    cerr << KNOB_BASE::StringKnobSummary();

    cerr << endl;

    return -1;


VOID docount()
{
    ins_count++;

}

int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }


    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
*/

/* ===================================================================== */
/* classes */
/* ===================================================================== */

class routine
{
public:
    RTN rtn;
    ADDRINT address;
    UINT32 rtnCallCount;
    UINT32 instructionsNum;

    //construct routine from instruction
    routine(RTN* rtn) : rtn(rtn), rtnCallCount(0), instructionsNum(0),
        address(RTN_Address(rtn)) {};

    void printRtn()
    {
        IMG img = IMG_FindByAddress(address);
        cout << IMG_name(img) << ", ";
        //FIXME: check if cout can print addrint type
        cout << "0x" << IMG_StartAddress(img) << ",";
        cout << RTN_Name(this->rtn) << ",";
        cout << "0x" << address << ",";
        cout << RTN_NumIns(this->rtn);
        cout << rtnCallCount << endl;
    }
};

/* ===================================================================== */
/* routine functions */
/* ===================================================================== */

routine* findRtn(list<routine>* routines, routine* target)
{
    ADDRINT targetAddress = target->address;
    list<routine>::iterator it = routines->begin();
    while (!it)
    {
        ADDRINT currentAddress = it->address;
        if (currentAddress == targetAddress) //targert routine exists in list
            return it;
        else it++
    }
    return NULL;
}

//prints routines to file and destroys list
void printToFile(list<routine> routines)
{
    std::ofstream outfile("rtn-output.csv");
    if (!outfile.open())
    {
        cout << "error opening file";
        return;
    }
    std::list<routine>::iterator it = routines.begin();
    while (!it)
    {
        outfile << it->printRtn() << endl;
        delete tmp;
        it = routines.erase(it);
    }
    routines.clear();
}

/* ===================================================================== */
/* pintool instructions */
/* ===================================================================== */

VOID instructionProcess(INS ins, list<routine> routines)
{
    routine* currentRtn = new routine(INS_Rtn(ins));
    routine* tmp = findRtn(currentRtn);
    if (!tmp) //rtn is new, add it to list
    {
        currentRtn->instructionsNum++;
        routines->push_back(currentRtn);
    }
    else //routine exists in routine list, only update its instruction count
    {
        tmp->instructionsNum++;
        delete currentRtn;
    }
}

VOID rtnProcess(RTN rtn, list<routine> routines)
{
    routine* currentRtn = new routine(rtn);
    tmp = findRtn(routines, currentRtn);
    if (!tmp) //rtn is new and should be added to list
    {
        currentRtn->rtnCallCount++;
        routines->push_back(currenRtn);
    }
    else //rtn isn't new and shouldn't be added, only increment its call count
    {
        tmp->rtnCallCount++;
        delete currentRtn;
    }
}

/* ===================================================================== */

VOID InstructionFunc(INS ins, VOID* routines)
{
    //INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_END);
    //	cerr << RTN_FindNameByAddress(INS_Address(ins));

    list<routine>* rtns = (list<routine>*) routines;
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)instructionProcess,
        INS, ins, list<routine>, rtns, IARG_END);
}

VOID RoutineFunc(RTN rtn, VOID* routines)
{
    list<routine>* rtns = (list<routine>*) routines;
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)rtnProcess,
        RTN, rtn, list<routine>, routines, IARG_END);
}

/* ===================================================================== */

VOID Fini(INT32 code, VOID* v)
{
    cerr << "Count " << ins_count << endl;
}

/* ===================================================================== */
/* main                                                                  */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    list<routine> routines;
    INS_AddInstrumentFunction(InstructionFunc, (VOID*)&routines);
    RTN_AddInstrumentFunction(RoutineFunc, (VOID*)&routines);
    PIN_AddFiniFunction(Fini, 0);

    printToFile(routines);
    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */