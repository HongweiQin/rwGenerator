all:
	gcc -o f2fsAtomicWrite f2fsAtomicWrite.c -lpthread -lrt -laio
	gcc -o f2fsAtomicPwrite f2fsAtomicPwrite.c -lpthread -lrt -laio

f2fsAtomicWrite:f2fsAtomicWrite.c
	gcc -o f2fsAtomicWrite f2fsAtomicWrite.c -lpthread -lrt -laio

f2fsAtomicPwrite:f2fsAtomicPwrite.c
	gcc -o f2fsAtomicPwrite f2fsAtomicPwrite.c -lpthread -lrt -laio
