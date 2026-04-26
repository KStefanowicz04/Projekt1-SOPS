#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  // Komendy Linuxowe; write, fork, etc.
#include <fcntl.h>  // Komenda open() i jej argumenty
#include <sys/stat.h>  // Komenda stat() do pobrania informacji o pliku/katalogu na danej ścieżce
#include <sys/sysmacros.h>
#include <time.h>

  //Rekurencyjnie usuwa katalog wraz z całą jego zawartością (pliki i podkatalogi).
int remove_directory_recursive(const char *path) {
    DIR *d = opendir(path);      // Próba otwarcia strumienia katalogu
    size_t path_len = strlen(path);
    int r = -1;                  // Zmienna przechowująca status operacji (-1 to błąd)

    if (d) {
        struct dirent *p;
        r = 0;                   // Katalog otwarty pomyślnie, resetujemy status na 0

        // Pętla czytająca wszystkie wpisy w katalogu
        while (!r && (p = readdir(d))) {
            int r2 = -1;
            char *buf;
            size_t len;

            // ignoruj "." i ".." aby nie wyjść poza usuwany katalog
            if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;

            // Obliczanie długości nowej ścieżki: katalog + nazwa pliku + '\0'
            len = path_len + strlen(p->d_name) + 2; 
            buf = malloc(len);   // Dynamiczna alokacja bufora na ścieżkę

            if (buf) {
                struct stat statbuf;
                // Składanie pełnej ścieżki do elementu
                snprintf(buf, len, "%s/%s", path, p->d_name);

                // Pobranie informacji o pliku/katalogu
                if (!lstat(buf, &statbuf)) {
                    if (S_ISDIR(statbuf.st_mode)) {
                        // Jeśli to katalog wywołanie rekurencyjne (wchodzimy w głąb)
                        r2 = remove_directory_recursive(buf);
                    } else {
                        // Jeśli to plik lub inny typ usuwamy bezpośrednio
                        r2 = unlink(buf);
                    }
                }
                free(buf);       // Zwolnienie pamięci bufora po użyciu
            }
            r = r2;              // Aktualizacja statusu jeśli r2 == -1, pętla zostanie przerwana
        }
        closedir(d);             // Zamknięcie strumienia katalogu
    }

    // Jeśli zawartość została usunięta pomyślnie (r == 0), usuwamy sam obecnie pusty katalog
    if (!r) {
        r = rmdir(path);
    }

    return r;  
/**
 * Główna funkcja synchronizująca drzewa katalogów.
 * * @param source_path    Ścieżka do katalogu źródłowego.
 * @param target_path    Ścieżka do katalogu docelowego .
 * @param recursive      Flaga logiczna (0/1). Jeśli 1, funkcja skanuje podkatalogi (opcja -R).
 * @param threshold      Próg rozmiaru pliku w bajtach, decydujący o metodzie kopiowania (opcja -s).
 */
void scan_directory(const char *source_path, const char *target_path, int recursive, size_t threshold) {
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    char full_src_path[PATH_MAX];
    char full_dst_path[PATH_MAX];

    // Otwarcie strumienia katalogu źródłowego
    dir = opendir(source_path);
    if (!dir) {
        // %m w syslog automatycznie wypisuje opis błędu z errno (np. "Permission denied")
        syslog(LOG_ERR, "Nie można otworzyć katalogu źródłowego %s: %m", source_path);
        return;
    }

    // Skanowanie źródła i aktualizacja celu
    while ((entry = readdir(dir)) != NULL) {
        // Ignorowanie "." i ".." zapobiega nieskończonej pętli w rekurencji
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        // Budowanie pełnych ścieżek dla źródła i celu
        snprintf(full_src_path, sizeof(full_src_path), "%s/%s", source_path, entry->d_name);
        snprintf(full_dst_path, sizeof(full_dst_path), "%s/%s", target_path, entry->d_name);

        // lstat pobiera informacje o pliku 
        if (lstat(full_src_path, &statbuf) == -1) continue;

        // Jeśli napotkany element jest zwykłym plikiem
        if (S_ISREG(statbuf.st_mode)) {
            struct stat target_stat;
            int needs_sync = 0;

            // Sprawdzenie, czy plik istnieje w katalogu docelowym
            if (lstat(full_dst_path, &target_stat) == -1) {
                if (errno == ENOENT) { // ENOENT = plik nie istnieje
                    syslog(LOG_INFO, "Wykryto nowy plik: %s", entry->d_name);
                    needs_sync = 1;
                }
            } else {
                // Porównanie czasu modyfikacji [st_mtime]
                // Synchronizujemy tylko jeśli źródło jest nowsze niż cel
                if (statbuf.st_mtime > target_stat.st_mtime) {
                    syslog(LOG_INFO, "Plik zmodyfikowany: %s", entry->d_name);
                    needs_sync = 1;
                }
            }

            if (needs_sync) {
                // Wybór metody kopiowania na podstawie rozmiaru pliku 
                if (statbuf.st_size < (off_t)threshold) {
                    syslog(LOG_INFO, "Kopiowanie (read/write): %s", entry->d_name);
                    // copy_read_write(full_src_path, full_dst_path); // Issue #4
                } else {
                    syslog(LOG_INFO, "Kopiowanie (mmap): %s", entry->d_name);
                    // copy_mmap(full_src_path, full_dst_path); // Issue #4
                }
                
                // synchronizacja czasu modyfikacji po skopiowaniu w  obu plikach 
                //  przy następnym obudzeniu daty będą identyczne
                struct utimbuf time_data;
                time_data.actime = statbuf.st_atime;  
                time_data.modtime = statbuf.st_mtime; 
                if (utime(full_dst_path, &time_data) == -1) {
                    syslog(LOG_ERR, "Błąd ustawiania czasu dla %s: %m", full_dst_path);
                }
            }
        } 
        // Jeśli napotkany element jest katalogiem i włączono flagę rekurencji (-R)
        else if (S_ISDIR(statbuf.st_mode) && recursive) {
            // Próba stworzenia katalogu w celu z uprawnieniami ze źródła
            if (mkdir(full_dst_path, statbuf.st_mode) == -1) {
                if (errno != EEXIST) { // Jeśli błąd to nie "katalog już istnieje"
                    syslog(LOG_ERR, "Błąd mkdir %s: %m", full_dst_path);
                    continue;
                }
            } else {
                syslog(LOG_INFO, "Utworzono katalog: %s", full_dst_path);
            }
            // Wywołanie rekurencyjne dla podkatalogu
            scan_directory(full_src_path, full_dst_path, recursive, threshold);
        }
    }
    closedir(dir);

    // Czyszczenie celu usuwanie nadmiarowych plików
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
        // Sprawdzamy, czy plik z celu istnieje w źródle
        if (lstat(check_src_path, &src_stat) == -1 && errno == ENOENT) {
            struct stat dst_stat;
            if (lstat(check_dst_path, &dst_stat) == -1) continue;

            // Jeśli plik jest w celu, a nie ma go w źródle -> usuwamy
            if (S_ISREG(dst_stat.st_mode)) {
                if (unlink(check_dst_path) == 0)
                    syslog(LOG_INFO, "Usunięto nadmiarowy plik: %s", target_entry->d_name);
            } 
            // Jeśli katalog jest w celu, a nie ma go w źródle, usuwamy rekurencyjnie
            else if (S_ISDIR(dst_stat.st_mode) && recursive) {
                if (remove_directory_recursive(check_dst_path) == 0)
                    syslog(LOG_INFO, "Usunięto nadmiarowy katalog: %s", target_entry->d_name);
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
