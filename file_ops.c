#define _POSIX_C_SOURCE 200809L

#include "file_ops.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define COPY_BUFFER_SIZE 65536

static int write_all(int fd, const char *buf, size_t count) {
    size_t total = 0;

    while (total < count) {
        size_t remaining = count - total;
        size_t chunk = remaining;
        if (chunk > (size_t)SSIZE_MAX) {
            chunk = (size_t)SSIZE_MAX;
        }

        ssize_t written = write(fd, buf + total, chunk);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            errno = EIO;
            return -1;
        }
        total += (size_t)written;
    }

    return 0;
}

static int apply_src_timestamps(const char *dst_path, const struct stat *src_st) {
    struct timespec times[2];

    times[0] = src_st->st_atim;
    times[1] = src_st->st_mtim;

    return utimensat(AT_FDCWD, dst_path, times, 0);
}

static int copy_small_read_write(const char *src_path, const char *dst_path, const struct stat *src_st) {
    int src_fd = -1;
    int dst_fd = -1;
    char *buffer = NULL;
    int result = -1;

    src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        return -1;
    }

    dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, src_st->st_mode & 0777);
    if (dst_fd < 0) {
        goto cleanup;
    }

    if (fchmod(dst_fd, src_st->st_mode & 0777) < 0) {
        goto cleanup;
    }

    buffer = (char *)malloc(COPY_BUFFER_SIZE);
    if (buffer == NULL) {
        errno = ENOMEM;
        goto cleanup;
    }

    for (;;) {
        ssize_t read_count = read(src_fd, buffer, COPY_BUFFER_SIZE);

        if (read_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            goto cleanup;
        }

        if (read_count == 0) {
            break;
        }

        if (write_all(dst_fd, buffer, (size_t)read_count) < 0) {
            goto cleanup;
        }
    }

    if (apply_src_timestamps(dst_path, src_st) < 0) {
        goto cleanup;
    }

    result = 0;

cleanup:
    free(buffer);
    if (src_fd >= 0) {
        close(src_fd);
    }
    if (dst_fd >= 0) {
        close(dst_fd);
    }
    return result;
}

static int copy_large_mmap_write(const char *src_path, const char *dst_path, const struct stat *src_st) {
    int src_fd = -1;
    int dst_fd = -1;
    int result = -1;
    void *mapped = MAP_FAILED;

    src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        return -1;
    }

    dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, src_st->st_mode & 0777);
    if (dst_fd < 0) {
        goto cleanup;
    }

    if (fchmod(dst_fd, src_st->st_mode & 0777) < 0) {
        goto cleanup;
    }

    if (src_st->st_size > 0) {
        size_t total = (size_t)src_st->st_size;

        if (ftruncate(dst_fd, src_st->st_size) < 0) {
            goto cleanup;
        }

        mapped = mmap(NULL, total, PROT_READ, MAP_PRIVATE, src_fd, 0);
        if (mapped == MAP_FAILED) {
            goto cleanup;
        }

        if (write_all(dst_fd, (const char *)mapped, total) < 0) {
            goto cleanup;
        }
    }

    if (apply_src_timestamps(dst_path, src_st) < 0) {
        goto cleanup;
    }

    result = 0;

cleanup:
    if (mapped != MAP_FAILED) {
        munmap(mapped, (size_t)src_st->st_size);
    }
    if (src_fd >= 0) {
        close(src_fd);
    }
    if (dst_fd >= 0) {
        close(dst_fd);
    }
    return result;
}

int copy_file_with_threshold(const char *src_path, const char *dst_path, off_t mmap_threshold) {
    struct stat src_st;

    if (src_path == NULL || dst_path == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (mmap_threshold < 0) {
        errno = EINVAL;
        return -1;
    }

    if (lstat(src_path, &src_st) < 0) {
        return -1;
    }

    if (!S_ISREG(src_st.st_mode)) {
        errno = EINVAL;
        return -1;
    }

    if (src_st.st_size >= mmap_threshold && mmap_threshold > 0) {
        return copy_large_mmap_write(src_path, dst_path, &src_st);
    }

    return copy_small_read_write(src_path, dst_path, &src_st);
}

static int remove_path_recursive(const char *path) {
    struct stat st;

    if (lstat(path, &st) < 0) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }

    if (!S_ISDIR(st.st_mode)) {
        return unlink(path);
    }

    DIR *dir = opendir(path);
    if (dir == NULL) {
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t path_len;
        size_t name_len;
        char *child;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        path_len = strlen(path);
        name_len = strlen(entry->d_name);
        child = (char *)malloc(path_len + 1 + name_len + 1);
        if (child == NULL) {
            closedir(dir);
            errno = ENOMEM;
            return -1;
        }

        memcpy(child, path, path_len);
        child[path_len] = '/';
        memcpy(child + path_len + 1, entry->d_name, name_len);
        child[path_len + 1 + name_len] = '\0';

        if (remove_path_recursive(child) < 0) {
            free(child);
            closedir(dir);
            return -1;
        }

        free(child);
    }

    if (closedir(dir) < 0) {
        return -1;
    }

    return rmdir(path);
}

int remove_path_sync(const char *path, bool recursive) {
    struct stat st;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (lstat(path, &st) < 0) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }

    if (!S_ISDIR(st.st_mode)) {
        return unlink(path);
    }

    if (recursive) {
        return remove_path_recursive(path);
    }

    return rmdir(path);
}
