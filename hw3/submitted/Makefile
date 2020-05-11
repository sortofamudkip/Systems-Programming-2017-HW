all:
	gcc server.c -o server
	gcc file_reader.c -o file_reader
	gcc slow_file_reader.c -o slow_file_reader
	gcc very_slow_reader.c -o very_slow_reader

clean:
	rm -f very_slow_reader
	rm -f slow_file_reader
	rm -f file_reader
	rm -f server
