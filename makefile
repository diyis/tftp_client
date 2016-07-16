#Usar el buen compilador de GNU
CC=gcc

#Directorio para los ejecutables
EXE_DIR=./dist

#Directorio para los objetos ... aunque creo que no es necesario
#OBJ_DIR=./obj

tftp.o: tftp.h tftp.c
	$(CC) -o tftp.o -c tftp.c 

#Compilar el cliente y poner el resultado en EXE_DIR
#$(EXE_DIR)/client: tftp.h client.c
#	$(CC) -o $(EXE_DIR)/client tftp.h client.c

client: tftp.o main.c
	$(CC) -o client tftp.o main.c


#Compilar el main y poner el resultado en dist
#$(EXE_DIR)/main: main.c
#	$(CC) -o $(EXE_DIR)/main main.c


#Borrar los ejecutables (Borrará todo lo que esté dentro de la carpeta $(EXE_DIR)
#.PHONY: clean
#clean:
#	rm -vf $(EXE_DIR)/*
