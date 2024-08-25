#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>
#include <inttypes.h>
#include <stdbool.h>

#define BUFFER_SIZE 20000000

void parse_xcactivitylog(char input[BUFFER_SIZE], FILE *output, FILE *output_log) {
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
                if (next_padded_instances > 0) { fprintf(output, "    "); }
                fprintf(output, "[type: \"int\", value: %" PRIu64 "]\n", value);

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
                double double_value = *((double*)&hex_value);
                double_value = __builtin_bswap64(hex_value);  // Convert from little-endian
                if (next_padded_instances > 0) { fprintf(output, "    "); }
                fprintf(output, "[type: \"double\", value: %f]\n", double_value);
                p++;
            } else if (*p == '@') { // classInstance with index of declared class
                char *class_name = classes[value];
            
                if (strcmp(class_name, "IDEDiagnosticActivityLogMessage") == 0) {
                    if (found_diagnostic_activity_log_message) {
                        printf("WTF! WE ARE ALREADY LOOKING AT ONE\n");
                    }
                    printf("\n>>> IDEDiagnosticActivityLogMessage\n");
                    found_diagnostic_activity_log_message = true;
                    next_string_is_message = true;
                } else if (found_diagnostic_activity_log_message && strcmp(class_name, "DVTTextDocumentLocation") == 0) {
                    printf(" - Found DVTTextDocumentLocation following IDEDiagnosticActivityLogMessage\n");
                    next_string_is_file = true;
                } else {
                    printf("found something else..... %s\n", class_name);
                    found_diagnostic_activity_log_message = false;
                }

                if (next_padded_instances > 0) { fprintf(output, "    "); }
                fprintf(output, "[type: \"classInstance\", value: \"%s\"]\n", class_name);
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
                        printf(" - Found Message: %s\n", message);
                    } else if (next_string_is_file) {
                        file_name = str_value;
                        printf(" - File name: %s\n", file_name);
                        next_string_is_file = false;
                        next_string_is_log_type = true;
                    } else if (next_string_is_log_type) {
                        next_string_is_log_type = false;

                        if (strcmp(str_value, "Swift Compiler Error") == 0) {
                            // confirmed! gather up all data
                            fprintf(output_log, "%s:%d:%d: %s", file_name, line, column, message);
                            printf("Found swift compiler error\n");
                        } else {
                            printf(" - Nope! the string was '%s'\n\n", str_value);
                            found_diagnostic_activity_log_message = false;
                        }
                    }
                }

                if (next_padded_instances > 0) { fprintf(output, "    "); }
                fprintf(output, "[type: \"string\", length: %lu, value: \"%s\"]\n", value, str_value);
                /* free(str_value); */ // let it leak
                p += value;
            } else if (*p == '%') { // className
                p++;
                char *str_value = (char *)malloc(value + 1);
                strncpy(str_value, p, value);
                str_value[value] = '\0';
                p += value;
                fprintf(output, "[type: \"className\", index: %d, value: \"%s\"]\n", classes_found, str_value);
                classes[classes_found] = str_value;
                classes_found++;
                /* free(str_value); */ // let it leak
            } else if (*p == '(') {
                next_padded_instances = value;
                fprintf(output, "[type: \"array\", count: %" PRIu64 "]\n", value);
                p++;
            } else { // unexpected
                fprintf(stderr, "[ERROR] Unexpected token %c after a number.\n", *p);
                exit(1);
            }
        } else if (*p == '-') {
            if (next_padded_instances > 0) { fprintf(output, "    "); }
            fprintf(output, "[type: \"null\"]\n");
            p++;
        } else if (*p == '\n') {
            p++;
        } else if (*p == '\0') {
            printf("[EOF]\n");
        } else {
            fprintf(stderr, "[ERROR] Unexpected token '%c'.\n", *p);
            exit(1);
        }
    }
    printf("Finished work: %d\n", *p == '\0');
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <input.xcactivitylog> <output.txt>\n", argv[0]);
        return 1;
    }

    gzFile gz_input = gzopen(argv[1], "rb");
    if (!gz_input) {
        perror("Failed to open gzipped file");
        return 1;
    }

    FILE *output = fopen(argv[2], "w");
    if (!output) {
        perror("Failed to open output file");
        gzclose(gz_input);
        return 1;
    }

    FILE *output_log = fopen(argv[3], "w");
    if (!output_log) {
        perror("Failed to open error log file");
        gzclose(gz_input);
        return 1;
    }

    char *buffer = (char*) malloc(BUFFER_SIZE);
    gzread(gz_input, buffer, BUFFER_SIZE);
    parse_xcactivitylog(buffer, output, output_log);

    gzclose(gz_input);
    fclose(output);
    fclose(output_log);

    return 0;
}
