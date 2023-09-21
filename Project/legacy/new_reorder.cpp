/*########################################################################################################*/
// cd /nfs/iil/ptl/bt/ghaber1/pin/pin-2.10-45467-gcc.3.4.6-ia32_intel64-linux/source/tools/SimpleExamples
// make
//  ../../../pin -t obj-intel64/rtn-translation.so -- ~/workdir/tst
/*########################################################################################################*/
/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2011 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
/* ===================================================================== */

/* ===================================================================== */
/*! @file
 * This probe pintool generates translated code of routines, places them in an allocated TC 
 * and patches the orginal code to jump to the translated routines.
 */

#include "pin.H"
extern "C" {
#include "xed-interface.h"
}
#include <iostream>
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
KNOB<BOOL> KnobOpt(KNOB_MODE_WRITEONCE, "pintool", "opt", "0", "Enable optimization by reordering");

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */
std::ofstream* out = 0;

// For XED:
#if defined(TARGET_IA32E)
    xed_state_t dstate = {XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b};
#else
    xed_state_t dstate = { XED_MACHINE_MODE_LEGACY_32, XED_ADDRESS_WIDTH_32b};
#endif

//For XED: Pass in the proper length: 15 is the max. But if you do not want to
//cross pages, you can pass less than 15 bytes, of course, the
//instruction might not decode if not enough bytes are provided.
const unsigned int max_inst_len = XED_MAX_INSTRUCTION_BYTES;

ADDRINT lowest_sec_addr = 0;
ADDRINT highest_sec_addr = 0;

#define MAX_PROBE_JUMP_INSTR_BYTES  14

// tc containing the new code:
char *tc;    
int tc_cursor = 0;

// instruction map with an entry for each new instruction:
typedef struct { 
    ADDRINT orig_ins_addr;
    ADDRINT new_ins_addr;
    ADDRINT orig_targ_addr;
    bool hasNewTargAddr;
    char encoded_ins[XED_MAX_INSTRUCTION_BYTES];
    xed_category_enum_t category_enum;
    unsigned int size;
    int targ_map_entry;
} instr_map_t;


instr_map_t *instr_map = NULL;
int num_of_instr_map_entries = 0;
int max_ins_count = 0;


// total number of routines in the main executable module:
int max_rtn_count = 0;

// Tables of all candidate routines to be translated:
typedef struct { 
    ADDRINT rtn_addr; 
    USIZE rtn_size;
    int instr_map_entry;   // negative instr_map_entry means routine does not have a translation.
    bool isSafeForReplacedProbe;    
} translated_rtn_t;


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
    takenCount(0), notTakenCount(0), singleSource(true), sharingTarget(false) {}

    Edge(ADDRINT srcBBLHead, ADDRINT source, ADDRINT destination, ADDRINT fallThrough, ADDRINT rtnAddress, 
        UINT64 takenCount, UINT64 notTakenCount, bool singleSource, bool sharingTarget) : 
    srcBBLHead (srcBBLHead), source(source), destination(destination), fallThrough(fallThrough), rtnAddress(rtnAddress), 
    takenCount(takenCount), notTakenCount(notTakenCount), singleSource(singleSource), sharingTarget(sharingTarget) {}

    Edge (vector<string> line)
    {
        this->srcBBLHead      = static_cast<ADDRINT>(stoul(line[0], NULL, 16)); 
        this->source          = static_cast<ADDRINT>(stoul(line[1], NULL, 16)); 
        this->destination     = static_cast<ADDRINT>(stoul(line[2], NULL, 16)); 
        this->fallThrough     = static_cast<ADDRINT>(stoul(line[3], NULL, 16)); 
        this->rtnAddress      = static_cast<ADDRINT>(stoul(line[4], NULL, 16)); 
        this->takenCount      = static_cast<UINT64>(stoul(line[5], NULL, 10)); 
        this->notTakenCount   = static_cast<UINT64>(stoul(line[6], NULL, 10)); 
        this->singleSource    = static_cast<bool>(stoul(line[7], NULL, 10)); 
        this->sharingTarget   = static_cast<bool>(stoul(line[8], NULL, 10));          
    }
};

vector<Edge> rtnReorderVector;
ofstream outFile;
ifstream chosenRtnFile;
unordered_map<ADDRINT, Edge> edgesMap;
unordered_map<ADDRINT, vector<Edge>> rtnReorderMap; // map of (rtnAddress, vector<Edges>)

translated_rtn_t *translated_rtn;
int translated_rtn_num = 0;



/* ============================================================= */
/* Service dump routines                                         */
/* ============================================================= */

/*************************/
/* dump_all_image_instrs */
/*************************/
void dump_all_image_instrs(IMG img)
{
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {   
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
        {        

            // Open the RTN.
            RTN_Open( rtn );

            cerr << RTN_Name(rtn) << ":" << endl;

            for( INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins) )
            {                
                  cerr << "0x" << hex << INS_Address(ins) << ": " << INS_Disassemble(ins) << endl;
            }

            // Close the RTN.
            RTN_Close( rtn );
        }
    }
}


/*************************/
/* dump_instr_from_xedd */
/*************************/
void dump_instr_from_xedd (xed_decoded_inst_t* xedd, ADDRINT address)
{
    // debug print decoded instr:
    char disasm_buf[2048];

    xed_uint64_t runtime_address = static_cast<UINT64>(address);  // set the runtime adddress for disassembly     

    xed_format_context(XED_SYNTAX_INTEL, xedd, disasm_buf, sizeof(disasm_buf), static_cast<UINT64>(runtime_address), 0, 0);    

    cerr << hex << address << ": " << disasm_buf <<  endl;
}


/************************/
/* dump_instr_from_mem */
/************************/
void dump_instr_from_mem (ADDRINT *address, ADDRINT new_addr)
{
  char disasm_buf[2048];
  xed_decoded_inst_t new_xedd;

  xed_decoded_inst_zero_set_mode(&new_xedd,&dstate); 
   
  xed_error_enum_t xed_code = xed_decode(&new_xedd, reinterpret_cast<UINT8*>(address), max_inst_len);                   

  BOOL xed_ok = (xed_code == XED_ERROR_NONE);
  if (!xed_ok){
      cerr << "invalid opcode" << endl;
      return;
  }
 
  xed_format_context(XED_SYNTAX_INTEL, &new_xedd, disasm_buf, 2048, static_cast<UINT64>(new_addr), 0, 0);

  cerr << "0x" << hex << new_addr << ": " << disasm_buf <<  endl;  
 
}


/****************************/
/*  dump_entire_instr_map() */
/****************************/
void dump_entire_instr_map()
{    
    for (int i=0; i < num_of_instr_map_entries; i++) {
        for (int j=0; j < translated_rtn_num; j++) {
            if (translated_rtn[j].instr_map_entry == i) {

                RTN rtn = RTN_FindByAddress(translated_rtn[j].rtn_addr);

                if (rtn == RTN_Invalid()) {
                    cerr << "Unknwon"  << ":" << endl;
                } else {
                  cerr << RTN_Name(rtn) << ":" << endl;
                }
            }
        }
        dump_instr_from_mem ((ADDRINT *)instr_map[i].new_ins_addr, instr_map[i].new_ins_addr);        
    }
}


/**************************/
/* dump_instr_map_entry */
/**************************/
void dump_instr_map_entry(int instr_map_entry)
{
    cerr << dec << instr_map_entry << ": ";
    cerr << " orig_ins_addr: " << hex << instr_map[instr_map_entry].orig_ins_addr;
    cerr << " new_ins_addr: " << hex << instr_map[instr_map_entry].new_ins_addr;
    cerr << " orig_targ_addr: " << hex << instr_map[instr_map_entry].orig_targ_addr;

    ADDRINT new_targ_addr;
    if (instr_map[instr_map_entry].targ_map_entry >= 0)
        new_targ_addr = instr_map[instr_map[instr_map_entry].targ_map_entry].new_ins_addr;
    else
        new_targ_addr = instr_map[instr_map_entry].orig_targ_addr;

    cerr << " new_targ_addr: " << hex << new_targ_addr;
    cerr << "    new instr:";
    dump_instr_from_mem((ADDRINT *)instr_map[instr_map_entry].encoded_ins, instr_map[instr_map_entry].new_ins_addr);
}


