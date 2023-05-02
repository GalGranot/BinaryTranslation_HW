#include "pin.H"
#include <iostream>
#include <list>

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::list;



class MyRoutine
{

public:
	
	//ADDRINT		img_address; 
	RTN			rtn;
	ADDRINT		rtn_address;
	//string		img_name;
	UINT32		inst_count;
	UINT32		rtn_call_count;

	MyRoutine()
	{};

	MyRoutine(INS ins) : rtn(INS_Rtn(ins)), rtn_address(RTN_Address(this->rtn)), rtn_call_count(0) {};
	MyRoutine(RTN rtn) : rtn(rtn), rtn_address(RTN_Address(this->rtn)), rtn_call_count(0) {};

	void print_rtn_info()
	{
		IMG		image		= IMG_FindByAddress(this->rtn_address);
		ADDRINT	img_address = IMG_StartAddress(image);
		string	img_name	= IMG_Name(image);
		string	rtn_name	= RTN_Name(this->rtn);
		UINT32	inst_count	= RTN_NumIns(this->rtn);

		cout << img_name << ", ";
		cout << "0x" << img_address << ", ";
		cout << rtn_name << ", ";
		cout << "0x" << this->rtn_address << ", ";
		cout << inst_count << ", ";
		cout << this->rtn_call_count;
		cout << endl;

		return;
	};

};


MyRoutine* find_routine_in_list(list<MyRoutine*>* rtns, MyRoutine* rtn) //RTN* rtn)
{
	list<MyRoutine*>::iterator it = rtns->begin();
	ADDRINT	rtn_address = rtn->rtn_address; // RTN_Address(rtn->rtn);

	for (; it != rtns->end(); it++)
	{
		if ((*it)->rtn_address == rtn_address)
		{
			return *it;
		}
	}

	return nullptr;
}


INT32 Usage()
{
    cerr << "This tool prints out the number of dynamically executed " << endl
         << "instructions, basic blocks and threads in the application." << endl
         << endl;

    return -1;
}


VOID docount_inst(VOID* v, MyRoutine* rtn) // INS ins)
{
	//MyRoutine* rtn = new MyRoutine(ins);
	list<MyRoutine*>* rtns = (list<MyRoutine*>*) v;
	MyRoutine* tmp = find_routine_in_list(rtns, rtn);

	if (tmp)
	{
		tmp->inst_count++;
		delete rtn;
	}
	else
	{
		rtn->inst_count++;
		((list<MyRoutine*>*)rtns)->push_back(rtn);
	}

	return;
}


VOID docount_rtn(VOID* v, MyRoutine* rtn) // RTN rtn)
{
	//MyRoutine* rtn = new MyRoutine(rtn);
	list<MyRoutine*>* rtns = (list<MyRoutine*>*) v;
	MyRoutine* tmp = find_routine_in_list(rtns, rtn);

	if (tmp)
	{
		tmp->rtn_call_count++;
		delete rtn;
	}
	else
	{
		rtn->rtn_call_count++;
		((list<MyRoutine*>*)rtns)->push_back(rtn);
	}

	return;
}


VOID InstructionFunc(INS ins, VOID* v)
{
	// maybe should cast v to list<MyRoutine>* before
	MyRoutine* rtn = new MyRoutine(ins);
	INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount_inst, IARG_PTR, v, IARG_PTR, rtn, IARG_END);
}


VOID RoutineFunc(RTN rtn, VOID* v)
{
	// maybe should cast v to list<MyRoutine>* before
	MyRoutine* new_rtn = new MyRoutine(rtn);
cout << "bla" << endl;
	RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)docount_rtn, IARG_PTR, v, IARG_PTR, new_rtn, IARG_END);
}


VOID Fini(INT32 code, VOID* v) // must keep the code parameter!!
{
	list<MyRoutine*>* rtns = (list<MyRoutine*>*) v;
	rtns->empty();
	cout << "fini" << endl;
	//cerr << "Count " << ins_count << endl;
	return;
}

int main(int argc, char* argv[])
{
	PIN_InitSymbols();
	if (PIN_Init(argc, argv))
	{
		return Usage();
	}

	list<MyRoutine*> rtns;

	INS_AddInstrumentFunction(InstructionFunc, &rtns);
	RTN_AddInstrumentFunction(RoutineFunc, &rtns);

	PIN_AddFiniFunction(Fini, (&rtns));

	PIN_StartProgram();


	return 0;
}