#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>
#include <inttypes.h>
#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#define DEBUG 0
#define PROJECT_NAME "C24MobileSimOnly"

void find_latest_file(const char *directory, char *latest_file, time_t *latest_mtime) {
    DIR *dir = opendir(directory);
    if (!dir) {
        fprintf(stderr, "Unable to access directory: %s\n", directory);
        exit(1);
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // this is how we skip all other folders
        if (strstr(directory, PROJECT_NAME) == NULL) {
            // when PROJECT_PATH is not included in the dir, this means we didn't open it yet - keep looking
            if (strstr(entry->d_name, PROJECT_NAME) != NULL) {
                continue;
            }
        } else if (strstr(directory, "Build") == NULL) {
            if (strstr(entry->d_name, "Build") != NULL) {
                continue;
            }
        } else if (strstr(entry->d_name, "Logs") != NULL) {
            continue;
        }

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

        struct stat file_stat;
        if (stat(path, &file_stat) == -1) {
            /* fprintf(stderr, "Unable to read: %s\n", path); */
            continue;
        }

        if (S_ISDIR(file_stat.st_mode)) {
            find_latest_file(path, latest_file, latest_mtime);
        } else if (S_ISREG(file_stat.st_mode) && strstr(entry->d_name, ".xcactivitylog") != NULL) {
            if (file_stat.st_mtime > *latest_mtime) {
                *latest_mtime = file_stat.st_mtime;
                strcpy(latest_file, path);
            }
        }
    }

    closedir(dir);
}