/*************/
/* dump_tc() */
/*************/
void dump_tc()
{
  char disasm_buf[2048];
  xed_decoded_inst_t new_xedd;
  ADDRINT address = (ADDRINT)&tc[0];
  unsigned int size = 0;

  while (address < (ADDRINT)&tc[tc_cursor]) {

      address += size;

      xed_decoded_inst_zero_set_mode(&new_xedd,&dstate); 
   
      xed_error_enum_t xed_code = xed_decode(&new_xedd, reinterpret_cast<UINT8*>(address), max_inst_len);                   

      BOOL xed_ok = (xed_code == XED_ERROR_NONE);
      if (!xed_ok){
          cerr << "invalid opcode" << endl;
          return;
      }
 
      xed_format_context(XED_SYNTAX_INTEL, &new_xedd, disasm_buf, 2048, static_cast<UINT64>(address), 0, 0);

      cerr << "0x" << hex << address << ": " << disasm_buf <<  endl;

      size = xed_decoded_inst_get_length (&new_xedd);    
  }
}


/* ============================================================= */
/* Translation routines                                         */
/* ============================================================= */


/*************************/
/* add_new_instr_entry() */
/*************************/
int add_new_instr_entry(xed_decoded_inst_t *xedd, ADDRINT pc, unsigned int size)
{

    // copy orig instr to instr map:
    ADDRINT orig_targ_addr = 0;

    if (xed_decoded_inst_get_length (xedd) != size) {
        cerr << "Invalid instruction decoding" << endl;
        return -1;
    }

    xed_uint_t disp_byts = xed_decoded_inst_get_branch_displacement_width(xedd);
    
    xed_int32_t disp;

    if (disp_byts > 0) { // there is a branch offset.
      disp = xed_decoded_inst_get_branch_displacement(xedd);
      orig_targ_addr = pc + xed_decoded_inst_get_length (xedd) + disp;    
    }

    // Converts the decoder request to a valid encoder request:
    xed_encoder_request_init_from_decode (xedd);

    unsigned int new_size = 0;
    
    xed_error_enum_t xed_error = xed_encode (xedd, reinterpret_cast<UINT8*>(instr_map[num_of_instr_map_entries].encoded_ins), max_inst_len , &new_size);
    if (xed_error != XED_ERROR_NONE) {
        cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;        
        return -1;
    }    
    
    // add a new entry in the instr_map:
    
    instr_map[num_of_instr_map_entries].orig_ins_addr = pc;
    instr_map[num_of_instr_map_entries].new_ins_addr = (ADDRINT)&tc[tc_cursor];  // set an initial estimated addr in tc
    instr_map[num_of_instr_map_entries].orig_targ_addr = orig_targ_addr; 
    instr_map[num_of_instr_map_entries].hasNewTargAddr = false;
    instr_map[num_of_instr_map_entries].targ_map_entry = -1;
    instr_map[num_of_instr_map_entries].size = new_size;    
    instr_map[num_of_instr_map_entries].category_enum = xed_decoded_inst_get_category(xedd);

    num_of_instr_map_entries++;

    // update expected size of tc:
    tc_cursor += new_size;             

    if (num_of_instr_map_entries >= max_ins_count) {
        cerr << "out of memory for map_instr" << endl;
        return -1;
    }
    

    // debug print new encoded instr:
    if (KnobVerbose) {
        cerr << "    new instr:";
        dump_instr_from_mem((ADDRINT *)instr_map[num_of_instr_map_entries-1].encoded_ins, instr_map[num_of_instr_map_entries-1].new_ins_addr);
    }

    return new_size;
}


/*************************************************/
/* chain_all_direct_br_and_call_target_entries() */
/*************************************************/
int chain_all_direct_br_and_call_target_entries()
{
    for (int i=0; i < num_of_instr_map_entries; i++) {                

        if (instr_map[i].orig_targ_addr == 0)
            continue;

        if (instr_map[i].hasNewTargAddr)
            continue;

        for (int j = 0; j < num_of_instr_map_entries; j++) {

            if (j == i)
               continue;
    
            if (instr_map[j].orig_ins_addr == instr_map[i].orig_targ_addr) {
                instr_map[i].hasNewTargAddr = true; 
                instr_map[i].targ_map_entry = j;
                break;
            }
        }
    }
   
    return 0;
}


/**************************/
/* fix_rip_displacement() */
/**************************/
int fix_rip_displacement(int instr_map_entry) 
{
    //debug print:
    //dump_instr_map_entry(instr_map_entry);

    xed_decoded_inst_t xedd;
    xed_decoded_inst_zero_set_mode(&xedd,&dstate); 
                   
    xed_error_enum_t xed_code = xed_decode(&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), max_inst_len);
    if (xed_code != XED_ERROR_NONE) {
        cerr << "ERROR: xed decode failed for instr at: " << "0x" << hex << instr_map[instr_map_entry].new_ins_addr << endl;
        return -1;
    }

    unsigned int memops = xed_decoded_inst_number_of_memory_operands(&xedd);

    if (instr_map[instr_map_entry].orig_targ_addr != 0)  // a direct jmp or call instruction.
        return 0;

    //cerr << "Memory Operands" << endl;
    bool isRipBase = false;
    xed_reg_enum_t base_reg = XED_REG_INVALID;
    xed_int64_t disp = 0;
    for(unsigned int i=0; i < memops ; i++)   {

        base_reg = xed_decoded_inst_get_base_reg(&xedd,i);
        disp = xed_decoded_inst_get_memory_displacement(&xedd,i);

        if (base_reg == XED_REG_RIP) {
            isRipBase = true;
            break;
        }
        
    }

    if (!isRipBase)
        return 0;

            
    //xed_uint_t disp_byts = xed_decoded_inst_get_memory_displacement_width(xedd,i); // how many byts in disp ( disp length in byts - for example FFFFFFFF = 4
    xed_int64_t new_disp = 0;
    xed_uint_t new_disp_byts = 4;   // set maximal num of byts for now.

    unsigned int orig_size = xed_decoded_inst_get_length (&xedd);

    // modify rip displacement. use direct addressing mode:    
    new_disp = instr_map[instr_map_entry].orig_ins_addr + disp + orig_size; // xed_decoded_inst_get_length (&xedd_orig);
    xed_encoder_request_set_base0 (&xedd, XED_REG_INVALID);

    //Set the memory displacement using a bit length 
    xed_encoder_request_set_memory_displacement (&xedd, new_disp, new_disp_byts);

    unsigned int size = XED_MAX_INSTRUCTION_BYTES;
    unsigned int new_size = 0;
            
    // Converts the decoder request to a valid encoder request:
    xed_encoder_request_init_from_decode (&xedd);
    
    xed_error_enum_t xed_error = xed_encode (&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), size , &new_size); // &instr_map[i].size
    if (xed_error != XED_ERROR_NONE) {
        cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;
        dump_instr_map_entry(instr_map_entry); 
        return -1;
    }                

    if (KnobVerbose) {
        dump_instr_map_entry(instr_map_entry);
    }

    return new_size;
}


