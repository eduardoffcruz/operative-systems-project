PROG = main

CC = gcc
FLAGS = -Wall -pthread
#-lpthread -D_REENTRANT


all: ${PROG}

clear: 
	rm ${PROG}

$(PROG):	${PROG}.c ${PROG}.h
		${CC} ${FLAGS} ${PROG}.c -o ${PROG}


#_______________Projeto Sistemas Operativos @2021
#Eduardo F. Ferreira Cruz 2018285164
#Gonçalo Marinho Barroso 2019216314