void parse_xcactivitylog(char *input, FILE *output, FILE *output_log) {
    const char *p = input;

    if (strncmp(p, "SLF0", 4) != 0) {
        fprintf(stderr, "[ERROR] Invalid SLF header\n");
        exit(1);
    }
    p += 4;

    // important data
    bool found_diagnostic_activity_log_message = false;
    bool next_string_is_message = false;
    bool next_string_is_file = false;
    bool next_string_is_log_type = false;
    bool next_int_is_line = false;
    bool next_int_is_column = false;
    char *message;
    char *file_name;
    int line;
    int column;

    int classes_found = 1;
    int next_padded_instances = 0; // to print arrays
    char *classes[10];

    while (*p != '\0') {
        if (*p >= '0' && *p <= '9' || *p >= 'a' && *p <= 'f') {
            uint64_t value = 0;
            uint64_t hex_value = 0;
            while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
                value = value * 10 + (*p - '0');
                /* hex_value = hex_value * 16 + (*p >= 'a' ? *p - 'a' + 10 : (*p >= 'A' ? *p - 'A' + 10 : *p - '0')); // this is not correct */
                (p)++;
            }

            if (*p == '#') { // integer
#if DEBUG
                if (next_padded_instances > 0) { fprintf(output, "    "); }
                fprintf(output, "[type: \"int\", value: %" PRIu64 "]\n", value);
#endif

                if (next_int_is_line) {
                    next_int_is_line = false;
                    next_int_is_column = true;
                    line = value;
                    /* printf("found line\n"); */
                } else if (next_int_is_column) {
                    next_int_is_column = false;
                    column = value;
                    /* printf("found column\n"); */
                }

                p++;
            } else if (*p == '^') { // double
                double double_value = *((double*)&hex_value);
                double_value = __builtin_bswap64(hex_value);  // Convert from little-endian
#if DEBUG
                if (next_padded_instances > 0) { fprintf(output, "    "); }
                fprintf(output, "[type: \"double\", value: %f]\n", double_value);
#endif
                p++;
            } else if (*p == '@') { // classInstance with index of declared class
                char *class_name = classes[value];
            
                if (strcmp(class_name, "IDEDiagnosticActivityLogMessage") == 0) {
                    if (found_diagnostic_activity_log_message) {
                        /* printf("WTF! WE ARE ALREADY LOOKING AT ONE\n"); */
                    }
                    /* printf("\n>>> IDEDiagnosticActivityLogMessage\n"); */
                    found_diagnostic_activity_log_message = true;
                    next_string_is_message = true;
                    next_string_is_log_type = false;
                    next_string_is_file = false;
                } else if (found_diagnostic_activity_log_message && strcmp(class_name, "DVTTextDocumentLocation") == 0) {
                    /* printf(" - Found DVTTextDocumentLocation following IDEDiagnosticActivityLogMessage\n"); */
                    next_string_is_file = true;
                    next_int_is_line = true;
                } else {
                    /* printf("found something else..... '%s'\n", class_name); */
                    found_diagnostic_activity_log_message = false;
                    next_string_is_file = false;
                    next_int_is_line = false;
                }

#if DEBUG
                if (next_padded_instances > 0) { fprintf(output, "    "); }
                fprintf(output, "[type: \"classInstance\", value: \"%s\"]\n", class_name);
#endif
                next_padded_instances--;
                p++;
            } else if (*p == '"' || *p == '*') { // string
                p++;
                char *str_value = (char *)malloc(value + 1);
                strncpy(str_value, p, value);
                str_value[value] = '\0';  // Null-terminate the string

                if (found_diagnostic_activity_log_message) {
                    if (next_string_is_message) {
                        message = str_value;
                        next_string_is_message = false;
                        /* printf(" - Found Message: %s\n", message); */
                    } else if (next_string_is_file) {
                        file_name = str_value;
                        /* printf(" - File name: %s\n", file_name); */
                        next_string_is_file = false;
                        next_string_is_log_type = true;
                    } else if (next_string_is_log_type) {
                        next_string_is_log_type = false;

                        if (strcmp(str_value, "Swift Compiler Error") == 0) {
                            fprintf(output_log, "%s:%d:%d: %s\n", file_name, line + 1, column + 1, message);
                            /* printf("%s:%d:%d: %s\n", file_name, line + 1, column + 1, message); */
                            /* printf("Found swift compiler error\n\n"); */
                        } else {
                            /* printf(" - Nope! the string was '%s'\n\n", str_value); */
                            found_diagnostic_activity_log_message = false;
                        }
                    }
                }

                // free(str_value); // who cares

#if DEBUG
                if (next_padded_instances > 0) { fprintf(output, "    "); }
                fprintf(output, "[type: \"string\", length: %llu, value: \"%s\"]\n", value, str_value);
#endif
                p += value;
            } else if (*p == '%') { // className
                p++;
                char *str_value = (char *)malloc(value + 1);
                strncpy(str_value, p, value);
                str_value[value] = '\0';
                p += value;
#if DEBUG
                fprintf(output, "[type: \"className\", index: %d, value: \"%s\"]\n", classes_found, str_value);
#endif
                classes[classes_found] = str_value;
                classes_found++;

                // free(str_value); // leak all class names, because we use them later
            } else if (*p == '(') {
                next_padded_instances = value;
#if DEBUG
                fprintf(output, "[type: \"array\", count: %" PRIu64 "]\n", value);
#endif
                p++;
            } else { // unexpected
                fprintf(stderr, "[ERROR] Unexpected token %c after a number.\n", *p);
                exit(1);
            }
        } else if (*p == '-') {
#if DEBUG
            if (next_padded_instances > 0) { fprintf(output, "    "); }
            fprintf(output, "[type: \"null\"]\n");
#endif
            p++;
        } else if (*p == '\n') {
            p++;
        } else if (*p == '\0') {
            /* printf("[EOF]\n"); */
        } else {
            fprintf(stderr, "[ERROR] Unexpected token '%c'.\n", *p);
            exit(1);
        }
    }
    printf("Finished work: %d\n", *p == '\0');
}

int main(int argc, char *argv[]) {
    char path[250];
    const char *home_dir = getenv("HOME");
    sprintf(path, "%s/Library/Developer/Xcode/DerivedData", home_dir);

    char latest_file[1024] = {0};
    time_t latest_mtime = 0;
    find_latest_file(path, latest_file, &latest_mtime);
    if (strlen(latest_file) > 0) {
        printf("Found latest log: %s\n", latest_file);
    } else {
        printf("No .xcactivitylog files found.\n");
    }

    gzFile gz_input = gzopen(latest_file, "rb");
    if (!gz_input) {
        perror("Failed to open gzipped file");
        return 1;
    }

    FILE *output;
#if DEBUG
    output = fopen("log-dump.txt", "w");
    if (!output) {
        perror("Failed to open output file");
        gzclose(gz_input);
        return 1;
    }
#endif

    FILE *output_log = fopen(argv[2], "w");
    if (!output_log) {
        perror("Failed to open error log file");
        gzclose(gz_input);
        return 1;
    }

    const int BUFFER_SIZE = 100000000; // 100 MB cuz M2 is a supercomputer
    char *buffer = (char*) malloc(BUFFER_SIZE);
    gzread(gz_input, buffer, BUFFER_SIZE);
    parse_xcactivitylog(buffer, output, output_log);

    gzclose(gz_input);
#if DEBUG
    fclose(output);
#endif
    fclose(output_log);

    return 0;
}
