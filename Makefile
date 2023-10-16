CC=g++
CCFLAGS=-lcurses
CINC=-I inc/include -I inc/ncurses

BIN_DIR=bin
OBJ_DIR=obj

BIN_NAME=MultiShell

SOURCE_FILE=src/main.cpp

build: ${SOURCE_FILE} Makefile
	make dirs
	${CC} ${SOURCE_FILE} ${CCFLAGS} ${CINC} -o ${BIN_DIR}/${BIN_NAME}

dirs: Makefile
	mkdir -p ${BIN_DIR} ${OBJ_DIR}
