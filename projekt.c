#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  // Komendy Linuxowe; write, fork, etc.
#include <fcntl.h>  // Komenda open() i jej argumenty
#include <sys/stat.h>  // Komenda stat() do pobrania informacji o pliku/katalogu na danej ścieżce
#include <sys/sysmacros.h>
#include <time.h>

void scan_directory(const char *source_path, const char *target_path, int recursive, size_t threshold) {
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    char full_src_path[PATH_MAX];
    char full_dst_path[PATH_MAX];

    dir = opendir(source_path);
    if (!dir) {
        syslog(LOG_ERR, "Nie można otworzyć katalogu źródłowego %s: %m", source_path);
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        snprintf(full_src_path, sizeof(full_src_path), "%s/%s", source_path, entry->d_name);
        snprintf(full_dst_path, sizeof(full_dst_path), "%s/%s", target_path, entry->d_name);

        if (lstat(full_src_path, &statbuf) == -1) continue;

        if (S_ISREG(statbuf.st_mode)) {
            struct stat target_stat;
            int needs_sync = 0;

            if (lstat(full_dst_path, &target_stat) == -1) {
                if (errno == ENOENT) {
                    syslog(LOG_INFO, "Wykryto nowy plik: %s", entry->d_name);
                    needs_sync = 1;
                }
            } else {
                // Porównanie dat modyfikacji - kluczowe dla wydajności demona
                if (statbuf.st_mtime > target_stat.st_mtime) {
                    syslog(LOG_INFO, "Plik zmodyfikowany: %s", entry->d_name);
                    needs_sync = 1;
                }
            }

            if (needs_sync) {
                /*
                metoda kopiowania zalezna od pliku osoba 4 issuse
                */
                if (statbuf.st_size < (off_t)threshold) {
                    syslog(LOG_INFO, "Kopiowanie (read/write): %s", entry->d_name);
                    // copy_read_write(full_src_path, full_dst_path);
                } else {
                    syslog(LOG_INFO, "Kopiowanie (mmap): %s", entry->d_name);
                    // copy_mmap(full_src_path, full_dst_path);
                }
                
                
                struct utimbuf time_data;
                time_data.actime = statbuf.st_atime;  // zachowaj czas dostępu
                time_data.modtime = statbuf.st_mtime; // ustaw czas modyfikacji na taki sam jak w źródle
                if (utime(full_dst_path, &time_data) == -1) {
                    syslog(LOG_ERR, "Błąd ustawiania czasu dla %s: %m", full_dst_path);
                }
            }
        } 
        else if (S_ISDIR(statbuf.st_mode) && recursive) {
            if (mkdir(full_dst_path, statbuf.st_mode) == -1) {
                if (errno != EEXIST) {
                    syslog(LOG_ERR, "Błąd mkdir %s: %m", full_dst_path);
                    continue;
                }
            } else {
                syslog(LOG_INFO, "Utworzono katalog: %s", full_dst_path);
            }
            scan_directory(full_src_path, full_dst_path, recursive, threshold);
        }
    }
    closedir(dir);

    // usuwanie
    DIR *target_dir = opendir(target_path);
    if (!target_dir) return;

    struct dirent *target_entry;
    while ((target_entry = readdir(target_dir)) != NULL) {
        if (strcmp(target_entry->d_name, ".") == 0 || strcmp(target_entry->d_name, "..") == 0) continue;

        char check_src_path[PATH_MAX];
        char check_dst_path[PATH_MAX];
        snprintf(check_src_path, sizeof(check_src_path), "%s/%s", source_path, target_entry->d_name);
        snprintf(check_dst_path, sizeof(check_dst_path), "%s/%s", target_path, target_entry->d_name);

        struct stat src_stat;
        if (lstat(check_src_path, &src_stat) == -1 && errno == ENOENT) {
            struct stat dst_stat;
            if (lstat(check_dst_path, &dst_stat) == -1) continue;

            if (S_ISREG(dst_stat.st_mode)) {
                if (unlink(check_dst_path) == 0)
                    syslog(LOG_INFO, "Usunięto plik: %s", target_entry->d_name);
            } else if (S_ISDIR(dst_stat.st_mode) && recursive) {
                if (remove_directory_recursive(check_dst_path) == 0)
                    syslog(LOG_INFO, "Usunięto katalog: %s", target_entry->d_name);
            }
        }
    }
    closedir(target_dir);
}
// Main
int main(int argc, char *argv[]) {
	// Program powinien był otrzymać ścieżkę źródłową i ścieżkę docelową. Jeśli nie otrzymał, zwracamy błąd i kończymy program.
	if (argc < 2) {
		perror("Za mało argumentów!\n Format: ./program /ścieżka/do/źródła /ścieżka/do/celu (argumenty dodatkowe)");
		exit(EXIT_FAILURE);
	}


	// Sprawdzenie poprawności podanych argumentów
	//
	// Tu zostaną zapisane informacje o danym pliku, otrzymane poprzez stat()
	struct stat statb;

	// Ścieżka źródłowa
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

	// Ścieżka docelowa
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


	// Skoro program nie zakończył się, czyli dane argumenty są poprawne,
	// więc zamieniamy programu w demona za pomocą Linuxowej funkcji deamon()
	int status = deamon(0, 0);

	// Główna pętla programu
	while(1) {
		// Demon śpi przez 5 minut (300s)
		sleep(300);

		// Po śnie, demon porównuje katalogi; wykonuje kopiowanie, usuwanie, etc.
		
	}


	return 0;
}
