# SiaVM

A virtual machine to execute Simple Instruction Architecture instructions. Created for CSI-404 - Assembly Programming and Computer Organization

---

## Simple Instruction Architecture
SIA (Simple Instruction Architecture) is a minimal instruction set with 16 instructions, including those for arithmetic, stack management, register management, and control flow. See documentation for details. SIA handles up to 32-bit instructions, though most utilize only 16 bits. 

SiaVM is a program which takes input of SIA instructions in a binary file and executes until it reaches a halt. SIA machine code binaries can be created with the assembler program. Instructions can be written in English following a specific syntax. See documentation for info, and unit tests for examples. 

---

## Pipelining
SiaVM executes instrucitons in a fetch, decode, execute, and store loop. SiaVM pipelines instructions, that is, while an instruction is working it's way through the FDES process the following instructions are not waiting for completion. If an instruction in currently at the execution step, the following two instructions are already being fetched and executed. This is accomplished by double buffering registers between the steps and a history check to validate the pipeline during execution step.

---

## Output
Output should only be expected if an interrupt instruction is given. Interrupt 0 dumps the registers, interrupt 1 dumps memory. Output is sent to console.