/************************************/
/* fix_direct_br_call_to_orig_addr */
/************************************/
int fix_direct_br_call_to_orig_addr(int instr_map_entry)
{

    xed_decoded_inst_t xedd;
    xed_decoded_inst_zero_set_mode(&xedd,&dstate); 
                   
    xed_error_enum_t xed_code = xed_decode(&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), max_inst_len);
    if (xed_code != XED_ERROR_NONE) {
        cerr << "ERROR: xed decode failed for instr at: " << "0x" << hex << instr_map[instr_map_entry].new_ins_addr << endl;
        return -1;
    }
    
    xed_category_enum_t category_enum = xed_decoded_inst_get_category(&xedd);

    // cerr << "ENCODE CATEGORY: " << xed_category_enum_t2str(category_enum) << endl;
    // if (category_enum != XED_CATEGORY_CALL && category_enum != XED_CATEGORY_UNCOND_BR /*&& category_enum != XED_CATEGORY_COND_BR*/ ) {

    //     cerr << "ERROR: Invalid direct jump from translated code to original code in rotuine: " 
    //           << RTN_Name(RTN_FindByAddress(instr_map[instr_map_entry].orig_ins_addr)) << endl;
    //     dump_instr_map_entry(instr_map_entry);
    //     return -1;
    // }

    // check for cases of direct jumps/calls back to the orginal target address:
    if (instr_map[instr_map_entry].targ_map_entry >= 0) {
        cerr << "ERROR: Invalid jump or call instruction" << endl;
        return -1;
    }

    unsigned int ilen = XED_MAX_INSTRUCTION_BYTES;
    unsigned int olen = 0;
                

    xed_encoder_instruction_t  enc_instr;

    ADDRINT new_disp = (ADDRINT)&instr_map[instr_map_entry].orig_targ_addr - 
                       instr_map[instr_map_entry].new_ins_addr - 
                       xed_decoded_inst_get_length (&xedd);

    if (category_enum == XED_CATEGORY_CALL)
            xed_inst1(&enc_instr, dstate, 
            XED_ICLASS_CALL_NEAR, 64,
            xed_mem_bd (XED_REG_RIP, xed_disp(new_disp, 32), 64));

    if (category_enum == XED_CATEGORY_UNCOND_BR)
            xed_inst1(&enc_instr, dstate, 
            XED_ICLASS_JMP, 64,
            xed_mem_bd (XED_REG_RIP, xed_disp(new_disp, 32), 64));

    // if (category_enum == XED_CATEGORY_COND_BR)
    // {
    //     cout << xed_iclass_enum_t2str(xed_decoded_inst_get_iclass(&xedd)) << endl;
    //     xed_inst1(&enc_instr, dstate, 
    //     xed_decoded_inst_get_iclass(&xedd), 64,
    //     xed_relbr (new_disp, 64));
    // }
            


    xed_encoder_request_t enc_req;

    xed_encoder_request_zero_set_mode(&enc_req, &dstate);
    xed_bool_t convert_ok = xed_convert_to_encoder_request(&enc_req, &enc_instr);
    if (!convert_ok) {
        cerr << "conversion to encode request failed" << endl;
        return -1;
    }
   

    xed_error_enum_t xed_error = xed_encode(&enc_req, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), ilen, &olen);
    if (xed_error != XED_ERROR_NONE) {
        // cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;
        dump_instr_map_entry(instr_map_entry); 
        return -1;
    }
    // cout << "here" << endl;

    // handle the case where the original instr size is different from new encoded instr:
    if (olen != xed_decoded_inst_get_length (&xedd)) {
        
        new_disp = (ADDRINT)&instr_map[instr_map_entry].orig_targ_addr - 
                   instr_map[instr_map_entry].new_ins_addr - olen;

        if (category_enum == XED_CATEGORY_CALL)
            xed_inst1(&enc_instr, dstate, 
            XED_ICLASS_CALL_NEAR, 64,
            xed_mem_bd (XED_REG_RIP, xed_disp(new_disp, 32), 64));

        if (category_enum == XED_CATEGORY_UNCOND_BR)
            xed_inst1(&enc_instr, dstate, 
            XED_ICLASS_JMP, 64,
            xed_mem_bd (XED_REG_RIP, xed_disp(new_disp, 32), 64));


        xed_encoder_request_zero_set_mode(&enc_req, &dstate);
        xed_bool_t convert_ok = xed_convert_to_encoder_request(&enc_req, &enc_instr);
        if (!convert_ok) {
            cerr << "conversion to encode request failed" << endl;
            return -1;
        }

        xed_error = xed_encode (&enc_req, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), ilen , &olen);
        if (xed_error != XED_ERROR_NONE) {
            cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;
            dump_instr_map_entry(instr_map_entry);
            return -1;
        }        
    }

    
    // debug prints:
    if (KnobVerbose) {
        dump_instr_map_entry(instr_map_entry); 
    }
        
    instr_map[instr_map_entry].hasNewTargAddr = true;
    return olen;    
}


/***********************************/
/* fix_direct_br_call_displacement */
/***********************************/
int fix_direct_br_call_displacement(int instr_map_entry) 
{                    

    xed_decoded_inst_t xedd;
    xed_decoded_inst_zero_set_mode(&xedd,&dstate); 
                   
    xed_error_enum_t xed_code = xed_decode(&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), max_inst_len);
    if (xed_code != XED_ERROR_NONE) {
        cerr << "ERROR: xed decode failed for instr at: " << "0x" << hex << instr_map[instr_map_entry].new_ins_addr << endl;
        return -1;
    }

    xed_int32_t  new_disp = 0;    
    unsigned int size = XED_MAX_INSTRUCTION_BYTES;
    unsigned int new_size = 0;


    xed_category_enum_t category_enum = xed_decoded_inst_get_category(&xedd);
    
    if (category_enum != XED_CATEGORY_CALL && category_enum != XED_CATEGORY_COND_BR && category_enum != XED_CATEGORY_UNCOND_BR) {
        cerr << "ERROR: unrecognized branch displacement" << endl;
        return -1;
    }

    // fix branches/calls to original targ addresses:
    if (instr_map[instr_map_entry].targ_map_entry < 0) {
       int rc = fix_direct_br_call_to_orig_addr(instr_map_entry);
       return rc;
    }

    ADDRINT new_targ_addr;        
    new_targ_addr = instr_map[instr_map[instr_map_entry].targ_map_entry].new_ins_addr;
        
    new_disp = (new_targ_addr - instr_map[instr_map_entry].new_ins_addr) - instr_map[instr_map_entry].size; // orig_size;

    xed_uint_t   new_disp_byts = 4; // num_of_bytes(new_disp);  ???

    // the max displacement size of loop instructions is 1 byte:
    xed_iclass_enum_t iclass_enum = xed_decoded_inst_get_iclass(&xedd);
    if (iclass_enum == XED_ICLASS_LOOP ||  iclass_enum == XED_ICLASS_LOOPE || iclass_enum == XED_ICLASS_LOOPNE) {
      new_disp_byts = 1;
    }

    // the max displacement size of jecxz instructions is ???:
    xed_iform_enum_t iform_enum = xed_decoded_inst_get_iform_enum (&xedd);
    if (iform_enum == XED_IFORM_JRCXZ_RELBRb){
      new_disp_byts = 1;
    }

    // Converts the decoder request to a valid encoder request:
    xed_encoder_request_init_from_decode (&xedd);

    //Set the branch displacement:
    xed_encoder_request_set_branch_displacement (&xedd, new_disp, new_disp_byts);

    xed_uint8_t enc_buf[XED_MAX_INSTRUCTION_BYTES];
    unsigned int max_size = XED_MAX_INSTRUCTION_BYTES;
    
    xed_error_enum_t xed_error = xed_encode (&xedd, enc_buf, max_size , &new_size);
    if (xed_error != XED_ERROR_NONE) {
        cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) <<  endl;
        char buf[2048];        
        xed_format_context(XED_SYNTAX_INTEL, &xedd, buf, 2048, static_cast<UINT64>(instr_map[instr_map_entry].orig_ins_addr), 0, 0);
        cerr << " instr: " << "0x" << hex << instr_map[instr_map_entry].orig_ins_addr << " : " << buf <<  endl;
          return -1;
    }        

    new_targ_addr = instr_map[instr_map[instr_map_entry].targ_map_entry].new_ins_addr;

    new_disp = new_targ_addr - (instr_map[instr_map_entry].new_ins_addr + new_size);  // this is the correct displacemnet.

    //Set the branch displacement:
    xed_encoder_request_set_branch_displacement (&xedd, new_disp, new_disp_byts);
    
    xed_error = xed_encode (&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), size , &new_size); // &instr_map[i].size
    if (xed_error != XED_ERROR_NONE) {
        cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;
        dump_instr_map_entry(instr_map_entry);
        return -1;
    }                

    //debug print of new instruction in tc:
    if (KnobVerbose) {
        dump_instr_map_entry(instr_map_entry);
    }

    return new_size;
}                


