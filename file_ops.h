#ifndef FILE_OPS_H
#define FILE_OPS_H

#include <stdbool.h>
#include <sys/types.h>

/*
 * Kopiuje pojedynczy plik z src_path do dst_path.
 * Dla duzych plikow (>= mmap_threshold) uzywa mmap()+write().
 * Dla mniejszych plikow uzywa read()+write().
 * Po skopiowaniu ustawia czasy pliku docelowego jak w zrodle.
 * Zwraca 0 przy sukcesie, -1 przy bledzie (ustawia errno).
 */
int copy_file_with_threshold(const char *src_path, const char *dst_path, off_t mmap_threshold);

/*
 * Usuwa sciezke z drzewa docelowego.
 * Gdy recursive=true i sciezka jest katalogiem, usuwa zawartosc rekurencyjnie.
 * Gdy recursive=false, katalog moze byc usuniety tylko jesli jest pusty.
 * Zwraca 0 przy sukcesie, -1 przy bledzie (ustawia errno).
 */
int remove_path_sync(const char *path, bool recursive);

#endif
