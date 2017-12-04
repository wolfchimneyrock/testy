LD_LIBRARY_PATH=/usr/local/lib gcc -O2 -I/usr/local/include -I/usr/local/include/evhtp *.c -pthread -ltcmalloc -levent -levhtp -lconfuse -o testy