/************************************/
/* fix_instructions_displacements() */
/************************************/
int fix_instructions_displacements()
{
   // fix displacemnets of direct branch or call instructions:

    int size_diff = 0;    

    do {
        
        size_diff = 0;

        if (KnobVerbose) {
            cerr << "starting a pass of fixing instructions displacements: " << endl;
        }

        for (int i=0; i < num_of_instr_map_entries; i++) {

            instr_map[i].new_ins_addr += size_diff;
                   
            int new_size = 0;

            // fix rip displacement:            
            new_size = fix_rip_displacement(i);
            if (new_size < 0)
                return -1;

            if (new_size > 0) { // this was a rip-based instruction which was fixed.

                if (instr_map[i].size != (unsigned int)new_size) {
                   size_diff += (new_size - instr_map[i].size);                     
                   instr_map[i].size = (unsigned int)new_size;                                
                }

                continue;   
            }

            // check if it is a direct branch or a direct call instr:
            if (instr_map[i].orig_targ_addr == 0) {
                continue;  // not a direct branch or a direct call instr.
            }


            // fix instr displacement:            
            new_size = fix_direct_br_call_displacement(i);
            if (new_size < 0)
                return -1;

            if (instr_map[i].size != (unsigned int)new_size) {
               size_diff += (new_size - instr_map[i].size);
               instr_map[i].size = (unsigned int)new_size;
            }

        }  // end int i=0; i ..

    } while (size_diff != 0);

   return 0;
 }


