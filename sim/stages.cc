#include "cpu.h"
#include "stages.h"

#include <stdio.h>
#include <stdlib.h>

// global branch target buffer
table_t* btb = NULL;

void print_btb(int n){
	printf("==================================\n");
	for(int i = 0; i < n; i++){
		printf("[%d] ", i);
		printf("Inst: 0x%x\t", btb[i].branch_inst_addr);
		printf("Target: 0x%x\t", btb[i].branch_target_addr);
		printf("Bit: %d\n", btb[i].prediction_bit);
	}
	printf("==================================\n");
}

// the three operands are encoded inside a single uint16_t. This method cracks them open
static void inline decode_ops(uint16_t input, byte *dest, byte *src1, byte *src2)
{
	*dest = input & 0x001F;
	*src1 = (input & 0x03E0) >> 5;
	*src2 = (input & 0x7C00) >> 10;
}

static void inline jump_to(uint32_t* pc, uint32_t immediate) {
  if(immediate < 0x00400000)
    *pc = 0x00400000 + immediate; // load the target into the PC.
  else
    *pc = immediate;
}

/**
   The execute implementations. These actually perform the action of the stage.
 **/

// IF Stage ---------------------------------------------------------------
// Instructions are fetched from memory in this stage, and passed into the CPU's ID latch
void InstructionFetchStage::Execute() {

  // Input of the program
  int type_branch_predictor = core->type_branch_predictor; // Type of branch predictor (1: 1-bit, 2: 2-bit)
  int num_btb_entries = core->num_btb_entries; // The number of BTB entries
  //---------------------
  
	// initialize branch target buffer
	if(btb == NULL){
		btb = new table_t[num_btb_entries];
		for(int i = 0; i < num_btb_entries; i++){
			btb[i].branch_inst_addr = 0;
			btb[i].branch_target_addr = 0;
			btb[i].prediction_bit = 0;
		}
	}
	//--------------------------------

	right.opcode = core->mem->get<byte>(core->PC);
	uint16_t operands = core->mem->get<uint16_t>(core->PC + 1);
	decode_ops(operands, &right.Rdest, &right.Rsrc1, &right.Rsrc2);
	right.immediate = core->mem->get<uint32_t>(core->PC + 3);
	// core->PC += 8;
	// right.PC = core->PC;

  // Branch prediction ----------------------------------------------------
  int btb_index = num_btb_entries - 1;
	btb_index <<= 3;
	btb_index &= core -> PC;
	btb_index >>= 3;
	right.btb_index = btb_index;
	if(type_branch_predictor == 1){ // 1-bit predictor
		if(core -> PC == btb[btb_index].branch_inst_addr){ // PC value found in BTB
			right.btb_found = true;
			if(btb[btb_index].prediction_bit == 1){ // prediction bit = 1
				right.predict_taken = true; // predict as taken
				right.PC = core -> PC + 8;
				core -> PC = btb[btb_index].branch_target_addr; // change PC to target address
			}
			else{ // prediction bit = 0
				right.predict_taken = false;
				core -> PC += 8;
				right.PC = core -> PC;
			}
		}
		else{ // PC value not found in BTB
			right.btb_found = false;
			right.predict_taken = false;
			core -> PC += 8;
			right.PC = core -> PC;
		}
	}
	else if(type_branch_predictor == 2){ // 2-bit predictor
		//-------------------------
		// 0: strongly not taken
		// 1: weakly not taken
		// 2: weakly taken
		// 3: strongly taken
		//-------------------------
		if(core -> PC == btb[btb_index].branch_inst_addr){ // PC value found in BTB
			right.btb_found = true;
			switch(btb[btb_index].prediction_bit){
				case 0: // strongly not taken
				case 1: // weakly not taken
					right.predict_taken = false;
					core -> PC += 8;
					right.PC = core -> PC; 
					break;
				case 2: // weakly taken
				case 3: // strongly taken
					right.predict_taken = true;
					right.PC = core -> PC + 8;
					core -> PC = btb[btb_index].branch_target_addr; // change PC to target address
					break;
			}
		}
		else{ // PC value not found in BTB
			right.btb_found = false;
			right.predict_taken = false;
			core -> PC += 8;
			right.PC = core -> PC;
		}
	}
	else{
		printf("Invalid predictor type\n");
		exit(1);
	}

  // ----------------------------------------------------------------------

}


// ID Stage ---------------------------------------------------------------
void InstructionDecodeStage::Execute() {
	core->registers[0] = 0; // wire register 0 to zero for all register reads
	right.Rsrc1Val = *(int32_t *)&core->registers[left.Rsrc1];
	right.Rsrc2Val = *(int32_t *)&core->registers[left.Rsrc2];
	right.immediate = left.immediate;
	right.Rsrc1 = left.Rsrc1;
	right.Rsrc2 = left.Rsrc2;
	right.Rdest = left.Rdest;
	right.PC = left.PC;
	right.opcode = left.opcode;
	right.predict_taken = left.predict_taken;
	right.btb_found = left.btb_found; // btb_found transfer IF -> ID -> EX
	right.btb_index = left.btb_index; // btb_index transfer IF -> ID -> EX
}


// EXE Stage --------------------------------------------------------------
void ExecuteStage::Execute() {
  // Inputs ---------------------------------------------------------------
  // Values transmitted from left
  // * left.opcode
  // * left.Rsrc1Val
  // * left.Rsrc2Val
  // * left.immediate
  // * left.PC
  // * left.predict_taken

  // Decoded instruction (decoded_inst)
  const instruction* decoded_inst;
  decoded_inst = left.control();

  // Source value (svalue)
	int32_t svalue = left.Rsrc1Val;

  // Source parameter (sparam)
  uint32_t param;
	switch (decoded_inst->alu_source) {
	case 0: // source from register
		param = left.Rsrc2Val;
		break;

	case 1: // immediate add/sub
		param = left.immediate;
		break;

	case 2: // address calculation
		param = left.immediate + *(uint32_t *)&left.Rsrc1Val;
		break;
	}
	int32_t sparam = *(int32_t *)&param;

  // Input of the program
  int type_branch_predictor = core->type_branch_predictor; // Type of branch predictor (1: 1-bit, 2: 2-bit)
  int num_btb_entries = core->num_btb_entries; // The number of BTB entries
  //---------------------
  
  // ----------------------------------------------------------------------
  
  // Result (result)
	int32_t result = sparam;

  // ALU Execution --------------------------------------------------------
	switch (decoded_inst->alu_operation) {
	case 0:
		// do nothing, this operation does not require an alu op (copy forward)
		break;

	case 1:
		// do a signed add of reg1 to param
		result = svalue + sparam;
		break;

	case 2:
		// do a signed subtract of param from reg1
		result = svalue - sparam;
		break;
	}
  // ----------------------------------------------------------------------

  // Branch Execution -----------------------------------------------------
	if (decoded_inst->branch) {
		
		core -> branch_count += 1;
		bool taken = (left.opcode == 2 && left.Rsrc1Val == 0) ||
		             (left.opcode == 3 && left.Rsrc1Val >= left.Rsrc2Val) ||
		             (left.opcode == 4 && left.Rsrc1Val != left.Rsrc2Val);
		
		// if mispredict, nop out IF and ID. (mispredict == prediction and taken differ)
    if(core->verbose) {
      if(taken == 1) {
        printf("*** Branch taken: %d %d\n", left.Rsrc1Val, left.Rsrc2Val);
      } else {
        printf("*** Branch not taken: %d %d\n", left.Rsrc1Val, left.Rsrc2Val);
      }
    }

		if(type_branch_predictor == 1){ // 1-bit predictor
			if(left.btb_found){ // found in BTB during IF stage
				if(left.predict_taken){ // predicted to be taken
					if(taken){
						// HIT!
					}
					else{
						// MISS!
						core -> miss_count += 1; // increment misprediction count
						btb[left.btb_index].prediction_bit = 0; // set prediction bit to 0
						core -> PC = left.PC; // go back to not taken PC (PC + 8)
						core -> ifs.make_nop(); // flush IF instruction
						core -> ids.make_nop(); // flush ID instruction
					}
				}
				else{ // predicted not to be taken
					if(taken){
						// MISS!
						core -> miss_count += 1; // increment misprediction count
						btb[left.btb_index].prediction_bit = 1; // set prediction bit to 0
						core -> PC = btb[left.btb_index].branch_target_addr; // set PC to target address
						core -> ifs.make_nop(); // flush IF instruction
						core -> ids.make_nop(); // flush ID instruction
					}
					else{
						// HIT!
					}
				}		
			}
			else{ // not found in BTB during IF stage
				// in this case, branch is always not taken
				if(taken){ // BTB should be updated in this condition
					// MISS!
					// printf("Updated index: %d\n", left.btb_index);
					// print_btb(num_btb_entries);
					core -> miss_count += 1; // increment misprediction count
					btb[left.btb_index].branch_inst_addr = left.PC - 8; // update instruction address
					jump_to(&core -> PC, left.immediate); // update PC to taken branch
					btb[left.btb_index].branch_target_addr = core -> PC; // update target address
					btb[left.btb_index].prediction_bit = 1; // update prediction bit
					core -> ifs.make_nop(); // flush IF instruction
					core -> ids.make_nop(); // flush ID instruction
					//print_btb(num_btb_entries);
				}
				else{
					// HIT!
				}
			}
		}
		else if(type_branch_predictor == 2){ // 2-bit predictor
			//-------------------------
			// 0: strongly not taken
			// 1: weakly not taken
			// 2: weakly taken
			// 3: strongly taken
			//-------------------------
			if(left.btb_found){ // found in BTB during IF stage
				if(left.predict_taken){ // predicted to be taken
					if(taken){
						// HIT!
						if(btb[left.btb_index].prediction_bit == 2) // weakly -> strongly
							btb[left.btb_index].prediction_bit = 3;
					}
					else{
						// MISS!
						core -> miss_count += 1; // increment misprediction count
						if(btb[left.btb_index].prediction_bit >= 2) // strongly -> weakly
							btb[left.btb_index].prediction_bit -= 1;
						core -> PC = left.PC; // go back to not taken PC (PC + 8)
						core -> ifs.make_nop();
						core -> ids.make_nop();
					}
				}
				else{ // predicted not to be taken
					if(taken){
						// MISS!
						core -> miss_count += 1; // increment misprediction count
						if(btb[left.btb_index].prediction_bit <= 1) // strongly -> weakly
							btb[left.btb_index].prediction_bit += 1;
						core -> PC = btb[left.btb_index].branch_target_addr; // set PC to target address
						core -> ifs.make_nop(); // flush IF instruction
						core -> ids.make_nop(); // flush ID instruction
					}
					else{
						// HIT!
						if(btb[left.btb_index].prediction_bit == 1) // weakly -> strongly
							btb[left.btb_index].prediction_bit = 0;
					}
				}
			}
			else{ // not found in BTB during IF stage
				// in this case also, branch is always not taken
				if(taken){ // BTB should be updated in this condition
					// MISS!
					core -> miss_count += 1;
					btb[left.btb_index].branch_inst_addr = left.PC - 8; // update instruction address
					jump_to(&core -> PC, left.immediate); // update PC to taken branch
					btb[left.btb_index].branch_target_addr = core -> PC; // update target address
					btb[left.btb_index].prediction_bit = 3; // update prediction bit to strongly taken
					core -> ifs.make_nop(); // flush IF instruction
					core -> ids.make_nop(); // flush ID instruction
				}
				else{
					// HIT!
				}
			}
		}
		else{
			printf("Invalid predictor type\n");
			exit(1);
		}
	}
	//if (left.predict_taken != taken) {
	//  if(core->verbose) {
	//    printf("*** MISPREDICT!\n");
	//  }
	//}
	//if (taken) {
	//  core->ifs.make_nop();
	//  core->ids.make_nop();
	//  jump_to(&core->PC, left.immediate);
	//}
	// ----------------------------------------------------------------------

	right.aluresult = *(uint32_t *)&result;

	// Copy forward from previous latch -------------------------------------
  right.opcode = left.opcode;
  right.Rsrc1 = left.Rsrc1;
  right.Rsrc2 = left.Rsrc2;
  right.Rsrc1Val = left.Rsrc1Val;
  right.Rsrc2Val = left.Rsrc2Val;
  right.Rdest = left.Rdest;
  // ----------------------------------------------------------------------
}

// MEM Stage --------------------------------------------------------------

void MemoryStage::Execute()
{
	const instruction *control = left.control();

	right.aluresult = left.aluresult;
	right.mem_data = 0;
	if (control->mem_read) {
		if (control->mem_read == 1) {
			right.mem_data = core->mem->get<byte>(left.aluresult);
		}
		else if (control->mem_read == 4) {
			right.mem_data = core->mem->get<uint32_t>(left.aluresult);
		}
	}
	else if (control->mem_write) {
		if (control->mem_write == 1) {
			core->mem->set<byte>(left.aluresult, left.Rsrc2Val);
		}
		else if (control->mem_write == 4) {
			core->mem->set<uint32_t>(left.aluresult, left.Rsrc2Val);
		}
	}
	right.opcode = left.opcode;
	right.Rsrc1Val = left.Rsrc1Val;
	right.Rsrc2Val = left.Rsrc2Val;
	right.Rdest = left.Rdest;
}


// WB Stage ---------------------------------------------------------------
void WriteBackStage::Execute()
{
	const instruction *control = left.control();

	if (control->special_case != NULL) {
		control->special_case(core);
	}
	if (control->register_write) {
		if (control->mem_to_register) {
			// write the mem_data into register Rdest
			core->registers[left.Rdest] = left.mem_data;
		}
		else {
			// write the alu result into register Rdest
			core->registers[left.Rdest] = left.aluresult;
		}
		core->registers[0] = 0; // wire back to zero
	}
}


void InstructionDecodeStage::Shift()
{
	left = core->ifs.right;
}


void ExecuteStage::Shift()
{
	left = core->ids.right;
}


void MemoryStage::Shift()
{
	left = core->exs.right;
}


void WriteBackStage::Shift()
{
	left = core->mys.right;
}


void InstructionDecodeStage::Resolve()
{
	// Check for a data stall. If found, back up the machine one cycle. -----
	if (core->ids.right.control()->mem_read &&
	    (core->ids.right.Rdest == core->ifs.right.Rsrc1 || 
       core->ids.right.Rdest == core->ifs.right.Rsrc2)) {
		// Bubble the pipe!
		// nop the two stalled instructions, back up the pc by 2 instructions
    if(core->verbose) {
      printf("*** BUBBLE\n");
    }
		core->ifs.make_nop();
		core->PC -= 8;
	}
  // ----------------------------------------------------------------------
}


void ExecuteStage::Resolve()
{
	// If the previous instruction is attempting a READ of the same register the instruction
	// in this stage is supposed to WRITE, then here, update the previous stage's right latch
	// with the value coming out of the ALU. (also, not reading zero reg)
	if (core->exs.right.Rdest != 0 && core->exs.right.control()->register_write) {
		if (core->exs.right.Rdest == core->ids.right.Rsrc1) {
			core->ids.right.Rsrc1Val = core->exs.right.aluresult;
      if(core->verbose) {
        printf("*** FORWARDex1:  %08x going to ID/EX's Rsrc1Val \n",
            core->exs.right.aluresult);
      }
		}
		if (core->exs.right.Rdest == core->ids.right.Rsrc2) {
			core->ids.right.Rsrc2Val = core->exs.right.aluresult;
      if(core->verbose) {
        printf("*** FORWARDex2:  %08x going to ID/EX's Rsrc2Val \n",
            core->exs.right.aluresult);
      }
		}
	}
}


void MemoryStage::Resolve()
{
	// If the next to previous instruction (IDS) is attempting a READ of the same register the instruction
	// in this stage is supposed to WRITE, then here, update the next-to-previous stage's right latch
  // with the value coming out of the this stage. (also, the read is not on zero)
  if (core->mys.right.Rdest != 0 && core->mys.right.control()->register_write) {
    if (core->exs.right.Rdest != core->ids.right.Rsrc1 &&
        core->mys.right.Rdest == core->ids.right.Rsrc1) {
      core->ids.right.Rsrc1Val =
        core->mys.right.control()->mem_read ? core->mys.right.mem_data : core->mys.right.aluresult;
      if(core->verbose) {
        printf("*** FORWARDmem1: %08x going to ID/EX's Rsrc1Val \n",
            core->ids.right.Rsrc1Val);
      }
    }
    if (core->exs.right.Rdest != core->ids.right.Rsrc2 &&
        core->mys.right.Rdest == core->ids.right.Rsrc2) {
      core->ids.right.Rsrc2Val =
        core->mys.right.control()->mem_read ? core->mys.right.mem_data : core->mys.right.aluresult;
      if(core->verbose) {
        printf("*** FORWARDmem2: %08x going to ID/EX's Rsrc2Val \n",
            core->ids.right.Rsrc2Val);
      }
    }
  }
}
