# Makefile - Danning Yu, 305087992
# CS 118 Project 2

CC = g++
CCOMPILER = gcc
ADDRSANITIZER = -fsanitize=address -fsanitize=undefined -static-libubsan -static-libasan
COMPILEROPTIONS = -Wall -pedantic -Wextra -g
CLIENTFILE = client.cpp
SERVERFILE = server.cpp


.PHONY: client server client-dev server-dev

default: client server

client:
	$(CC) -O3 -o client $(CLIENTFILE)

server:
	$(CC) -O3 -o server $(SERVERFILE)

dev: client-dev server-dev

client-dev:
	$(CC) $(ADDRSANITIZER) $(COMPILEROPTIONS) -o client-dev $(CLIENTFILE)

server-dev:
	$(CC) $(ADDRSANITIZER) $(COMPILEROPTIONS) -o server-dev $(SERVERFILE)

dist:
	tar -czvf 305087992.tar.gz utility.h $(CLIENTFILE) $(SERVERFILE) Makefile README

c-check:
	$(CCOMPILER) $(ADDRSANITIZER) $(COMPILEROPTIONS) -o client-c $(CLIENTFILE)
	$(CCOMPILER) $(ADDRSANITIZER) $(COMPILEROPTIONS) -o server-c $(SERVERFILE)

check: clean default
	pkill server || true
	cat /dev/urandom | head -c 100 > file100
	./server 5000 & ./client localhost 5000 file100 > output_file
	./test_format.py output_file
	# if exit code is nonzero, make will automatically stop and report failure
	diff file100 1.file	
	pkill server || true
	rm -f 1.file file100

	# sleep 1 s to allow for previous server's port to be freed
	sleep 1
	./server 5000 & ./client localhost 5000 smallfile.txt > output_file
	diff 1.file smallfile.txt
	./test_format.py output_file
	./client localhost 5000 4669bytes.txt > output_file
	diff 2.file 4669bytes.txt
	./test_format.py output_file
	./client localhost 5000 5120bytes.txt > output_file
	diff 3.file 5120bytes.txt
	./test_format.py output_file
	./client localhost 5000 5230bytes.txt > output_file
	diff 4.file 5230bytes.txt
	./test_format.py output_file
	./client localhost 5000 bigfile.txt > output_file
	diff 5.file bigfile.txt
	./test_format.py output_file
	./client localhost 5000 1.5M.txt > output_file
	diff 6.file 1.5M.txt
	./test_format.py output_file
	./client localhost 5000 emptyfile.txt > output_file
	diff 7.file emptyfile.txt
	./test_format.py output_file
	
	pkill server || true
	rm -f *.file client server file100 output_file

	echo "Small file test passed for production version!"

check-dev: clean dev
	pkill server-dev || true
	cat /dev/urandom | head -c 100 > file100
	./server-dev 5000 & ./client-dev localhost 5000 file100 > output_file
	./test_format.py output_file
	# if exit code is nonzero, make will automatically stop and report failure
	diff file100 1.file	
	pkill server-dev || true
	rm -f 1.file file100

	# sleep 1 s to allow for previous server's port to be freed
	sleep 1
	./server-dev 5000 & ./client-dev localhost 5000 smallfile.txt > output_file
	diff 1.file smallfile.txt
	./test_format.py output_file
	./client-dev localhost 5000 4669bytes.txt > output_file
	diff 2.file 4669bytes.txt
	./test_format.py output_file
	./client-dev localhost 5000 5120bytes.txt > output_file
	diff 3.file 5120bytes.txt
	./test_format.py output_file
	./client-dev localhost 5000 5230bytes.txt > output_file
	diff 4.file 5230bytes.txt
	./test_format.py output_file
	./client-dev localhost 5000 bigfile.txt > output_file
	diff 5.file bigfile.txt
	./test_format.py output_file
	./client localhost 5000 1.5M.txt > output_file
	diff 6.file 1.5M.txt
	./test_format.py output_file
	./client-dev localhost 5000 emptyfile.txt > output_file
	diff 7.file emptyfile.txt
	./test_format.py output_file
	
	pkill server-dev || true
	rm -f *.file client-dev server-dev file100 output_file

	echo "Small file test passed!"

clean:
	rm -f 305087992.tar.gz client server client-dev server-dev *.file server-c client-c file100 output_file
