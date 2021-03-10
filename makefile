PROG = main

CC = gcc
FLAGS = -Wall -pthread


all: ${PROG}

clear: 
	rm ./${PROG}

$(PROG):	${PROG}.c ${PROG}.h
		${CC} ${FLAGS} ${PROG}.c -o ${PROG}


#Projeto SO
#--Eduardo Cruz
#--Gonçalo Barroso
