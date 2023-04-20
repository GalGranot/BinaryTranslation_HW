
#include "pin.H"
#include <iostream>
#include <list>

using std::cout;
using std::cerr;
using std::string;
using std::list;


UINT64 ins_count = 0;

class routine
{
public:
    RTN rtn;
    UINT32 rtnCallCount;
    UINT32 instructionsNum;

    //construct routine from instruction
    routine(RTN* rtn) : rtn(rtn), rtnCallCount(0), instructionsNum(0) {};

    void printRtn()
    {
        ADDRINT address = RTN_Address(this->rtn);
        IMG img = IMG_FindByAddress(address);
        cout << IMG_name(img) << ", ";
        //FIXME: check if cout can print addrint type
        cout << "0x" << IMG_StartAddress(img) << ", ";
        cout << RTN_Name(this->rtn) << ",";
        cout << "0x" << address << ", ";
        cout << RTN_NumIns(this->rtn);
        cout << rtnCallCount << endl;
    }
};

routine* findRtn(list<routine>* routines, routine* target)
{
    list<routine>::iterator it = routines->begin();
    while (it != routines->end())
    {
        if (RTN_Address(target->rtn) == RTN_Address(it->rtn))
            return it;
        else it++;
    }
    return NULL;

}


/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */


/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr <<
        "This tool prints out the number of dynamic instructions executed to stderr.\n"
        "\n";

    cerr << KNOB_BASE::StringKnobSummary();

    cerr << endl;

    return -1;
}

/* ===================================================================== */

VOID docount()
{
    ins_count++;

}

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
/* Main                                                                  */
/* ===================================================================== */

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

//new code

int main(int argc, char* argv[])
{
    list<routine> routines;
    INS_AddInstrumentFunction(InstructionFunc, (VOID*)&routines);
    RTN_AddInstrumentFunction(RoutineFunc, (VOID*)&routines);
    PIN_AddFiniFunction(Fini, 0);

    return 0;

}

/* ===================================================================== */
/* eof */
/* ===================================================================== */