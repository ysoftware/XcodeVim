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

void find_latest_file(const char *directory, char *latest_file, time_t *latest_mtime, const char *project_name) {
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

        // this is how we find our folder
        if (project_name != NULL) {
            if (strstr(directory, project_name) == NULL) {
                if (strstr(entry->d_name, project_name) == NULL) {
                    continue;
                }
            } else if (strstr(directory, "Logs") == NULL) {
                if (strstr(entry->d_name, "Logs") == NULL) {
                    continue;
                }
            } else if (strstr(directory, "Build") == NULL) {
                if (strstr(entry->d_name, "Build") == NULL) {
                    continue;
                }
            }
        }

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

        struct stat file_stat;
        if (stat(path, &file_stat) == -1) {
            /* fprintf(stderr, "Unable to read: %s\n", path); */
            continue;
        }

        if (S_ISDIR(file_stat.st_mode)) {
            find_latest_file(path, latest_file, latest_mtime, project_name);
        } else if (S_ISREG(file_stat.st_mode) && strstr(entry->d_name, ".xcactivitylog") != NULL) {
            if (file_stat.st_mtime > *latest_mtime) {
                *latest_mtime = file_stat.st_mtime;
                strcpy(latest_file, path);
            }
        }
    }

    closedir(dir);
}

void parse_xcactivitylog(char *input, FILE *output, bool output_full_log) {
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
    
    char *messages[10];
    char messages_count = 0;

    char *file_name;
    int line;
    int column;

    int classes_found = 1;
    char *classes[10];

    while (*p != '\0') {
        if (*p >= '0' && *p <= '9' || *p >= 'a' && *p <= 'f') {
            uint64_t value = 0;
            // to not fail when encountering doubles, we also parse these
            while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
                value = value * 10 + (*p - '0');
                (p)++;
            }

            if (*p == '#') { // integer
                if (output_full_log) {
                    fprintf(output, "[type: \"int\", value: %" PRIu64 "]\n", value);
                }

                if (next_int_is_line) {
                    next_int_is_line = false;
                    next_int_is_column = true;
                    line = value;
                } else if (next_int_is_column) {
                    next_int_is_column = false;
                    column = value;
                }

                p++;
            } else if (*p == '^') { // double
                if (output_full_log) {
                    fprintf(output, "[type: \"double\", value: not parsed]\n");
                }
                p++;
            } else if (*p == '@') { // classInstance
                char *class_name = classes[value];

                if (strcmp(class_name, "IDEDiagnosticActivityLogMessage") == 0) {
                    found_diagnostic_activity_log_message = true;
                    next_string_is_message = true;
                    next_string_is_log_type = false;
                    next_string_is_file = false;
                } else if (found_diagnostic_activity_log_message && strcmp(class_name, "DVTTextDocumentLocation") == 0) {
                    next_string_is_file = true;
                    next_int_is_line = true;
                } else {
                    found_diagnostic_activity_log_message = false;
                    next_string_is_file = false;
                    next_int_is_line = false;
                }

                if (output_full_log) {
                    fprintf(output, "[type: \"classInstance\", value: \"%s\"]\n", class_name);
                }
                p++;
            } else if (*p == '"' || *p == '*') { // string
                p++;
                char *str_value = (char *)malloc(value + 1);
                strncpy(str_value, p, value);
                str_value[value] = '\0';  // Null-terminate the string

                if (found_diagnostic_activity_log_message) {
                    if (next_string_is_message) {
                        last_message = str_value;
                        /* next_string_is_message = false; */
                    } else if (next_string_is_file) {
                        file_name = str_value;
                        next_string_is_file = false;
                        next_string_is_log_type = true;
                    } else if (next_string_is_log_type) {
                        next_string_is_log_type = false;

                        if (strcmp(str_value, "Swift Compiler Error") == 0) {
                            printf("%s:%d:%d: %s\n", file_name, line + 1, column + 1, message);
                            found_diagnostic_activity_log_message = false;
                        }
                    }
                }

                // free(str_value); // who cares

                if (output_full_log) {
                    fprintf(output, "[type: \"string\", length: %llu, value: \"%s\"]\n", value, str_value);
                }
                p += value;
            } else if (*p == '%') { // className
                p++;
                char *str_value = (char *)malloc(value + 1);
                strncpy(str_value, p, value);
                str_value[value] = '\0';
                p += value;
                if (output_full_log) {
                    fprintf(output, "[type: \"className\", index: %d, value: \"%s\"]\n", classes_found, str_value);
                }
                classes[classes_found] = str_value;
                classes_found++;

                // free(str_value); // leak all class names, because we use them later
            } else if (*p == '(') {
                if (output_full_log) {
                    fprintf(output, "[type: \"array\", count: %" PRIu64 "]\n", value);
                }
                p++;
            } else { // unexpected
                fprintf(stderr, "[ERROR] Unexpected token %c after a number.\n", *p);
                exit(1);
            }
        } else if (*p == '-') {
            if (output_full_log) {
                fprintf(output, "[type: \"null\"]\n");
            }
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
}

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0) {
        printf("Usage: %s <project_name> [-d]\n\n", argv[0]);
        printf("Latest modified found inside will by analysed (~/Library/Developer/Xcode/DerivedData/[project_name]*/Logs/Build/)\n\n");
        printf("Pass flag -d to dump the whole parsed log as log-dump.txt\n");
        exit(1);
    }

    bool output_full_log = false;
    if (argc >= 3 && strcmp(argv[2], "-d") == 0) {
        output_full_log = true;
    }

    char path[250];
    const char *home_dir = getenv("HOME");
    sprintf(path, "%s/Library/Developer/Xcode/DerivedData", home_dir);

    char *project_name = argv[1];

    char latest_file[1024] = {0};
    time_t latest_mtime = 0;
    find_latest_file(path, latest_file, &latest_mtime, project_name);
    if (strlen(latest_file) > 0) {
    } else {
        printf("No .xcactivitylog files found.\n");
    }

    gzFile gz_input = gzopen(latest_file, "rb");
    if (!gz_input) {
        perror("Failed to open gzipped file");
        return 1;
    }

    FILE *output = NULL;
    if (output_full_log) {
        output = fopen("log-dump.txt", "w");
        if (!output) {
            perror("Failed to open output file");
            gzclose(gz_input);
            return 1;
        }
    }

    const int BUFFER_SIZE = 100000000; // 100 MB is easy for modern computers
    char *buffer = (char*) malloc(BUFFER_SIZE);
    gzread(gz_input, buffer, BUFFER_SIZE);
    parse_xcactivitylog(buffer, output, output_full_log);

    gzclose(gz_input);
    if (output_full_log) {
        fclose(output);
    }

    return 0;
}



