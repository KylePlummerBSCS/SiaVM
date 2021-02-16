/* Kyle Plummer - CSI404 - Assembler Assignment - 2020-03-07
 * Program takes a text file with SIA instructions and outputs a binary file
 * of translated SIA machine code. Peruse code in HEX with:
 * od –x --endian=big [file] | head -5
 */



#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

char *words[5];
int wordsSize;//numWords used to track number of words in words[] (to aid wordsToLower)


/* Take a string. Split it into different words, putting them in the words array. For example:
 * This is a string
 * Becomes:
 * words[0] = This
 * words[1] = is
 * words[2] = a
 * words[3] = string
 * Only safe when words is big enough...
 */
//Note: getWords has two additional lines, one to reset wordsSize and another to increment it -Kyle
void getWords(char *string) { 
    printf ("\ninput: %s",string);
    int curWord = 0;
    wordsSize = 1;
    char *cur = string;
    words[curWord] = string;
    while (*cur != 0) {
        if (*cur == '\n' || *cur == '\r') *cur = ' ';
        if (*cur == ' ') {
            *cur = 0; // replace space with NULL
            curWord++;
            words[curWord] = cur+1; // set the start of the next word to the character after this one
            wordsSize++;
        } cur++; } for (int i=0;i<curWord;i++) 
        printf ("word %d = %s\n",i,words[i]);
}

// takes a string and returns register number or -1 if the string doesn't start with "r" or "R"
int getRegister (char* string) {
    if (string[0] != 'R' && string[0] != 'r') return -1;
    return atoi(string+1);
}

/* byteMe - Creates a new byte(char) from the low 4 bits of two ints
 * Places the low 4 bits of the first int into the high 4 bits of the 
 * resulting byte. Places the low 4 bits of the second int into the 
 * low 4 bits of the resulting byte.
 */
char byteMe(int high, int low) {
    return ((high << 4) | (low & 15)); 
}

//highByte - Returns the high 8 bits out of 16
char highByte(int num) {
    return num >> 8;
}

//lowByte - Returns the low 8 bits out of 16
char lowByte(int num) {
    return (num & 255);
}

/* wordsToLower - converts the contents of global words[] to lower case
 * I noticed I left a number of capitalized words in my unit tests
 * and figured I could help make up for it by using the std lib funciton tolower
 * on each character of the input strings stored in words[].
 * Required a global to go with words[] tracking its size.
 */
void wordsToLower() {
    //iterate through words[] array
    for (int i = 0; i < wordsSize; i++) {
        int j = 0;
        char *c = words[i];
        //iterate through char string loooking for teminating 0
        while (*c != 0){
            //invoke tolower() on character
            *c = tolower(*c);
            j++;
            c = words[i]+j;
        }
    }
}

/* Fills out the bytes array for all 3R instructions.
 * Takes an opcode and the bytes array, fills in the array and returns 2,
 * the length of 3R instructions in bytes.
 */
int translate3R(int opcode, char *bytes) {
    bytes[0] = byteMe(opcode, getRegister(words[1]));
    bytes[1] = byteMe(getRegister(words[2]), getRegister(words[3]));
    return 2;
}

/* Fills out the bytes array for all BR1 instructions.
 * Takes an int indicating branch type and the bytes array, fills in the array
 * and returns 4, the length of BR1 instructions in bytes.
 */
int translateBR1(int branchtype, char *bytes) {
    bytes[0] = byteMe(7, branchtype);
    bytes[1] = byteMe(getRegister(words[1]), getRegister(words[2]));
    int offset = (atoi(words[3]) / 2);
    bytes[2] = highByte(offset);
    bytes[3] = lowByte(offset);
    return 4;
}

/* Fills out the bytes array for all BR2 instructions.
 * Takes an int indicating call or jump, and the bytes array.
 * Fills in the array and returns 4, the length of BR2 instructions in bytes
 */
int translateBR2(int branchtype, char *bytes) {
    bytes[0] = byteMe(7, branchtype);
    int address = (atoi(words[1]) / 2);
    bytes[1] = address >> 16;
    bytes[2] = address >> 8;
    bytes[3] = address;
    return 4;
}

/* Fills out the bytes array for load/store instructions
 * Takes an opcode and the bytes array. fills in the array and returns 2,
 * the length of LS instructions in bytes
 */
int translateLS(int opcode, char *bytes) {
    bytes[0] = byteMe(opcode, getRegister(words[1]));
    bytes[1] = byteMe(getRegister(words[2]), (atoi(words[3])));
    return 2;
}


/* Fills out the bytes array for stack instructions
 * Takes an int indicating the type of instruciton as well as the bytes array
 * Fills in the array and returns 2, the length of the instructions in bytes.
 */
int translateStack(int type, char *bytes) {
    bytes[0] = byteMe(10, getRegister(words[1]));
    bytes[1] = (type << 6) & 192;
    return 2;
}

/* Fills out the bytes array for move instrucitons.
 * Takes an opcode and the bytes array, fills in the array and
 * returns 2, the length of the instruciton in bytes
 */
