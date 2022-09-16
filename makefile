test:
	gcc -pthread -o farm farm.c
	gcc -pthread -o generafile generafile.c
	chmod +x test.sh
	./test.sh