bool shouldRevert(Edge& e)
{
    return e.takenCount > e.notTakenCount;
}


    
/*****************************************/
/* find_candidate_rtns_for_translation() */
/*****************************************/
int find_candidate_rtns_for_translation(IMG img)
{
    //map<ADDRINT, xed_decoded_inst_t> local_instrs_map;
    vector<pair<ADDRINT, xed_decoded_inst_t>> localInsVector;
    localInsVector.clear();
    //local_instrs_map.clear();

    // go over routines and check if they are candidates for translation and mark them for translation:

    //new code

    //inline
    //endof inline

    //reorder
    // ADDRINT wantedRtnAddress = 0x404707;
    ADDRINT wantedRtnAddress = 0x406e0d;
    RTN rtn = RTN_FindByAddress(wantedRtnAddress);

    if (!RTN_Valid(rtn))
    {
        return -1;
    }

    vector<Edge> edgesVector = rtnReorderMap[wantedRtnAddress];

    translated_rtn[translated_rtn_num].rtn_addr = RTN_Address(rtn);            
    translated_rtn[translated_rtn_num].rtn_size = RTN_Size(rtn);
    // cout << "rtn size is " << translated_rtn[translated_rtn_num].rtn_size << endl;

    RTN_Open(rtn);
    INS ins = RTN_InsHead(rtn);
    // put first block in place
    for (ins = ins; !INS_IsControlFlow(ins); ins = INS_Next(ins))
    {
        ADDRINT addr = INS_Address(ins);

        //debug print of orig instruction:
        if (KnobVerbose) {
            // cerr << "from caller: "; //fixme remove
            cerr << "old instr: ";
            cerr << "0x" << hex << INS_Address(ins) << ": " << INS_Disassemble(ins) << endl;
            //xed_print_hex_line(reinterpret_cast<UINT8*>(INS_Address (ins)), INS_Size(ins));                               
        }

        xed_decoded_inst_t xedd;
        xed_error_enum_t xed_code;

        xed_decoded_inst_zero_set_mode(&xedd, &dstate);

        xed_code = xed_decode(&xedd, reinterpret_cast<UINT8*>(addr), max_inst_len);
        if (xed_code != XED_ERROR_NONE) {
            cerr << "ERROR: xed decode failed for instr at: " << "0x" << hex << addr << endl;
            translated_rtn[translated_rtn_num].instr_map_entry = -1;
            break;
        }

        // Save xed and addr into a map to be used later.
        pair<ADDRINT, xed_decoded_inst_t> p(addr, xedd);
        localInsVector.push_back(p);
        //local_instrs_map[addr] = xedd;
    }

    // add last ins of first block!!!!!!!!!!
    ADDRINT addrIns = INS_Address(ins);

    //debug print of orig instruction:
    if (KnobVerbose) {
        // cerr << "from caller: "; //fixme remove
        cerr << "old instr: ";
        cerr << "0x" << hex << addrIns << ": " << INS_Disassemble(ins) << endl;
        //xed_print_hex_line(reinterpret_cast<UINT8*>(INS_Address (ins)), INS_Size(ins));                               
    }

    xed_decoded_inst_t xeddIns;
    xed_error_enum_t xed_code;

    xed_decoded_inst_zero_set_mode(&xeddIns, &dstate);

    xed_code = xed_decode(&xeddIns, reinterpret_cast<UINT8*>(addrIns), max_inst_len);
    if (xed_code != XED_ERROR_NONE) {
        cerr << "ERROR: xed decode failed for instr at: " << "0x" << hex << addrIns << endl;
        translated_rtn[translated_rtn_num].instr_map_entry = -1;
        // break;
    }

    // Save xed and addr into a map to be used later.
    pair<ADDRINT, xed_decoded_inst_t> pIns(addrIns, xeddIns);
    localInsVector.push_back(pIns);
    //local_instrs_map[addr] = xedd;


    /// FIXME: add jump to fallthrough after last ins!!

    ADDRINT addr = INS_Address(ins);
            xed_error_enum_t xed_error;
            unsigned int max_size = XED_MAX_INSTRUCTION_BYTES;
            unsigned int new_size = 0;
            xed_decoded_inst_t xedd2;
            // xed_error_enum_t xed_code;

            xed_decoded_inst_zero_set_mode(&xedd2, &dstate);
            xed_uint8_t enc_buf2[XED_MAX_INSTRUCTION_BYTES];		
            // xed_int32_t disp = xed_decoded_inst_get_branch_displacement(xedd);
            xed_encoder_instruction_t  enc_instr;

            xed_inst1(&enc_instr, dstate, 
                    XED_ICLASS_JMP, 64,
                    // xed_mem_bd (XED_REG_RIP, xed_disp(new_disp, 32), 64));
                    xed_relbr(0, 32)); // create the branch command. relative to something.
                    // xed_relbr(-1 * disp, 32)); // create the branch command. relative to something.
                                    
            xed_encoder_request_t enc_req;

            xed_encoder_request_zero_set_mode(&enc_req, &dstate);
            xed_bool_t convert_ok = xed_convert_to_encoder_request(&enc_req, &enc_instr);
            if (!convert_ok) {
                cerr << "conversion to encode request failed" << endl;
                continue;
            }
            // xed_encoder_request_set_uimm0_bits(&enc_req, currEdge.destination, 32);
            xed_error = xed_encode (&enc_req, enc_buf2, max_size, &new_size);
            if (xed_error != XED_ERROR_NONE) {
                cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;				
                continue;
            }		


            // create a decoded xed object to be added to localInsVector
            xed_decoded_inst_zero_set_mode(&xedd2, &dstate);
            xed_code = xed_decode(&xedd2, enc_buf2, max_inst_len);
            if (xed_code != XED_ERROR_NONE) {
                cerr << "ERROR: xed decode failed for instr at: " << "0x" << hex << addr << endl;
                translated_rtn[translated_rtn_num].instr_map_entry = -1;
                break;
            }
            char buf[2048];
            xed_format_context(XED_SYNTAX_INTEL, &xedd2, buf, 2048, INS_Address(ins), 0, 0);
            cerr << "finish bbl add uncond jump: " << hex << INS_Address(ins) << " " << buf << endl << endl;
            // Save xed and addr into a map to be used later.
            pair<ADDRINT, xed_decoded_inst_t> new_jump(addr, xedd2); 
            localInsVector.push_back(new_jump);


    // USIZE extraSize = 0;
    // add all blocks, by order
    for (unsigned int i = 0; i < edgesVector.size(); i++)
    {
        INS currIns = RTN_InsHead(rtn); 

        Edge currEdge = edgesVector[i];
        if (currEdge.srcBBLHead == translated_rtn[translated_rtn_num].rtn_addr)
        {
            continue;
        }

        // find start of block
        while (INS_Address(currIns) != currEdge.srcBBLHead) { currIns = INS_Next(currIns); }
        INS bblIns = currIns;

        // put block in local vector
        for (bblIns = bblIns; INS_Address(bblIns) != currEdge.source ; bblIns = INS_Next(bblIns))
        {
            // put all xed instructions in cache (or some temp data structure)
            ADDRINT addr = INS_Address(bblIns) ;

            //debug print of orig instruction:
            if (KnobVerbose) {
                cerr << "from caller: "; //fixme remove
                cerr << "old instr: ";
                cerr << "0x" << hex << addr << ": " << INS_Disassemble(bblIns) << endl;
                //xed_print_hex_line(reinterpret_cast<UINT8*>(addr), INS_Size(bblIns));                               
            }

            xed_decoded_inst_t xedd;
            xed_error_enum_t xed_code;

            xed_decoded_inst_zero_set_mode(&xedd, &dstate);

            xed_code = xed_decode(&xedd, reinterpret_cast<UINT8*>(addr), max_inst_len);
            if (xed_code != XED_ERROR_NONE) {
                cerr << "ERROR: xed decode failed for instr at: " << "0x" << hex << addr << endl;
                translated_rtn[translated_rtn_num].instr_map_entry = -1;
                break;
            }

            // Save xed and addr into a map to be used later.
            pair<ADDRINT, xed_decoded_inst_t> p(addr, xedd);
            localInsVector.push_back(p);
            //local_instrs_map[addr] = xedd;
        }

        // handle last ins!!
        INS ins_tail = bblIns;
        xed_decoded_inst_t *orig_xedd = INS_XedDec(ins_tail);
        xed_category_enum_t category_enum = xed_decoded_inst_get_category(orig_xedd);
        xed_int32_t disp = xed_decoded_inst_get_branch_displacement(orig_xedd);
                cout << "disp: " << disp << endl;

        if (INS_IsBranch(bblIns) && (category_enum == XED_CATEGORY_COND_BR))
        {
            if (KnobVerbose) {
                cerr << "Inside if: "; //fixme remove
                cerr << "old instr: ";
                cerr << "0x" << hex << INS_Address(bblIns) << ": " << INS_Disassemble(bblIns) << endl;
                //xed_print_hex_line(reinterpret_cast<UINT8*>(addr), INS_Size(bblIns));                               
            }
            xed_iclass_enum_t iclass_enum = xed_decoded_inst_get_iclass(orig_xedd);

            if (iclass_enum == XED_ICLASS_JRCXZ) // do not revert JRCXZ
                continue;    

            xed_iclass_enum_t 	retverted_iclass;
            if (shouldRevert(currEdge))
            {
                switch (iclass_enum) {

                case XED_ICLASS_JB:
                    retverted_iclass = XED_ICLASS_JNB;		
                    break;

                case XED_ICLASS_JBE:
                    retverted_iclass = XED_ICLASS_JNBE;
                    break;

                case XED_ICLASS_JL:
                    retverted_iclass = XED_ICLASS_JNL;
                    break;
            
                case XED_ICLASS_JLE:
                    retverted_iclass = XED_ICLASS_JNLE;
                    break;

                case XED_ICLASS_JNB: 
                    retverted_iclass = XED_ICLASS_JB;
                    break;

                case XED_ICLASS_JNBE: 
                    retverted_iclass = XED_ICLASS_JBE;
                    break;

                case XED_ICLASS_JNL:
                    retverted_iclass = XED_ICLASS_JL;
                    break;

                case XED_ICLASS_JNLE:
                    retverted_iclass = XED_ICLASS_JLE;
                    break;

                case XED_ICLASS_JNO:
                    retverted_iclass = XED_ICLASS_JO;
                    break;

                case XED_ICLASS_JNP: 
                    retverted_iclass = XED_ICLASS_JP;
                    break;

                case XED_ICLASS_JNS: 
                    retverted_iclass = XED_ICLASS_JS;
                    break;

                case XED_ICLASS_JNZ:
                    retverted_iclass = XED_ICLASS_JZ;
                    break;

                case XED_ICLASS_JO:
                    retverted_iclass = XED_ICLASS_JNO;
                    break;

                case XED_ICLASS_JP: 
                    retverted_iclass = XED_ICLASS_JNP;
                    break;

                case XED_ICLASS_JS: 
                    retverted_iclass = XED_ICLASS_JNS;
                    break;

                case XED_ICLASS_JZ:
                    retverted_iclass = XED_ICLASS_JNZ;
                    break;
        
                default:
                    continue;
                }

                // Converts the decoder request to a valid encoder request:
                xed_encoder_request_init_from_decode (orig_xedd);

                // set the reverted opcode;
                xed_encoder_request_set_iclass	(orig_xedd, retverted_iclass);
                xed_encoder_request_set_branch_displacement(orig_xedd, -4, 4);

                xed_uint8_t enc_buf[XED_MAX_INSTRUCTION_BYTES];
                unsigned int max_size = XED_MAX_INSTRUCTION_BYTES;
                unsigned int new_size = 0;
            
                xed_error_enum_t xed_error = xed_encode (orig_xedd, enc_buf, max_size, &new_size);
                if (xed_error != XED_ERROR_NONE) {
                    cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) <<  endl;
                    continue;
                }		

                
                // create a direct uncond jump to the same address:
                xed_uint8_t enc_buf2[XED_MAX_INSTRUCTION_BYTES];		
                
                xed_encoder_instruction_t  enc_instr;

                xed_inst1(&enc_instr, dstate, 
                        retverted_iclass, 64,
                        // xed_relbr(-4, 32));
                        xed_relbr((-4), 32));
                        // xed_relbr((((int)INS_Size(INS_Next(bblIns)) * -1) ), 32));
                        // xed_relbr((int)((int)INS_Size(INS_Next(bblIns)) - (int)currEdge.fallThrough - 1), 32));
                //         // xed_imm0(currEdge.fallThrough, 32));
                
                
                         

                xed_encoder_request_t enc_req;

                xed_encoder_request_zero_set_mode(&enc_req, &dstate);
                xed_bool_t convert_ok = xed_convert_to_encoder_request(&enc_req, &enc_instr);
                if (!convert_ok) {
                    cerr << "conversion to encode request failed" << endl;
                    continue;
                }
                // xed_encoder_request_set_uimm0_bits(&enc_req, currEdge.fallThrough, 32);
                xed_error = xed_encode (&enc_req, enc_buf2, max_size, &new_size);
                if (xed_error != XED_ERROR_NONE) {
                    cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;				
                    continue;
                }		
                

                //print the original and the new reverted cond instructions:
                //
                cerr << "orig instr:              " << hex << INS_Address(ins_tail) << " " << INS_Disassemble(ins_tail) << endl;		         

                char buf[2048];		
                xed_decoded_inst_t new_xedd;
                xed_decoded_inst_zero_set_mode(&new_xedd,&dstate);

                xed_error_enum_t xed_code = xed_decode(&new_xedd, enc_buf, XED_MAX_INSTRUCTION_BYTES);
                if (xed_code != XED_ERROR_NONE) {
                    cerr << "ERROR: xed decode failed for instr at: " << "0x" << hex << INS_Address(ins_tail) << endl;
                    continue;
                }

                xed_format_context(XED_SYNTAX_INTEL, &new_xedd, buf, 2048, INS_Address(ins_tail), 0, 0);
                cerr << "reverted cond jump:      " << hex << INS_Address(ins_tail) << " " << buf << endl;		         

                // xed_decoded_inst_zero_set_mode(&new_xedd,&dstate);
                // xed_code = xed_decode(&new_xedd, enc_buf2, XED_MAX_INSTRUCTION_BYTES);
                // if (xed_code != XED_ERROR_NONE) {
                //     cerr << "ERROR: xed decode failed for instr at: " << "0x" << hex << INS_Address(ins_tail) << endl;
                //     continue;
                // }

                xed_format_context(XED_SYNTAX_INTEL, &new_xedd, buf, 2048, INS_Address(ins_tail), 0, 0);
                cerr << "newly added cond jump: " << hex << INS_Address(ins_tail) << " " << buf << endl << endl;

                pair<ADDRINT, xed_decoded_inst_t> p(INS_Address(ins_tail), new_xedd);
                // pair<ADDRINT, xed_decoded_inst_t> p(INS_Address(ins_tail), *orig_xedd);
                localInsVector.push_back(p);
            }
            else // put original jump, because it shouldnt be reverted
            {
                pair<ADDRINT, xed_decoded_inst_t> p(INS_Address(ins_tail), *orig_xedd);
                localInsVector.push_back(p);
            }
        }
            // change jump to next block if needed.
            // revert condition if needed.

            // add to localInsVector
        

        else // not a conditional branch
        {
            // add bblIns
            ADDRINT addr = INS_Address(bblIns);

            //debug print of orig instruction:
            if (KnobVerbose) {
                cerr << "from caller: "; //fixme remove
                cerr << "old instr: ";
                cerr << "0x" << hex << addr << ": " << INS_Disassemble(bblIns) << endl;
                //xed_print_hex_line(reinterpret_cast<UINT8*>(addr), INS_Size(bblIns));                               
            }

            xed_decoded_inst_t xedd;
            xed_error_enum_t xed_code;
            

            xed_decoded_inst_zero_set_mode(&xedd, &dstate);

            xed_code = xed_decode(&xedd, reinterpret_cast<UINT8*>(addr), max_inst_len);
            if (xed_code != XED_ERROR_NONE) {
                cerr << "ERROR: xed decode failed for instr at: " << "0x" << hex << addr << endl;
                translated_rtn[translated_rtn_num].instr_map_entry = -1;
                break;
            }

            // Save xed and addr into a map to be used later.
            pair<ADDRINT, xed_decoded_inst_t> p(addr, xedd);
            localInsVector.push_back(p);
            
        }
        // always adds a jump to the original fallThrough address
            // FIXME: add *new* unconditional jump to next address INS_NextAddress(bblIns) (start of next block) !!!!!!!!!!!!!!!!!!

            // create a direct uncond jump to the same address:
            // ADDRINT addr2 = INS_Address(ins);
        if (!(INS_IsRet(bblIns) || (INS_Category(bblIns) ==  XED_CATEGORY_UNCOND_BR))) // add new jump at end of block, if not ret or uncond jump
        {
            ADDRINT addr = INS_Address(bblIns);
            xed_error_enum_t xed_error;
            unsigned int max_size = XED_MAX_INSTRUCTION_BYTES;
            unsigned int new_size = 0;
            xed_decoded_inst_t xedd2;
            // xed_error_enum_t xed_code;

            xed_decoded_inst_zero_set_mode(&xedd2, &dstate);
            xed_uint8_t enc_buf2[XED_MAX_INSTRUCTION_BYTES];		
            // xed_int32_t disp = xed_decoded_inst_get_branch_displacement(xedd);
            xed_encoder_instruction_t  enc_instr;

            xed_inst1(&enc_instr, dstate, 
                    XED_ICLASS_JMP, 64,
                    // xed_mem_bd (XED_REG_RIP, xed_disp(new_disp, 32), 64));
                    xed_relbr(2, 32)); // create the branch command. relative to something.
                    // xed_relbr(-1 * disp, 32)); // create the branch command. relative to something.
                                    
            xed_encoder_request_t enc_req;

            xed_encoder_request_zero_set_mode(&enc_req, &dstate);
            xed_bool_t convert_ok = xed_convert_to_encoder_request(&enc_req, &enc_instr);
            if (!convert_ok) {
                cerr << "conversion to encode request failed" << endl;
                continue;
            }
            // xed_encoder_request_set_uimm0_bits(&enc_req, currEdge.destination, 32);
            xed_error = xed_encode (&enc_req, enc_buf2, max_size, &new_size);
            if (xed_error != XED_ERROR_NONE) {
                cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;				
                continue;
            }		


            // create a decoded xed object to be added to localInsVector
            xed_decoded_inst_zero_set_mode(&xedd2, &dstate);
            xed_code = xed_decode(&xedd2, enc_buf2, max_inst_len);
            if (xed_code != XED_ERROR_NONE) {
                cerr << "ERROR: xed decode failed for instr at: " << "0x" << hex << addr << endl;
                translated_rtn[translated_rtn_num].instr_map_entry = -1;
                break;
            }
            char buf[2048];
            xed_format_context(XED_SYNTAX_INTEL, &xedd2, buf, 2048, INS_Address(ins_tail), 0, 0);
            cerr << "finish bbl add uncond jump: " << hex << INS_Address(ins_tail) << " " << buf << endl << endl;
            // Save xed and addr into a map to be used later.
            pair<ADDRINT, xed_decoded_inst_t> new_jump(addr, xedd2); 
            localInsVector.push_back(new_jump);
        }

    }
        
    RTN_Close(rtn);
    translated_rtn_num++;
    // put all xeds in a data structure, separated by bbls.
        // data structure should contain the bbl's size in bytes (so we can fix the jumps)
        // should contain a list of the xeds by original order.
    // after arranging, 

    // for (Edge edge : rtnReorderVector)
    // {
    //     RTN rtn = RTN_FindByAddress(edge.rtnAddress);
    //     if (rtn == RTN_Invalid())
    //     {
    //         cerr << "Warning: invalid routine " << endl;
    //         continue;
    //     }
    //     translated_rtn[translated_rtn_num].rtn_addr = RTN_Address(rtn);            
    //     translated_rtn[translated_rtn_num].rtn_size = RTN_Size(rtn);
    //     RTN_Open(rtn);

    //     for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
    //     {
    //         ADDRINT addr = INS_Address(ins);
    //         if (INS_IsControlFlow(ins))
    //         {
    //             if (0/*compare to targeted instructions of rtn*/)
    //             {
    //                     ;
    //             }
    //         }
            
    //         //debug print of orig instruction:
    //         if (KnobVerbose)
    //         {
    //             cerr << "old instr: ";
    //             cerr << "0x" << hex << INS_Address(ins) << ": " << INS_Disassemble(ins) << endl;
    //             //xed_print_hex_line(reinterpret_cast<UINT8*>(INS_Address (ins)), INS_Size(ins));
    //         }

    //         //don't touch
    //         xed_decoded_inst_t xedd;
    //         xed_error_enum_t xed_code;

    //         xed_decoded_inst_zero_set_mode(&xedd, &dstate);

    //         xed_code = xed_decode(&xedd, reinterpret_cast<UINT8*>(addr), max_inst_len);
    //         if (xed_code != XED_ERROR_NONE) {
    //             cerr << "ERROR: xed decode failed for instr at: " << "0x" << hex << addr << endl;
    //             translated_rtn[translated_rtn_num].instr_map_entry = -1;
    //             break;
    //         }
    //         // Save xed and addr into a map to be used later.
    //         pair<ADDRINT, xed_decoded_inst_t> p(addr, xedd);
    //         localInsVector.push_back(p);
    //         //local_instrs_map[addr] = xedd;
    //         //endof don't touch
    //     } //endof ins
        
    //       // debug print of routine name:
    //     if (KnobVerbose)
    //     {
    //         cerr <<   "rtn name: " << RTN_Name(rtn) << " : " << dec << translated_rtn_num << endl;
    //     }

    //     RTN_Close(rtn);
    //     translated_rtn_num++;
    // } //endof rtnReorderVector
    // //endof reoreder


    //endof new code


    // Go over the local_instrs_map map and add each instruction to the instr_map:
    int rtn_num = 0;
    
    //for (map<ADDRINT, xed_decoded_inst_t>::iterator iter = local_instrs_map.begin(); iter != local_instrs_map.end(); iter++) {
    for (unsigned int i = 0; i < localInsVector.size(); i++)
    {
        pair<ADDRINT, xed_decoded_inst_t>* iter = &localInsVector[i];
    
       ADDRINT addr = iter->first;
       xed_decoded_inst_t xedd = iter->second;           

       // Check if we are at a routine header:
       if (translated_rtn[rtn_num].rtn_addr == addr) {
           translated_rtn[rtn_num].instr_map_entry = num_of_instr_map_entries;
           translated_rtn[rtn_num].isSafeForReplacedProbe = true;
           rtn_num++;
       }
    
       //debug print of orig instruction:
       if (KnobVerbose) {
         char disasm_buf[2048];
         xed_format_context(XED_SYNTAX_INTEL, &xedd, disasm_buf, 2048, static_cast<UINT64>(addr), 0, 0);               
         cerr << "0x" << hex << addr << ": " << disasm_buf  <<  endl; 
       }
      
       // Add instr into global instr_map:
       int rc = add_new_instr_entry(&xedd, addr, xed_decoded_inst_get_length(&xedd));
       if (rc < 0) {
           cerr << "ERROR: failed during instructon translation." << endl;
         translated_rtn[rtn_num].instr_map_entry = -1;
         break;
       }
       
       // Check if this is a direct call instr:    
       xed_category_enum_t category_enum = xed_decoded_inst_get_category(&xedd);
       if (category_enum == XED_CATEGORY_CALL) { 
          xed_int64_t disp = xed_decoded_inst_get_branch_displacement(&xedd);
          ADDRINT target_addr = addr + xed_decoded_inst_get_length (&xedd) + disp; 
          RTN rtn = RTN_FindByAddress(target_addr);
          RTN_Open(rtn);          
          for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
               ADDRINT addr = INS_Address(ins);
               cerr << " Callee addr: " << std::hex << addr << "\n";
          }
          RTN_Close(rtn);
       }                
      
       
    } // end for map<...

    return 0;
}