int translateMove(int opcode, char *bytes) {
    //move instructions out of order?
    //Example: move -127 r1; sets register R1 to -127
    //| opcode | register | 8 bit imm value |
    //|words[0]| words[2] |    words[1]     |
    bytes[0] = byteMe(opcode, getRegister(words[2]));
    bytes[1] = atoi(words[1]);
    return 2;
}

// Figure out from the first word which operation we are doing and do it...
/* Parameter 0: char *string - string of chars representing 1 line from input file
 * Parameter 1: char *bytes - array of bites to hold assembly output
 * Returns: int - length of assembled instruction in bytes
 */
int assembleLine(char *string, char *bytes) {
    //tokenize input into words
    getWords(string);

    //convert all characters in words to lowercase
    wordsToLower();

    /* What follows is a long if/else-if chain testing the first input word
     * to see which instruction to translate. Each block calls the translator 
     * funciton associated with that instruciton type. Halt and interrupt are 
     * treated differently as they eschew some input.
     */
    //3R Instructions
    //add - opcode 1
    if (strcmp(words[0] ,"add") == 0) {
        return translate3R(1, bytes);
    }

    //and - opcode 2
    else if (strcmp(words[0] ,"and") == 0) {
        return translate3R(2, bytes);
    }

    //divide - opcode 3
    else if (strcmp(words[0] ,"divide") == 0) {
        return translate3R(3, bytes);
    }

    //multiply - opcode 4
    else if (strcmp(words[0] ,"multiply") == 0) {
        return translate3R(4, bytes);
    }

    //subtract - opcode 5
    else if (strcmp(words[0] ,"subtract") == 0) {
        return translate3R(5, bytes);
    }

    //or - opcode 6
    else if (strcmp(words[0] ,"or") == 0) {
        return translate3R(6, bytes);
    }

    //BR1 - relative branch instrucitons - opcode 7
    //branchifless
    else if (strcmp(words[0] ,"branchifless") == 0) {
        return translateBR1(0, bytes);
    }

    //branchiflessorequal
    else if (strcmp(words[0] ,"branchiflessorequal") == 0) {
        return translateBR1(1, bytes);
    }

    //branchifequal
    else if (strcmp(words[0] ,"branchifequal") == 0) {
        return translateBR1(2, bytes);
    }

    //branchifnotequal
    else if (strcmp(words[0] ,"branchifnotequal") == 0) {
        return translateBR1(3, bytes);
    }

    //branchifgreater
    else if (strcmp(words[0] ,"branchifgreater") == 0) {
        return translateBR1(4, bytes);
    }

    //branchifgreaterorequal
    else if (strcmp(words[0] ,"branchifgreaterorequal") == 0) {
        return translateBR1(5, bytes);
    }

    //BR2 absolute branch instructions - opcode 7
    //call
    else if (strcmp(words[0] ,"call") == 0) {
        return translateBR2(6, bytes);
    }

    //jump
    else if (strcmp(words[0] ,"jump") == 0) {
        return translateBR2(7, bytes);
    }

    //LS instruciton
    //load - opcode 8
    else if (strcmp(words[0] ,"load") == 0) {
        return translateLS(8, bytes);
    }

    //store - opcode 9
    else if (strcmp(words[0] ,"store") == 0) {
        return translateLS(9, bytes);
    }

    //Stack instructions - opcode 10
    //return
    else if (strcmp(words[0] ,"return") == 0) {
        return translateStack(0, bytes);
    }

    //push
    else if (strcmp(words[0] ,"push") == 0) {
        return translateStack(1, bytes);
    }

    //pop
    else if (strcmp(words[0] ,"pop") == 0) {
        return translateStack(2, bytes);
    }

    //Move instructions
    //move - opcode 11
    else if (strcmp(words[0] ,"move") == 0) {
        return translateMove(11, bytes);
    }


    //Halt and Interrupt are distinct from the other instructions as they ignore some input.
    //HALT opcode: 0, type: 3R
    else if (strcmp(words[0] ,"halt") == 0) {
        bytes[0] = 0;
        bytes[1] = 0;
        return 2;
    }

    //Interrupt opcode: 12, type: move
    else if (strcmp(words[0] ,"interrupt") == 0) {
        bytes[0] = byteMe(12, 0);
        bytes[1] = atoi(words[1]);
        return 2;
    }

    //output error if instruction doesn't match any of the above options, though it might
    //be more prudent to wipe the output file instead if the resulting machine code is bad
    else {
        printf("Error: Bad instruction!\nInstruction: %s.\n", words[0]);
    }
}




int main (int argc, char **argv)  {
    if (argc != 3)  {printf ("assemble inputFile outputFile\n"); exit(1); }
    FILE *in = fopen(argv[1],"r");
    if (in == NULL) { printf ("unable to open input file\n"); exit(1); }
    FILE *out = fopen(argv[2],"wb");
    if (out == NULL) { printf ("unable to open output file\n"); exit(1); }

    char bytes[4], inputLine[100];
    while (!feof(in)) {
        if (NULL != fgets(inputLine,100,in)) {
            int outSize = assembleLine(inputLine,bytes);
            fwrite(bytes,outSize,1,out);
        }
    }
    fclose(in);
    fclose(out);
}