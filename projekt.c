#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  // Komendy Linuxowe; write, fork, etc.
#include <fcntl.h>  // Komenda open() i jej argumenty
#include <sys/stat.h>  // Komenda stat() do pobrania informacji o pliku/katalogu na danej ścieżce
#include <sys/sysmacros.h>
#include <time.h>

// Main
int main(int argc, char *argv[]) {
	// Program powinien był otrzymać ścieżkę źródłową i ścieżkę docelową. Jeśli nie otrzymał zwracamy błąd i kończymy program.
	if (argc < 2) {
		perror("Za mało argumentów!\n Format: ./program /ścieżka/do/źródła /ścieżka/do/celu (argumenty dodatkowe)");
		exit(EXIT_FAILURE);
	}


	// Tu zostaną zapisane informacje o danym pliku, otrzymane poprzez stat()
	struct stat statb;

	// Próba odczytania informacji o katalogu na ścieżce źródłowej. W przypadku niepowodzenia (stat zwraca '-1'), program kończy się.
	if (stat(argv[1], &statb) == -1) {
		perror("Błąd stat()!\n");
		exit(EXIT_FAILURE);
	}
	// Odczytanie informacji ze statbuf 'statb'; jeśli na podanej ścieżce źródłowej nie ma katalogu, program kończy się.
	if (!S_ISDIR(statb.st_mode)) {
		perror("Ścieżka źródłowa nie wskazuje na katalog!\n");
		exit(EXIT_FAILURE);
	}

	// Próba odczytania informacji o katalogu na ścieżce docelowej. W przypadku niepowodzenia (stat zwraca '-1'), program kończy się.
	if (stat(argv[2], &statb) == -1) {
		perror("Błąd stat()!\n");
		exit(EXIT_FAILURE);
	}
	// Odczytanie informacji ze statbuf 'statb'; jeśli na podanej ścieżce docelowej nie ma katalogu, program kończy się.
	if (!S_ISDIR(statb.st_mode)) {
		perror("Ścieżka docelowa nie wskazuje na katalog!\n");
		exit(EXIT_FAILURE);
	}


	// Podane argumenty są poprawne, więc zamieniamy programu w demona za pomocą Linuxowej funkcji deamon()
	// int status = deamon(0, 0);


	return 0;
}
