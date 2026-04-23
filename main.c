#include <stdio.h>       
#include <stdlib.h>      // Funkcje standardowe 
#include <unistd.h>      // API systemowe POSIX 
#include <sys/stat.h>    // Informacje o plikach i strukturze stat
#include <string.h>      // Operacje na ciągach znaków
#include <syslog.h>      // Obsługa dziennika systemowego 
#include <signal.h>      // Obsługa sygnałów systemowych
#include <stdbool.h>     // Typ boolowski w C 


// Struktura Config
typedef struct {
    char *sourcePath;    // Ścieżka źródłowa
    char *destPath;      // Ścieżka docelowa
    int sleepTime;       // Czas spania (domyślnie 100s)
    bool recursive;      // Flaga -R (kopiowanie rekurencyjne)
    int mmapThreshold;   // Próg mmap 
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
    Config config = {NULL, NULL, 100, false, 0};
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

    // Walidacja obecności wymaganych argumentów
    if (config.sourcePath == NULL || config.destPath == NULL) {
        fprintf(stderr, "Błąd: Musisz podać -s (źródło) i -d (cel)\n");
        exit(EXIT_FAILURE);
    }

    // Walidacja poprawności ścieżek (czy są katalogami)
    if (!isDirectory(config.sourcePath) || !isDirectory(config.destPath)) {
        fprintf(stderr, "Błąd: Podane ścieżki nie są katalogami lub nie istnieją!\n");
        exit(EXIT_FAILURE);
    }

    return config;
}

// Funkcja logowania do syslog
void log_event(const char* message) {
    openlog("ProjektDemon", LOG_PID | LOG_CONS, LOG_USER);
    syslog(LOG_INFO, "%s", message);
    closelog();
}

// Obsługa sygnałów do wybudzania z sleep
volatile sig_atomic_t wake_up_flag = 0;

void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        log_event("Received signal - waking up");
        wake_up_flag = 1;
    }
}

int main(int argc, char* argv[]) {
    Config config = parseArguments(argc, argv);

    // Log startu programu
    log_event("Daemon start");

    // Ustawienie obsługi sygnałów
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Konfiguracja Demona:\n");
    printf("- Źródło: %s\n", config.sourcePath);
    printf("- Cel: %s\n", config.destPath);
    printf("- Czas spania: %d s\n", config.sleepTime);
    printf("- Rekurencja: %s\n", config.recursive ? "TAK" : "NIE");
    printf("- Próg mmap: %d bajtów\n", config.mmapThreshold);

    // Główna pętla demona
    while (1) {
        log_event("Entering sleep mode");
        printf("Demon śpi... Wyślij SIGINT (Ctrl+C), aby go obudzić.\n");

        // Pętla spania z możliwością przerwania przez sygnał
        for (int i = 0; i < config.sleepTime; ++i) {
            sleep(1); 
            if (wake_up_flag) {
                wake_up_flag = 0; // Reset flagi po pobudce sygnałem
                break;
            }
        }

        
        log_event("Waking up");
        
    }

    return 0;
}
