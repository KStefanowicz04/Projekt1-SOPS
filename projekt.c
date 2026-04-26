#include <stdio.h>
#include <stdlib.h>      // Funkcje standardowe 
#include <unistd.h>      // Komendy Linuxowe; write, fork, etc.
#include <fcntl.h>       // Komenda open() i jej argumenty
#include <sys/stat.h>    // Informacje o plikach i strukturze stat
#include <sys/sysmacros.h>
#include <time.h>
#include "file_ops.h"    // Funkcja z plików file_ops
#include <dirent.h>
#include <errno.h>
#include <syslog.h>      // Obsługa dziennika systemowego
#include <signal.h>      // Obsługa sygnałów systemowych
#include <utime.h>
#include <string.h>      // Operacje na ciągach znaków
#include <stdint.h>      // Typ int w C
#include <stdbool.h>     // Typ boolowski w C 
#include <linux/limits.h>
#include <getopt.h>      // Argumenty podawane do programu przez użytkownika




// Struktura Config
typedef struct {
    char *sourcePath;    // Ścieżka źródłowa
    char *destPath;      // Ścieżka docelowa
    int sleepTime;       // Czas spania (domyślnie 100s)
    bool recursive;      // Flaga -R (kopiowanie rekurencyjne)
    int mmapThreshold;   // Próg mmap (domyslnie 8KB czyli wartość równa 8192)
} Config;

// Sprawdzenie, czy podana ścieżka jest katalogiem
bool isDirectory(const char* path) {
    struct stat info;
    // stat() zwraca 0 przy powodzeniu, pobiera informacje o pliku do struktury info
    if (stat(path, &info) != 0) return false;
    // Makro S_ISDIR sprawdza bity trybu (st_mode) pod kątem katalogu
    return S_ISDIR(info.st_mode);
}

// Parsowanie argumentów
Config parseArguments(int argc, char* argv[]) {
    // Inicjalizacja domyślna struktury
    Config config = {NULL, NULL, 100, false, 8192};
    int opt;

    // getopt() przetwarza argumenty linii poleceń
    while ((opt = getopt(argc, argv, "s:d:t:Rm:")) != -1) {
        switch (opt) {
            case 's': config.sourcePath = optarg; break; // source
            case 'd': config.destPath = optarg; break;   // destination
            case 't': config.sleepTime = atoi(optarg); break; // czas spania
            case 'R': config.recursive = true; break;    // rekurencja
            case 'm': config.mmapThreshold = atoi(optarg); break; // próg mmap
            default:
                fprintf(stderr, "Użycie: %s -s <src> -d <dst> [-t <time>] [-R] [-m <threshold>]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Walidacja obecności wymaganych argumentów (ścieżka źródłowa i ścieżka docelowa są wymagane)
    if (config.sourcePath == NULL || config.destPath == NULL) {
        fprintf(stderr, "Błąd: Nie podano ścieżki źródłowej albo docelowej. \n");
        fprintf(stderr, "Format programu: ./program -s </ścieżka/do/źródła> -d </ścieżka/do/folderu/docelowego> [-t <czas snu>] [-R] [-m <próg mmap>]\n");
        exit(EXIT_FAILURE);
    }

    // Walidacja poprawności ścieżek (czy obie ścieżki są katalogami?)
    if (!isDirectory(config.sourcePath) || !isDirectory(config.destPath)) {
        fprintf(stderr, "Błąd: Podane ścieżki nie są katalogami lub nie istnieją!\n");
        exit(EXIT_FAILURE);
    }

    return config;
}

// Obsługa sygnałów do wybudzania z sleep albo zatrzymania procesu demona
volatile sig_atomic_t wake_up_flag = 0;

void signal_handler(int signo) {
    // Sygnał zatrzymujący pracę demona
    if (signo == SIGINT || signo == SIGTERM) {
        syslog(LOG_INFO, "Otrzymano sygnał SIGTERM; zatrzymanie procesu demona.");
        exit(0);
    }

    // Sygnał budzący demona
    if (signo == SIGUSR1) {
        syslog(LOG_INFO, "Otrzymano sygnał SIGUSR1; budzenie demona.");
        wake_up_flag = 1;
    }
}



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
                    copy_file_with_threshold(full_src_path, full_dst_path, threshold);
                } else {
                    syslog(LOG_INFO, "Kopiowanie (mmap): %s", entry->d_name);
                    copy_file_with_threshold(full_src_path, full_dst_path, threshold);
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
                if (remove_path_sync(check_dst_path, true) == 0)
                    syslog(LOG_INFO, "Usunięto nadmiarowy katalog: %s", target_entry->d_name);
            }
        }
    }
    closedir(target_dir);
}





// Main; zawiera główną pętlę demona
int main(int argc, char* argv[]) {
    // Przetwarzanie argumentów podanych przez użytkownika
    Config config = parseArguments(argc, argv);

    // Program zamieniany jest w Demona, log startu programu
    daemon(0, 0);
    syslog(LOG_INFO, "Zamieniono proces w demona");

    // Ustawienie obsługi sygnałów
    signal(SIGINT, signal_handler);  // Sygnału kończące pracę demona ("kill <PID>")
    signal(SIGTERM, signal_handler);
    signal(SIGUSR1, signal_handler);  // Sygnał kończący pracę demona ("kill -SIGURS1 <PID>")

    // Zapisanie konfiguracji demona do syslog
    syslog(LOG_INFO, "Konfiguracja demona: Źródło - %s; Cel - %s; Czas snu - %ds; Rekurencja - %s; Próg mmap - %d",
        config.sourcePath, config.destPath, config.sleepTime, config.recursive ? "TAK" : "NIE", config.mmapThreshold);


    // Główna pętla demona
    while (1) {
        syslog(LOG_INFO, "Uśpiono demona na %d sekund.", config.sleepTime);

        // Pętla spania z możliwością przerwania przez sygnał
        for (int i = 0; i < config.sleepTime; ++i) {
            sleep(1); 
            if (wake_up_flag) {
                wake_up_flag = 0; // Reset flagi po pobudce sygnałem
                break;
            }
        }


        // Po śnie, demon porównuje katalogi; wykonuje kopiowanie, usuwanie, etc.
        syslog(LOG_INFO, "Obudzono demona.");
		scan_directory(config.sourcePath, config.destPath, config.recursive, config.mmapThreshold);  // Parametry zostały wcześniej zapisane w config
    }

    return 0;
}