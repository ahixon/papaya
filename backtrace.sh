#!/bin/bash

#This script matches the program counter values returned by 
#backtrace() in sos to the function names. 
#the argument could be: -l -S for objdump extra options
#-w width for print out line width
#-f filename for get the input from the file

BACKTRACE_PC=""
LEVEL=0
OBJDUMP_OPTIONS="-d"
WIDTH=4
COMPILER_PREFIX=arm-none-linux-gnueabi-
SOS_BIN_PATH="build/arm/imx6/sos/sos.bin"

NM_EXE=nm
OBJDUMP_EXE=objdump
NM=$COMPILER_PREFIX$NM_EXE
OBJDUMP=$COMPILER_PREFIX$OBJDUMP_EXE

#help function for the backtrace_symbol script
print_help()
{
    echo "USAGE:    backtrace.sh [OPTIONS] <<EOF"
    echo "           <pc_addr0> <pc_addr1> <pc_addr2> <pc_addr3> ..."
    echo "           <<EOF"
    echo
    echo "OPTIONS:  -l printout line number for disassembly"
    echo "          -S printout source code for disassembly"
    echo "          -w WIDTH extra lines of disassembly for each side of the target PC, between 0 and 32 (default is 4)"
    echo "          --start-address=0xADDR only process data whose address is >= ADDR"
    echo "          --stop-address=0xADDR  only process data whose address is <= ADDR"
    echo "          --help printout this message"
    echo
    echo "Format:   The PCs(Program Counter Values) should be space or newline separated"
    echo "          hex numbers, starting with \"0x\""
    exit 0
    return
}


#process options for the script
process_options()
{
    #no options to process, return
    if [ $# -eq 0 ]
    then
        return
    fi

    #if the argument list is not empty
    while [ -n "$1" ]
    do 
        if [ "$1" = "-l" ]
        then
               #add -l to the objdump options
            OBJDUMP_OPTIONS="$OBJDUMP_OPTIONS -l"
            
        elif [ "$1" = "-S" ] 
        then            
        #add -S to the objdump options
            OBJDUMP_OPTIONS="$OBJDUMP_OPTIONS -S"

        elif [ "$1" = "-w" ]
        then
        #resolve the line number surround each pc,[0-10]
            if [ $2 -lt 33 ] && [ $2 -gt -1 ]; then
                WIDTH=$2
                shift
            else
            #call the help function
                print_help
            fi            
                        
        elif [ `expr "$1" : '--start-address=0x'` -eq 18 ]
        then 
           #passing start address address to the objdump
            OBJDUMP_OPTIONS="$OBJDUMP_OPTIONS $1"

        elif [ `expr "$1" : '--stop-address=0x'` -eq 17 ]
        then 
           #passing stop address to the objdump
            OBJDUMP_OPTIONS="$OBJDUMP_OPTIONS $1"

        elif [ "$1" = "--help" ]
        then
            print_help

        else
            #wrong option, printout help message
            print_help
        fi
        #shift the arguement right to one position
        shift

    done

    return 
}

#process stdin
process_stdin()
{
    #read in the line if there is one available
    while read line
    do
        # split line into variables.
        for var1 in $line
        do
            if [ ${#var1} -le 10 ] && [ `expr "$var1" : '0x'` -eq 2 ] && \
               [[ "$var1" -gt "0x0" ]] && [[ "$var1" -lt "0xffffffff" ]]
            then
                #record the backtrace pc if it is in the right range 0-0xffffffff
                #the string is less than 10, start with "0x"
                BACKTRACE_PC="$BACKTRACE_PC $var1"
            else
                #wrong input, print out help function
                echo 'ERROR - Invalid input.'
                print_help
                return
            fi
        done
    done
    return
}



#get the input from stdin, call function 
process_options $@
process_stdin


#if the backtrace_pc list is null, print out the help function and exit
if [ "$BACKTRACE_PC" = "" ]
then 
    echo "ERROR: empty PC list"
    print_help
fi

#list the symbol in increasing order
$NM -n $SOS_BIN_PATH > sos.nm
if [ $? -ne 0 ]
then
    print_help
fi

$OBJDUMP $OBJDUMP_OPTIONS $SOS_BIN_PATH > sos.dis
if [ $? -ne 0 ]
then
    print_help
fi


#for each address in the back trace pc list, do the search
for ADDRESS in $BACKTRACE_PC
do 
    echo "--------------------------------------------------------------------------------"
    #search the function name with a given PC
    awk -v searchadd=$ADDRESS -v le=$LEVEL --non-decimal-data \
        ' BEGIN {
           target = searchadd + 0   #numeric the searchadd
           resolve = 0
           }
         { 
          #the first string in each line is an 8 digits hex number
          if (length($1) != 8)
              next

          a1 = ("0x" $1) + 0  #numeric the string
          if (a1 < target) {
              pervious_name = $3
              pervious_add = a1 
          } else if (a1 == target) {
              #printout the name in this line
              #target find at the start of the function
              resolve = 1
              printf "    #%2d  0x%08x in %-30s\n", le, target, $3
              exit 1
          } else { 
               #printout the pervious recorded name
              resolve = 1
              printf "    #%2d  0x%08x in  %-30s\n",le,target, pervious_name
              exit 1
                }}
          END { if (resolve == 0) {
                    #the address can not be resolved
                    printf "    #%2d: can not resolve 0x%08x\n",le,target 
                    exit 2 }} ' sos.nm

    #printout the relative disassembled code if the pc is resolved
    EXIT_STATUS=$?
    if [ $EXIT_STATUS -eq 1 ]; then
        echo ""
        awk -v searchadd=$ADDRESS -v width=$WIDTH --non-decimal-data \
            ' BEGIN {
               add =  searchadd + 0     #numeric the search address
               start = add - width * 4
               end = add + width * 4
               flag = 0
              }
              {
              #for a correct disassembly line, the second string should be the machine code
              if (length($2) == 8 && $2 ~ /^[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]/) {

                    #the first line should be the address of the pc, in the form hex:
                    if (length($1) > 9)
                       next

                    a1 = ("0x" $1) + 0 #numeric the pc string
                    if (a1 == end) {
                        print $0
                        exit   #print finish
                     }

                     if (a1 == start) {
                        flag = 1   #set the print flag
                      }
                } 
                #print out the line if the flag is set    
                if (flag == 1) {
                   print $0
                }
              }' sos.dis
    fi

    #increase the stack frame number by one
    LEVEL=`expr $LEVEL + 1`
done

#remove the temp files
rm sos.nm
rm sos.dis