/***************************/
/* int copy_instrs_to_tc() */
/***************************/
int copy_instrs_to_tc()
{
    int cursor = 0;

    for (int i=0; i < num_of_instr_map_entries; i++) {

      if ((ADDRINT)&tc[cursor] != instr_map[i].new_ins_addr) {
          cerr << "ERROR: Non-matching instruction addresses: " << hex << (ADDRINT)&tc[cursor] << " vs. " << instr_map[i].new_ins_addr << endl;
          return -1;
      }      

      memcpy(&tc[cursor], &instr_map[i].encoded_ins, instr_map[i].size);

      cursor += instr_map[i].size;
    }

    return 0;
}


/*************************************/
/* void commit_translated_routines() */
/*************************************/
inline void commit_translated_routines() 
{
    // Commit the translated functions: 
    // Go over the candidate functions and replace the original ones by their new successfully translated ones:

    for (int i=0; i < translated_rtn_num; i++) {
        // cerr << "in tc commit" << endl;
        //replace function by new function in tc
        // cerr << "trying to commit rtn num " << i << endl;
        if (translated_rtn[i].instr_map_entry >= 0) {
            // cout << "translated_rtn[i].rtn_size is " << translated_rtn[i].rtn_size << endl;
            // cout << "translated_rtn[i].isSafeForReplacedProbe is " << translated_rtn[i].isSafeForReplacedProbe << endl;
            if (translated_rtn[i].rtn_size > MAX_PROBE_JUMP_INSTR_BYTES && translated_rtn[i].isSafeForReplacedProbe) {                        

                RTN rtn = RTN_FindByAddress(translated_rtn[i].rtn_addr);

                //debug print:                
                if (rtn == RTN_Invalid()) {
                    cerr << "committing rtN: Unknown";
                } else {
                    cerr << "committing rtN: " << RTN_Name(rtn);
                }
                cerr << " from: 0x" << hex << RTN_Address(rtn) << " to: 0x" << hex << instr_map[translated_rtn[i].instr_map_entry].new_ins_addr << endl;

                        
                if (RTN_IsSafeForProbedReplacement(rtn)) {

                    AFUNPTR origFptr = RTN_ReplaceProbed(rtn,  (AFUNPTR)instr_map[translated_rtn[i].instr_map_entry].new_ins_addr);                            

                    if (origFptr == NULL) {
                        cerr << "RTN_ReplaceProbed failed.";
                    } else {
                        cerr << "RTN_ReplaceProbed succeeded. ";
                    }
                    cerr << " orig routine addr: 0x" << hex << translated_rtn[i].rtn_addr
                            << " replacement routine addr: 0x" << hex << instr_map[translated_rtn[i].instr_map_entry].new_ins_addr << endl;    

                    dump_instr_from_mem ((ADDRINT *)translated_rtn[i].rtn_addr, translated_rtn[i].rtn_addr);                                                
                }                                                
            }
            // else
            // {
            //     cerr << "didnt make second if" << endl;
            // }
        }
        // else
        // {
        //     cerr << "didnt make first if" << endl;
        // }
    }
}


