# Makefile - Danning Yu, 305087992
# CS 118 Project 1 - Web Server

CC = gcc
ADDRSANITIZER = -fsanitize=address -fsanitize=undefined -static-libubsan -static-libasan
COMPILEROPTIONS = -Wall -pedantic -Wextra
CHECKOUTPUT = index2.html index3.html sfrobu2.txt ucla2.jpg lab2b2.png downloadfile malloc2.html badExtension

default:
	$(CC) -o webserver lab1.c

dev:
	$(CC) $(ADDRSANITIZER) $(COMPILEROPTIONS) -g -o webserver lab1.c

dist:
	tar -czvf 305087992.tar.gz lab1.c Makefile README

check: dev
	pkill webserver || true
	./webserver & curl -o index2.html localhost:5000
	diff index.html index2.html
	
	curl -o index3.html localhost:5000/index.html
	diff index.html index3.html

	curl -o sfrobu2.txt localhost:5000/sfrobu.txt
	diff sfrobu.txt sfrobu2.txt

	curl -o malloc2.html localhost:5000/malloc.html
	diff malloc2.html malloc.html

	curl -o ucla2.jpg localhost:5000/UCLA.jpg
	diff ucla2.jpg UCLA.jpg

	curl -o lab2b2.png localhost:5000/lab2b_1.png
	diff lab2b2.png lab2b_1.png

	curl -o lab2b2.png localhost:5000/lab2b_1.html.png
	diff lab2b2.png lab2b_1.html.png

	curl -o index3.html localhost:5000/index.jpg.html
	diff index3.html index.jpg.html
	
	cat /dev/urandom | head -c 1500000 > binaryfile
	curl -o downloadfile localhost:5000/binaryfile
	diff downloadfile binaryfile

	curl -o badExtension localhost:5000/indexDownload.abcd
	diff badExtension indexDownload.abcd

	rm -f $(CHECKOUTPUT)
	pkill webserver
	echo "Automated tests passed!"

clean:
	rm -f webserver 305087992.tar.gz $(CHECKOUTPUT)