// TODO: proper parsing of these classes
//
/* [type: "classInstance", value: "IDEDiagnosticActivityLogMessage"] */
/* [type: "string", length: 21, value: "Expected '}' in class"] */
/* [type: "null"] */
/* [type: "int", value: 746354466] */
/* [type: "int", value: 18446744073709551615] */
/* [type: "int", value: 0] */

/* [type: "array", count: 1] */
/*     [type: "classInstance", value: "IDEDiagnosticActivityLogMessage"] */
/*     [type: "string", length: 25, value: "To match this opening '{'"] */
/*     [type: "null"] */
/*     [type: "int", value: 746354466] */
/*     [type: "int", value: 18446744073709551615] */
/*     [type: "int", value: 0] */
/* arr [type: "null"] */
/*     [type: "int", value: 0] */
/*     [type: "string", length: 27, value: "com.apple.dt.IDE.diagnostic"] */
/*         [type: "classInstance", value: "DVTTextDocumentLocation"] */
/*         [type: "string", length: 177, value: "file:///Users/iaroslav.erokhin/Documents/Check24/ios-pod-mobile-sim/Pod/Classes/CheckoutShared/Features/Checkout/InputManagement/Data%20Sources/CheckoutIBANInputDataSource.swift"] */
/*         [type: "double", value: not parsed] */
/*         [type: "int", value: 10] */
/*         [type: "int", value: 69] */
/*         [type: "int", value: 10] */
/*         [type: "int", value: 69] */
/*         [type: "int", value: 18446744073709551615] */
/*         [type: "int", value: 0] */
/*         [type: "int", value: 0] */
/*         [type: "string", length: 21, value: "Swift Compiler Notice"] */
/*         [type: "array", count: 0] */
/*         [type: "null"] */

/* [type: "int", value: 2] */
/* [type: "string", length: 27, value: "com.apple.dt.IDE.diagnostic"] */
/* [type: "classInstance", value: "DVTTextDocumentLocation"] */
/* [type: "string", length: 177, value: "file:///Users/iaroslav.erokhin/Documents/Check24/ios-pod-mobile-sim/Pod/Classes/CheckoutShared/Features/Checkout/InputManagement/Data%20Sources/CheckoutIBANInputDataSource.swift"] */
/* [type: "double", value: not parsed] */
/* [type: "int", value: 12] */
/* [type: "int", value: 4] */
/* [type: "int", value: 12] */
/* [type: "int", value: 4] */
/* [type: "int", value: 18446744073709551615] */
/* [type: "int", value: 0] */
/* [type: "int", value: 0] */
/* [type: "string", length: 20, value: "Swift Compiler Error"] */
/* [type: "array", count: 0] */
/* [type: "null"] */


// TODO: keep modified time of last build to not open old ones after problem was solved (how does that evne happen? we still have the latest build report which is ok)
