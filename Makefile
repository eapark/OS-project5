CC = /usr/bin/gcc

virtmem: main.o page_table.o disk.o program.o
	$(CC) -g -gdwarf-2 main.o page_table.o disk.o program.o -o virtmem -lpthread

main.o: main.c
	$(CC) -Wall -g -c main.c -o main.o

page_table.o: page_table.c
	$(CC) -Wall -g -c page_table.c -o page_table.o

disk.o: disk.c
	$(CC) -Wall -g -c disk.c -o disk.o

program.o: program.c
	$(CC) -Wall -g -c program.c -o program.o


clean:
	rm -f *.o virtmem
