#include <iostream>
#include <unistd.h>   // getopt
#include <sys/stat.h> // stat
#include <string>

using namespace std;

// struktura config
struct Config {
    string sourcePath;
    string destPath;
    int sleepTime = 300; // domyślnie 5 minut
    bool recursive = false;
    int mmapThreshold = 0;
};

// sprawdzenie czy to katalog
bool isDirectory(const string& path) {
    struct stat info;

    if (stat(path.c_str(), &info) != 0) {
        return false;
    }

    return (info.st_mode & S_IFDIR);
}

// parsowanie argumentów
Config parseArguments(int argc, char* argv[]) {
    Config config;

    int opt;

    // s - source
    // d - dest
    // t - time
    // R - recursive
    // m - mmap threshold
    while ((opt = getopt(argc, argv, "s:d:t:Rm:")) != -1) {
        switch (opt) {
            case 's':
                config.sourcePath = optarg;
                break;
            case 'd':
                config.destPath = optarg;
                break;
            case 't':
                config.sleepTime = atoi(optarg);
                break;
            case 'R':
                config.recursive = true;
                break;
            case 'm':
                config.mmapThreshold = atoi(optarg);
                break;
            default:
                cerr << "Błąd argumentów!" << endl;
                exit(1);
        }
    }

    // sprawdzenie wymaganych argumentów
    if (config.sourcePath.empty() || config.destPath.empty()) {
        cerr << "Musisz podać -s (source) i -d (destination)" << endl;
        exit(1);
    }

    // walidacja katalogów
    if (!isDirectory(config.sourcePath) || !isDirectory(config.destPath)) {
        cerr << "Podane ścieżki nie są katalogami!" << endl;
        exit(1);
    }

    return config;
}

int main(int argc, char* argv[]) {
    Config config = parseArguments(argc, argv);

    cout << "Source: " << config.sourcePath << endl;
    cout << "Destination: " << config.destPath << endl;
    cout << "Sleep: " << config.sleepTime << endl;
    cout << "Recursive: " << (config.recursive ? "TAK" : "NIE") << endl;
    cout << "Mmap threshold: " << config.mmapThreshold << endl;

    while (1) {
        cout << "Śpię..." << endl;
        sleep(config.sleepTime);
    }

    return 0;
}