/****************************/
/* allocate_and_init_memory */
/****************************/ 
int allocate_and_init_memory(IMG img) 
{
    // Calculate size of executable sections and allocate required memory:
    //
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {   
        if (!SEC_IsExecutable(sec) || SEC_IsWriteable(sec) || !SEC_Address(sec))
            continue;


        if (!lowest_sec_addr || lowest_sec_addr > SEC_Address(sec))
            lowest_sec_addr = SEC_Address(sec);

        if (highest_sec_addr < SEC_Address(sec) + SEC_Size(sec))
            highest_sec_addr = SEC_Address(sec) + SEC_Size(sec);

        // need to avouid using RTN_Open as it is expensive...
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
        {        

            if (rtn == RTN_Invalid())
                continue;

            max_ins_count += RTN_NumIns  (rtn);
            max_rtn_count++;
        }
    }

    max_ins_count *= 4; // estimating that the num of instrs of the inlined functions will not exceed the total nunmber of the entire code.
    
    // Allocate memory for the instr map needed to fix all branch targets in translated routines:
    instr_map = (instr_map_t *)calloc(max_ins_count, sizeof(instr_map_t));
    if (instr_map == NULL) {
        perror("calloc");
        return -1;
    }


    // Allocate memory for the array of candidate routines containing inlineable function calls:
    // Need to estimate size of inlined routines.. ???
    translated_rtn = (translated_rtn_t *)calloc(max_rtn_count, sizeof(translated_rtn_t));
    if (translated_rtn == NULL) {
        perror("calloc");
        return -1;
    }


    // get a page size in the system:
    int pagesize = sysconf(_SC_PAGE_SIZE);
    if (pagesize == -1) {
      perror("sysconf");
      return -1;
    }

    ADDRINT text_size = (highest_sec_addr - lowest_sec_addr) * 2 + pagesize * 4;

    int tclen = 2 * text_size + pagesize * 4;   // need a better estimate???

    // Allocate the needed tc with RW+EXEC permissions and is not located in an address that is more than 32bits afar:        
    char * addr = (char *) mmap(NULL, tclen, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    if ((ADDRINT) addr == 0xffffffffffffffff) {
        cerr << "failed to allocate tc" << endl;
        return -1;
    }
    
    tc = (char *)addr;
    return 0;
}



/* ============================================ */
/* Main translation routine                     */
/* ============================================ */
VOID ImageLoad(IMG img, VOID *v)
{
    // debug print of all images' instructions
    //dump_all_image_instrs(img);


    // Step 0: Check the image and the CPU:
    if (!IMG_IsMainExecutable(img))
        return;

    int rc = 0;

    // step 1: Check size of executable sections and allocate required memory:    
    rc = allocate_and_init_memory(img);
    if (rc < 0)
        return;

    cout << "after memory allocation" << endl;

    
    // Step 2: go over all routines and identify candidate routines and copy their code into the instr map IR:
    rc = find_candidate_rtns_for_translation(img);
    if (rc < 0)
        return;

    cout << "after identifying candidate routines" << endl;     
    
    // Step 3: Chaining - calculate direct branch and call instructions to point to corresponding target instr entries:
    rc = chain_all_direct_br_and_call_target_entries();
    if (rc < 0 )
        return;
    
    cout << "after calculate direct br targets" << endl;

    // Step 4: fix rip-based, direct branch and direct call displacements:
    rc = fix_instructions_displacements();
    if (rc < 0 )
        return;
    
    cout << "after fix instructions displacements" << endl;


    // Step 5: write translated routines to new tc:
    rc = copy_instrs_to_tc();
    if (rc < 0 )
        return;

    cout << "after write all new instructions to memory tc" << endl;

   if (KnobDumpTranslatedCode) {
       cerr << "Translation Cache dump:" << endl;
       dump_tc();  // dump the entire tc

       cerr << endl << "instructions map dump:" << endl;
       dump_entire_instr_map();     // dump all translated instructions in map_instr
   }


    // Step 6: Commit the translated routines:
    //Go over the candidate functions and replace the original ones by their new successfully translated ones:
    cout << "check if should commit" << endl;
    if (!KnobDoNotCommitTranslatedCode) {
      commit_translated_routines();    
      cout << "after commit translated routines" << endl;
    }
}



/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */
INT32 Usage()
{
    cerr << "This tool translated routines of an Intel(R) 64 binary"
         << endl;
    cerr << KNOB_BASE::StringKnobSummary();
    cerr << endl;
    return -1;
}


void printEdge(const Edge& e)
{
    outFile << hex
        /*<< "0x"*/ << e.srcBBLHead << ","
        /*<< "0x"*/ << e.source << ","
        /*<< "0x"*/ << e.destination << ","
        /*<< "0x"*/ << e.fallThrough << ","
        /*<< "0x"*/ << e.rtnAddress << "," 
        << dec
        << e.takenCount << ","
        << e.notTakenCount << "," 
        << e.singleSource << "," 
        << e.sharingTarget << endl;
}

vector<string> split(const string& s, char delim)
{
    vector<string> result;
    stringstream ss (s);
    string item;

    while (getline (ss, item, delim)) {
        result.push_back(item);
    }

    return result;
}


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

    // ADDRINT wantedRtnAddress = 0x406e0d;
    // ADDRINT wantedRtnAddress = 0x404707;

    // if(RTN_Id(RTN_FindByAddress(wantedRtnAddress)) != RTN_Id(RTN_FindByAddress(TRACE_Address(trc))))
    // {
    //     return;
    // }

    for (BBL bbl = TRACE_BblHead(trc); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        // if (!BBL_HasFallThrough(bbl)) // FIXME: should we really filter out jumps that do not have a fallthrough?
        //     continue;
        INS insTail = BBL_InsTail(bbl);
        INS insHead = BBL_InsHead(bbl);

        // if (!(INS_IsDirectControlFlow(insTail) || INS_IsDirectCall(insTail))) // dont take into account because there is no branching, or target might not be constant.
        // {
        //     // if (!INS_IsDirectCall(insTail))
        //     // {
        //         continue;
        //     // }
        // }

        
        ADDRINT tailAddress = INS_Address(insTail);
        ADDRINT headAddress = INS_Address(insHead);

        // cout << "inside rtn " << hex << wantedRtnAddress << " in block head " << headAddress << " and block tail " << tailAddress << endl;

        if (edgesMap.find(headAddress) == edgesMap.end()) // edge not found. create it.
        {
            ADDRINT insFallThroughAddress = INS_NextAddress(insTail);
            ADDRINT targetAddress = 0xFFFFFFFFFFFFFFFF;
            if (INS_IsDirectControlFlow(insTail))
            {
                targetAddress = INS_DirectControlFlowTargetAddress(insTail);
            }
            

            //ignore edges which connect different rtns. 
            // this also ignores calls (?)
            RTN sourceRtn = RTN_FindByAddress(tailAddress);
            // RTN targetRtn = RTN_FindByAddress(targetAddress);
            // if (RTN_Valid(targetRtn) && (RTN_Id(sourceRtn) != RTN_Id(targetRtn)) )
            // {
            //     // recursive routine, invalidate this rtn from beaing reordered.
            //     continue;
            // }
                

            Edge edge(headAddress, tailAddress, targetAddress, insFallThroughAddress, RTN_Address(sourceRtn));
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


VOID FiniProf(INT32 code, VOID* v)
{
    outFile << "source bbl head,source,destination,fallthrough,rtn address,taken count,not taken count,single source, sharing target" << endl;
    setSingleSource(); 
    findTargetEdges(); 
}


/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char * argv[])
{

    // Initialize pin & symbol manager
    //out = new std::ofstream("xed-print.out");

    if( PIN_Init(argc,argv) )
        return Usage();

    PIN_InitSymbols();

    if (KnobProf)
    {
        outFile.open("edge-count.csv");
        TRACE_AddInstrumentFunction(Trace, 0);

        // Register FiniProf to be called when the application exits
        PIN_AddFiniFunction(FiniProf, 0);
    

        // Start the program, never returns
        PIN_StartProgram();
    }

    // Register ImageLoad

    else if (KnobOpt)
    {
        chosenRtnFile.open("edge-count.csv");
		if (chosenRtnFile.fail())
		{
			cerr << endl << endl
			<< "Something wrong with inline-count.csv. try running with \"-prof\" option first!"
			<< endl << endl << endl;
			return Usage();
		}

        string line;
        std::getline(chosenRtnFile, line); // remove headlines

        // parse lines
        while (chosenRtnFile.good() && !chosenRtnFile.eof())
        {
            std::getline(chosenRtnFile, line);
            vector<string> in = split(line, ',');
            if (in.size() == 0) // if reached an empty line, pass
            {
                continue;
            }

            Edge tempEdge = Edge(in);
            
            rtnReorderMap[tempEdge.rtnAddress].push_back(tempEdge);

        }
        cout << "finished parsing input file" << endl;
        chosenRtnFile.close();
        outFile.open("reorder-out.csv");
        outFile << "source bbl head,source,destination,fallthrough,rtn address,taken count,not taken count,single source, sharing target" << endl;
        for (const auto& pair : rtnReorderMap)
        {
           vector<Edge> rtnEdges = pair.second;
            if (rtnEdges.empty())
                continue;
            
            std::sort(rtnEdges.begin(), rtnEdges.end(), compareEdgePtr);
            for (const auto& e : rtnEdges)
                printEdge(e);
        }
        outFile.close();


        IMG_AddInstrumentFunction(ImageLoad, 0);
        // PIN_AddFiniFunction(FiniOpt, 0);
        // Start the program, never returns
        PIN_StartProgramProbed();
    }


    
    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */