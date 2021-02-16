/* Kyle Plummer - CSI404 - 2020-05-01 - SIA VM - V1.0
 * This virtual machine executes SIA instructions from a binary file.
 * SIA specifies 16 32-bit registers, utilizing the last one as a stack pointer.
 * The VM provides 1KB of virtual memory for both program data and stack. The VM 
 * loads a file at runtime and executes until it reaches a halt instruction.
 * Output should only be expected if an interrupt instruction is given.
 * Interrupt 0 for register dump, interrupt 1 for memory dump.
 * Use: siavm.exe file.bin
 * 
 * Version 2.X: VM now implements pipelining. Fetch, decode, execute, and store functions can now be preformed 
 * in any order. There is now a double buffer for instruction as input for each function: decode, execute, and store.
 * Fetch function grabs instructions from virtual memory where they are stored upon loading the binary file.
 * Operational registers OP1, and OP2 are also double buffered between decode and execute. Result is double
 * buffered between execute and store.
 * 
 * Branching instructions branch, call, jump will all invalidate the pipeline when the program counter
 * is updated during the store step, invalidating sequential instructions already in the pipeline. (if the branch
 * is not taken, no invalidation occurs)
 * 
 * VM implements register forwarding for instructions that depend on output from previous instrucitons.
 * It keeps a history of the last 4 instrucitons and checks it during execution step for dependencies. If 
 * found, the output from the most recent history is used in place, by overwriting the contents of 
 * OP1 and OP2.
 */



//////////////
// includes //
//////////////
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdbool.h>



//////////////////////////
// Virtual architecture //
//////////////////////////

//1KB virtual main memory
unsigned char virtualMemory[1000];

//16 primary registers, 0-15. R15 is the stack pointer
int registers[16];

//VM internal use
bool halt; //halt flag
unsigned int PC; //program counter

//Double buffering added to allow for pipelining. One function can be reading it's input while the previous writes
//safely to a secondary buffer and vice-versa. bool "ready" flags used to mute buffers during read/write: 1 = ready, 0 = muted.
//bool "valid" flags used to determine if there is valid in data either buffer to be used. 1 = valid, 0 = not valid.
//There are also separate instruction buffers to hold the instructions. Each of the primary funcitons
//loads the buffers into local variables, and then writes those locals to a global for the next step.

//double buffer between fetch and decode
bool decodeInstructionValid;
bool decodeBuff1Ready;
bool decodeBuff2Ready;
unsigned char decodeInstructionBuffer1[4];
unsigned char decodeInstructionBuffer2[4];

//double buffer between deocde and execute
bool executeInstructionValid;
bool executeBuff1Ready;
bool executeBuff2Ready;
unsigned char executeInstructionBuffer1[4];
unsigned char executeInstructionBuffer2[4];
int OP1_1;
int OP1_2;
int OP2_1;
int OP2_2;

//double buffer between execute and store
bool storeInstructionValid;
bool storeBuff1Ready;
bool storeBuff2Ready;
unsigned char storeInstructionBuffer1[4];
unsigned char storeInstructionBuffer2[4];
int result1;
int result2;

//historic instruction tracking - for register forwarding. History array will contain the register in 0-3, and in 4-7 the value.
//This creates a rudimentary mapping where resultHistory[n] is the register and resultHistory[n+4] is the value.
int resultHistoryCursor;
int resultHistory[8];




//////////////////////
// Helper functions //
//////////////////////

//historyCheck checks the last 4 results to see if a register being executed on was changed
//recently enough to break continuity. If so, forward the result from that previous execution to the current one.
//In order to eschew a method to check if historyCheck found the register, value is passed as input and returned changed or not.
int historyCheck(int reg, int value) {//vscode throws errors when naming parameter "register" is this a reserved word in c?
    int cursor = resultHistoryCursor;
    //check the history from oldest to newest looking for changes to specified register
    for(int i = 0; i < 4; i++) {
        if(resultHistory[cursor] == reg) {
            value = resultHistory[cursor + 4];
        }
        cursor++;
        //check to make sure cursor stays within the 0-3 range
        if(cursor >= 4) {
            cursor =- 4;
        }
    }
    return value;
}

//historyLog logs the result each time a register is updated for use with historyCheck() and forwarding.
void historyLog(int reg, int value) {
    resultHistoryCursor++;
    //check to make sure cursor stays within the 0-3 range
    if(resultHistoryCursor >= 4) {
        resultHistoryCursor =- 4;
    }

    //store history log
    resultHistory[resultHistoryCursor] = reg;
    resultHistory[resultHistoryCursor + 4] = value;
}

//highHalfByte - returns the high 4 bits from a given byte
//shifted to the low 4 bits of the resultant byte
char highHalfByte(unsigned char byte) {
    return (byte & 240) >> 4;
}

//lowHalfByte - returns the low 4 bits of a given byte
//with the high 4 bits masked out
char lowHalfByte(unsigned char byte) {
    return (byte & 15);
}

//get3R - helper functions for decoding 3R instructions
//get the first register from 3R instructions
unsigned char get3R1(unsigned char instruction[4]) {
    return lowHalfByte(instruction[0]);
}

//get the second register from 3R instrucitons
unsigned char get3R2(unsigned char instruction[4]) {
    return highHalfByte(instruction[1]);
}

//get the third register from 3R instructions
unsigned char get3R3(unsigned char instruction[4]) {
    return lowHalfByte(instruction[1]);
}

//getBR - helper functions for decoding br1 instructions 
//get the first register from BR1 instructions
unsigned char getBR1(unsigned char instruction[4]) {
    return highHalfByte(instruction[1]);
}

//get the second register from BR1 instrucitons
//NOTE: not for BR2 instructions.
unsigned char getBR2(unsigned char instruction[4]) {
    return lowHalfByte(instruction[1]);
}

//getStackRegister - get register from stack instrucitons
unsigned char getStackRegister(unsigned char instruction[4]) {
    return lowHalfByte(instruction[0]);
}

//get register from move instructions
unsigned char getMoveRegister(unsigned char instruction[4]) {
    return lowHalfByte(instruction[0]);
}

//move the stack pointer up or down, roll over if out of bounds
void moveStackPointer(int offset) {
    registers[15] += offset;
    if(registers[15] > 1000) {
        registers[15] -= 1000;
    }
    if(registers[15] < 0) {
        registers[15] += 1000;
    }
    printf("%d\n", registers[15]);
}

//getImmediate - get the immediate value from move instructions,
//and convert to signed.
signed char getImmediate(unsigned char instruction[4]) {
    signed char signedByte = instruction[1];

    //conversion - is this actually necessary?
    if((signedByte >> 7) == 1) {
        signedByte -= 1;
        signedByte = ~signedByte;
    }

    return signedByte;
}

//loadfile - function loads binary file of SIA instructions from disk
void loadFile(char *filename) {
    FILE *in = fopen(filename,"r");
    if (in == NULL) { 
        printf("unable to open input file\n");
        exit(1); 
    }

    //while the end of file has not been encountered, take one character
    //at a time storing in virtual memory.
    char c;
    int cursor = 0;
    while(!feof(in)) {
        c = fgetc(in);
        virtualMemory[cursor] = c;
        cursor++;
    }
    fclose(in);
}

//invalidatePipeline - invalidates instructions in pipeline when program counter jumps
void invalidatePipeline() {
    decodeInstructionValid = 0;
    executeInstructionValid = 0;
    storeInstructionValid = 0;
}





///////////////////////
// Primary Functions //
///////////////////////
// The primary execution loop repeats these 4 functions: fetch, decode, execute, store. 

//fetch function - fetches the next instruction by fetching the 4 bytes at
//the location indicated by the program counter. 2-byte instructions ignore 
//extra 2 bytes fetched. Preforms a check to see if the program counter is
//about to reach the top of the stack.
void fetchInstruction() {
    //printf("DEBUG: begin fetch...\n");
    unsigned char instruction[4];

    if(PC + 4 >= registers[15]) {
        //instructions and stack may have collided. Fetch may retrieve stack data
        printf("Error! Instructions and stack may have collided. Stack ptr: %d, PC: %d\n", registers[15], PC);
        exit(1);
    }
    
    //fetch the 4 bytes at PC in vitrualMemory, the next 2- or 4-byte instruction and place into one of the two buffers
    if (decodeBuff1Ready) {
        decodeBuff1Ready = 0;
        for (int i = 0; i < 4; i++) {
            decodeInstructionBuffer1[i] = virtualMemory[PC + i];
        }
        decodeBuff1Ready = 1;
    }
    else if(decodeBuff2Ready) {
        decodeBuff2Ready = 0;
        for (int i = 0; i < 4; i++) {
            decodeInstructionBuffer2[i] = virtualMemory[PC + i];
        }
        decodeBuff2Ready = 1;
    }

    decodeInstructionValid = 1;
}

//decode function - looks at the opcodes of the instruction fetched and prepares for execution
void decodeInstruction() {
    //printf("DEBUG: begin decode...\n");

    if(decodeInstructionValid) {
        //copy current instruciton from an open double-buffer to local buffer, mute buffer during copy
        unsigned char instruction[4];
        if(decodeBuff1Ready) {
            decodeBuff1Ready = 0;
            for (int i = 0; i < 4; i++) {
                instruction[i] = decodeInstructionBuffer1[i];
            }
            decodeBuff1Ready = 1;
        }
        else if(decodeBuff2Ready) {
            decodeBuff2Ready = 0;
            for (int i = 0; i < 4; i++) {
                instruction[i] = decodeInstructionBuffer2[i];
            }
            decodeBuff2Ready = 1;
        }


        //all instrucitons begin with a 4-bit opcode, branch and stack have a secondary opcode
        unsigned char opcode;
        unsigned char opcode2;
        opcode = highHalfByte(instruction[0]);
        if(opcode == 7){opcode2 = lowHalfByte(instruction[0]);}
        else if(opcode == 10) {opcode2 = instruction[1] >> 6;}


        int OP1;
        int OP2;
        //load contents into OP1 & OP2 if necessary
        if(opcode >= 1 && opcode <= 6) {
            //3R instruction
            OP1 = registers[get3R1(instruction)];
            OP2 = registers[get3R2(instruction)];
        }
        else if(opcode == 7 && opcode2 >= 0 && opcode2 <= 5) {
            OP1 = registers[getBR1(instruction)];
            OP2 = registers[getBR2(instruction)];
        }
        else if(opcode == 8 || opcode == 9) {
            OP1 = registers[highHalfByte(instruction[1])]; //address register
        }

        //fill out one of the two buffers which go to execute step as input. Also includes double buffered OP1 & OP2
        if(executeBuff1Ready) {
            executeBuff1Ready = 0;//mute
            OP1_1 = OP1;
            OP1_2 = OP2;
            for (int i = 0; i < 4; i++) {
                executeInstructionBuffer1[i] = instruction[i];
            }
            executeBuff1Ready = 1;//unmute
        }
        else if(executeBuff2Ready) {
            executeBuff2Ready = 0;//mute
            OP2_1 = OP1;
            OP2_2 = OP2;
            for (int i = 0; i < 4; i++) {
                executeInstructionBuffer2[i] = instruction[i];
            }
            executeBuff2Ready = 1;//unmute
        }

        executeInstructionValid = 1;
    }

}


//execute function - considers the decoded opcodes to determine
//control flow, executing the instructed operation in a series
//of nested switches.
void executeInstruction() {
    //printf("DEBUG: begin execute...\n");

    if(executeInstructionValid) {
        //prepare local variables with data from mutable double buffers outputted by decode function
        unsigned char instruction[4];
        unsigned char opcode, opcode2;
        int OP1, OP2, result;
        if(executeBuff1Ready) {
            executeBuff1Ready = 0; //mute buffer
            OP1 = OP1_1;
            OP2 = OP1_2;
            for (int i = 0; i < 4; i++) {
                instruction[i] = executeInstructionBuffer1[i];
            }
            executeBuff1Ready = 1; //unmute buffer
        }
        else if(executeBuff2Ready) { 
            executeBuff2Ready = 0; //mute buffer
            OP1 = OP2_1;
            OP2 = OP2_2;
            for (int i = 0; i < 4; i++) {
                instruction[i] = executeInstructionBuffer2[i];
            }
            executeBuff2Ready = 1; //unmute buffer
        }

        //all instrucitons begin with a 4-bit opcode, branch and stack have 2 opcodes
        opcode = highHalfByte(instruction[0]);
        if(opcode == 7){opcode2 = lowHalfByte(instruction[0]);}
        else if(opcode == 10) {opcode2 = instruction[1] >> 6;}

        //big outer opcode switch, considers the primary opcode for instruciton type
        switch(opcode)
        {
            //register forwarding - check register history, overwrite current values with most recent values if they are present
            OP1 = historyCheck(get3R1(instruction), OP1);
            OP2 = historyCheck(get3R2(instruction), OP2);
            int reg; //needed later during case blocks

            //3R instructions - these simply preform the operation on two registers, and store the result for later
            case 1://add
                result = OP1 + OP2;
                break;
            
            case 2://and
                result = OP1 & OP1;
                break;

            case 3://divide
                result = OP1 / OP1;
                break;

            case 4://multiply
                result = OP1 * OP1;
                break;

            case 5://subtract
                result = OP1 - OP1;
                break;

            case 6: //or
                result = OP1 | OP1;
                break;

            //branch instructions
            case 7:
            //register forwarding - check register history, overwrite current values with most recent values if they are present
            OP1 = historyCheck(getBR1(instruction), OP1);
            OP2 = historyCheck(getBR2(instruction), OP2);

                //inner switch for the bracnhes as well as jump and call denoted by secondary opcode
                switch(opcode2)
                {
                    case 0://branchifless
                        //if first register contents < second register contents
                        if(OP1 < OP2) {
                            //result = reconstructed address offset from instruction octets 2 and 3
                            result = (instruction[2] << 8) | instruction[3];
                        }
                        //if the test fails, no branch. set result to -1
                        else result = -1;
                        //Each of the following conditional branches follows this same pattern.
                        break;

                    case 1://branchiflessorequal
                        if(OP1 <= OP2) {
                            result = (instruction[2] << 8) | instruction[3];
                        }
                        else result = -1;
                        break;
                    
                    case 2://branchifequal
                        if(OP1 == OP2) {
                            result = (instruction[2] << 8) | instruction[3];
                        }
                        else result = -1;
                        break;
                    
                    case 3://branchfnotequal
                        if(OP1 != OP2) {
                            result = (instruction[2] << 8) | instruction[3];
                        }
                        else result = -1;
                        break;

                    case 4://branchifgreater
                        if(OP1 > OP2) {
                            result = (instruction[2] << 8) | instruction[3];
                        }
                        else result = -1;
                        break;
                    
                    case 5://branchifgreaterorequal
                        if(OP1 >= OP2) {
                            result = (instruction[2] << 8) | instruction[3];
                        }
                        else result = -1;
                        break;

                    //call and jump both reconstruct an address from instruction octets 1-3
                    case 6://call
                        result = (instruction[1] << 16) | (instruction[2] << 8) | (instruction[3]);
                        break;

                    case 7://jump
                        result = (instruction[1] << 16) | (instruction[2] << 8) | (instruction[3]);
                        break;
                }
                break;

            //load/store instructions
            unsigned int loc;
            case 8://load
                //loc = address held in specified register + offset
                reg = highHalfByte(instruction[1]);
                loc = registers[reg];
                loc = historyCheck(reg, loc);
                loc += lowHalfByte(instruction[1]);
                //result = the 4 bytes found in memory at loc
                result = (virtualMemory[loc] << 24) | (virtualMemory[loc + 1] << 16) | (virtualMemory[loc + 2] << 8) | (virtualMemory[loc +3]);
                break;

            case 9://store
                //result = address held in specified register + offset, location to store data in next step
                reg = highHalfByte(instruction[1]);
                result = registers[reg];
                result = historyCheck(reg, result);
                result += lowHalfByte(instruction[1]);
                break;

            //stack instructions
            case 10:
                //inner switch for push, pop, and return specified by secondary opcode
                switch(opcode2) 
                {
                    int reg;
                    case 0://return
                        //location = stack pointer
                        loc = registers[15];
                        loc = historyCheck(15, loc);
                        //result = 4 bytes found in virtual memory at loc - address to return to
                        result = (virtualMemory[loc] << 24) | (virtualMemory[loc + 1] << 16) | (virtualMemory[loc + 2] << 8) | (virtualMemory[loc +3]);
                        break;

                    case 1://push
                        //result = stack pointer, location to push data for use in store step
                        reg = getStackRegister(instruction);
                        result = registers[reg];
                        result = historyCheck(reg, result);
                        break;

                    case 2://pop
                        //loc = stack pointer
                        loc = registers[15];
                        loc = historyCheck(15, loc);
                        //result = 4 bytes found in memory at loc - data to pop off stack
                        result = (virtualMemory[loc] << 24) | (virtualMemory[loc + 1] << 16) | (virtualMemory[loc + 2] << 8) | (virtualMemory[loc +3]);
                        break;

                }
                break;
                

            case 11://move
                //get the immedate value from move instrucitons, converted to signed
                result = getImmediate(instruction);
                break;
            
            case 12://interrupt
                if(instruction[1] == 0) {//interrupt 0 instructs VM to print registers, 16 in total
                    printf("Register contents: \n");
                    for(int i = 0; i < 16; i++) {
                        int reg = i;
                        int value = registers[i];
                        value = historyCheck(reg, value);//use historic data if present
                        printf("Reg[%-2d]: %d\n", i, value);
                    }
                }
                else if(instruction[1] == 1) {//interrupt 1 instructs VM to print memory contents, 1KB in total arrayed 20 bytes per line
                    printf("Memory contents: \n=================================================================\n");
                    for(int i = 0; i < 1000; i++){
                        if(i % 20 == 0 && i != 0) //every 20 bytes print line number (bytes) and newline
                            printf(" %0 4d\n", (i-20));
                        printf("%02X ", virtualMemory[i]);
                    }
                    printf(" %0 4d\n=================================================================\n", 980);
                }
                break;

            case 0://halt - stops main execution loop upon next loop start
                halt = 1;
                break;

        }//end of the big outer opcode switch

        //fill in double buffer for store
        if(storeBuff1Ready) {
            storeBuff1Ready = 0;//mute
            result1 = result;
            for (int i = 0; i < 4; i++) {
                storeInstructionBuffer1[i] = instruction[i];
            }

            storeBuff1Ready = 1;//unmute
        }
        else if(storeBuff2Ready) {
            storeBuff2Ready = 0;//mute
            result2 = result;
            for (int i = 0; i < 4; i++) {
                storeInstructionBuffer2[i] = instruction[i];
            }
            storeBuff2Ready = 1;//unmute
        }

        storeInstructionValid = 1;
    }

}


//store function - takes results from execute and stores them in memory or registers as instructed,
//then updates program counter as needed.
void storeResult() {
    //printf("DEBUG: begin store...\n");

    if(storeInstructionValid) {
        //prepare local variables with data from mutable double buffers outputted by execute function
        unsigned char instruction[4];
        unsigned char opcode, opcode2;
        int result;
        if(executeBuff1Ready) {
            executeBuff1Ready = 0; //mute buffer
            result = result1;
            for (int i = 0; i < 4; i++) {
                instruction[i] = executeInstructionBuffer1[i];
            }
            executeBuff1Ready = 1; //unmute buffer
        }
        else if(executeBuff2Ready) { 
            executeBuff2Ready = 0; //mute buffer
            result = result2;
            for (int i = 0; i < 4; i++) {
                instruction[i] = executeInstructionBuffer2[i];
            }
            executeBuff2Ready = 1; //unmute buffer
        }

        //all instrucitons begin with a 4-bit opcode, branch and stack have 2 opcodes
        opcode = highHalfByte(instruction[0]);
        if(opcode == 7){opcode2 = lowHalfByte(instruction[0]);}
        else if(opcode == 10) {opcode2 = instruction[1] >> 6;}


        //3R instructions OPCODE 1-6
        if(opcode >= 1 && opcode <= 6) {
            int reg = get3R3(instruction);
            registers[reg] = result;
            historyLog(reg, result);
            PC += 2;
        }
        //Branch Instructions OPCODE 7 - 4-byte instructions
        else if(opcode == 7) {
            //call branch type 6
            if(opcode2 == 6) {
                PC = result;
                invalidatePipeline();//when branch is taken, sequential instructions in pipeline become invalid
            }
            //jump branch type 7
            else if(opcode2 == 7) {
                invalidatePipeline();
                PC = result;
            }
            //else conditional branches, branch types 0-5
            else {
                //condition for branching not met
                if(result == -1) {
                    PC += 4;
                }
                //condition for branching met
                else {
                    invalidatePipeline();
                    PC += result;
                }
            }
        }

        //load OPCODE 8
        else if(opcode == 8) {
            //store result in specified register
            int reg = lowHalfByte(instruction[0]);
            registers[reg] = result;
            historyLog(reg, result); //log register change for later forwarding
            PC += 2;
        }
        //store OPCODE 9
        else if(opcode == 9) {
            //store data from specified 32-bit register into 4 bytes of virtual memory,
            //by splitting and shifting each octet.
            virtualMemory[result] = registers[lowHalfByte(instruction[0])] >> 24;
            virtualMemory[result + 1] = registers[lowHalfByte(instruction[0])] >> 16;
            virtualMemory[result + 2] = registers[lowHalfByte(instruction[0])] >> 8;
            virtualMemory[result + 3] = registers[lowHalfByte(instruction[0])];
            PC += 2;
        }

        //stack instructions OPCODE 10
        else if(opcode == 10) {
            switch(opcode2) {
                int reg;
                //return
                case 0:
                    //update the stack pointer, down 4 bytes as data was popped off in execute step
                    moveStackPointer(4);
                    //update program counter to new instruciton location for next fetch
                    PC = result;
                    break;

                //push
                case 1:
                    //update stack pointer, up 4 butes as we push data onto stack
                    moveStackPointer(-4);
                    //store data from execute result in virtual memory at stack pointer by shifting and storing each octet
                    virtualMemory[registers[15]] = result >> 24;
                    virtualMemory[registers[15] + 1] = result >> 16;
                    virtualMemory[registers[15] + 2] = result >> 8;
                    virtualMemory[registers[15] + 3] = result;
                    PC += 2;
                    break;

                //pop
                case 2:
                    //store execute result in specified register
                    reg = getStackRegister(instruction);
                    registers[reg] = result;
                    historyLog(reg, result);
                    //move stack pointer down 4 bytes as we popped off data
                    moveStackPointer(4);
                    PC += 2;
                    break;
            }
        }

        //move
        else if(opcode == 11) {
            //store result from execute in specified register
            int reg = getMoveRegister(instruction);
            registers[reg] = result;
            historyLog(reg, result);
            PC += 2;
        }

        //interrupt
        else if(opcode == 12) {
            //only need to advance program counter here.
            PC += 2;
        }

        //halt
        else if(opcode == 0) {
            //not actually necessary to advance PC, halts execution before next fetch
            PC += 2;
        }
    }
}

//////////////////////////
// Main - Program Entry //
//////////////////////////

int main (int argc, char **argv)  {
    //prepare for execution: set halt flag off, program counter to 0, resultHistoryCursor to 0, and stack pointer to bottom of stack
    halt = 0;
    PC = 0;
    resultHistoryCursor = 0;
    registers[15] = 1000;

    //start with mutable buffers unmuted
    decodeBuff1Ready = 1;
    decodeBuff2Ready = 1;
    executeBuff1Ready = 1;
    executeBuff2Ready = 1;
    storeBuff1Ready = 1;
    storeBuff2Ready = 1;

    //start with all instruction buffers invalid
    //these are validated when a step outputs data for the next step, but invalidated
    //whenever the program counter is moved with branch, call, jump, or return.
    decodeInstructionValid = 0;
    executeInstructionValid = 0;
    storeInstructionValid = 0;
    

    //make sure proper # of arguments given, otherwise output hint.
    if (argc != 2) {
        printf ("Bad Args. Hint: siavm.exe file.bin\n"); 
        exit(1);
    }
        
    //load file with instructions to execute
    loadFile(argv[1]);

    while(!halt) {
        //The main execution loop, continues to run until a halt instruction in executed.
        //Fetch -> decode -> execute -> store -> repeat...halt
        //These can now be executed in any order, and as long as all 4 execute before cycling
        //the pipeline features should keep everything valid.

        executeInstruction();
        storeResult();
        fetchInstruction();
        decodeInstruction();
    }

    return 0;
}