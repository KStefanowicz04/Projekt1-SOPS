#ifndef FILE_OPS_H
#define FILE_OPS_H

#include <stdbool.h>
#include <sys/types.h>

/*
 * Copy one file from src_path to dst_path.
 * For files larger than mmap_threshold, uses mmap()+write().
 * For smaller files, uses read()+write().
 * On success keeps destination timestamps equal to source timestamps.
 * Returns 0 on success, -1 on failure (errno is set).
 */
int copy_file_with_threshold(const char *src_path, const char *dst_path, off_t mmap_threshold);

/*
 * Remove a path from destination tree.
 * If recursive is true and path is a directory, remove all nested contents.
 * If recursive is false, directory removal only works for empty directories.
 * Returns 0 on success, -1 on failure (errno is set).
 */
int remove_path_sync(const char *path, bool recursive);

#endif
