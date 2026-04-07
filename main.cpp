#include <iostream>
#include <unistd.h>      // getopt, sleep
#include <sys/stat.h>    // stat
#include <string>
#include <syslog.h>      // logowanie do syslog
#include <signal.h>      // sygnały

using namespace std;

// Struktura Config
struct Config {
    string sourcePath;
    string destPath;
    int sleepTime = 100;    // domyślnie 100 sekund
    bool recursive = false;
    int mmapThreshold = 0;
};

// Sprawdzenie, czy podana ścieżka jest katalogiem
bool isDirectory(const string& path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) return false;
    return (info.st_mode & S_IFDIR);
}

// Parsowanie argumentów
Config parseArguments(int argc, char* argv[]) {
    Config config;
    int opt;
    while ((opt = getopt(argc, argv, "s:d:t:Rm:")) != -1) {
        switch (opt) {
            case 's': config.sourcePath = optarg; break;
            case 'd': config.destPath = optarg; break;
            case 't': config.sleepTime = atoi(optarg); break;
            case 'R': config.recursive = true; break;
            case 'm': config.mmapThreshold = atoi(optarg); break;
            default:
                cerr << "Błąd argumentów!" << endl;
                exit(1);
        }
    }

    if (config.sourcePath.empty() || config.destPath.empty()) {
        cerr << "Musisz podać -s (source) i -d (destination)" << endl;
        exit(1);
    }

    if (!isDirectory(config.sourcePath) || !isDirectory(config.destPath)) {
        cerr << "Podane ścieżki nie są katalogami!" << endl;
        exit(1);
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

    cout << "Source: " << config.sourcePath << endl;
    cout << "Destination: " << config.destPath << endl;
    cout << "Sleep: " << config.sleepTime << endl;
    cout << "Recursive: " << (config.recursive ? "TAK" : "NIE") << endl;
    cout << "Mmap threshold: " << config.mmapThreshold << endl;

    while (true) {
        log_event("Entering sleep");
        cout << "Śpię..." << endl;

        for (int i = 0; i < config.sleepTime; ++i) {
            sleep(1);
            if (wake_up_flag) {
                wake_up_flag = 0;
                break;
            }
        }

        log_event("Waking up");
    }

    return 0;
}
