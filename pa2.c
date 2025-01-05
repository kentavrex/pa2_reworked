#include <sys/types.h>
#include <sys/wait.h>
#include <asm-generic/errno.h>

#include "util.h"
#include "common.h"
#include "pipes_manager.h"

void validate_arguments(int argc, char *argv[], int *num_processes) {
    if (argc < 3 || strcmp("-p", argv[1]) != 0) {
        fprintf(stderr, "Usage: -p X\n");
        exit(1);
    }

    *num_processes = atoi(argv[2]);
    if (*num_processes < 1 || *num_processes > 10) {
        fprintf(stderr, "Process count should be between 1 and 10\n");
        exit(1);
    }
    *num_processes += 1; // Include parent process
}

FILE *open_log_file(const char *filename) {
    FILE *log_file = fopen(filename, "w+");
    if (!log_file) {
        perror("Failed to open log file");
        exit(1);
    }
    return log_file;
}

void parse_initial_balances(int argc, char *argv[], int num_processes, int *balances) {
    if (argc < num_processes + 2) {
        fprintf(stderr, "Provide initial balance values for each process\n");
        exit(1);
    }

    for (int i = 3; i < 3 + num_processes - 1; ++i) {
        int balance = atoi(argv[i]);
        if (balance < 1 || balance > 99) {
            fprintf(stderr, "Invalid balance at argument %d\n", i);
            exit(1);
        }
        balances[i - 3] = balance;
    }
}

void create_child_processes(int num_processes, int *balances, Pipe **pipes, FILE *log_pipes, FILE *log_events) {
    for (local_id i = 1; i < num_processes; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        }
        if (pid == 0) {
            Process child_proc = {
                .num_process = num_processes,
                .pipes = pipes,
                .pid = i,
                .balance = balances[i - 1],
                .history = {.s_id = i, .s_history_len = 0},
                .last_time = get_physical_time()
            };

            close_non_related_pipes(&child_proc, log_pipes);
            send_message(&child_proc, STARTED, NULL);

            fprintf(log_events, log_started_fmt, get_physical_time(), i, getpid(), getppid(), child_proc.balance);
            if (check_all_received(&child_proc, STARTED) != 0) {
                fprintf(stderr, "Error: Process %d failed to receive all STARTED messages\n", i);
                exit(EXIT_FAILURE);
            }
            fprintf(log_events, log_received_all_started_fmt, get_physical_time(), i);

            bank_operations(&child_proc, log_events);

            close_outcoming_pipes(&child_proc, log_pipes);
            exit(EXIT_SUCCESS);
        }
    }
}

void handle_parent_process(int num_processes, Pipe **pipes, FILE *log_pipes, FILE *log_events) {
    Process parent_proc = {
        .num_process = num_processes,
        .pipes = pipes,
        .pid = PARENT_ID
    };
    close_non_related_pipes(&parent_proc, log_pipes);

    if (check_all_received(&parent_proc, STARTED) != 0) {
        fprintf(stderr, "Error: Parent process failed to receive all STARTED messages\n");
        fclose(log_pipes);
        fclose(log_events);
        exit(EXIT_FAILURE);
    }
    fprintf(log_events, log_received_all_started_fmt, get_physical_time(), PARENT_ID);

    bank_robbery(&parent_proc, num_processes - 1);
    send_message(&parent_proc, STOP, NULL);

    if (check_all_received(&parent_proc, DONE) != 0) {
        fprintf(stderr, "Error: Parent process failed to receive all DONE messages\n");
        fclose(log_pipes);
        fclose(log_events);
        exit(EXIT_FAILURE);
    }
    fprintf(log_events, log_received_all_done_fmt, get_physical_time(), PARENT_ID);

    histories(&parent_proc);
    close_outcoming_pipes(&parent_proc, log_pipes);
    while (wait(NULL) > 0);
}

int main(int argc, char *argv[]) {
    int num_processes;
    validate_arguments(argc, argv, &num_processes);

    FILE *log_pipes = open_log_file("pipes.log");
    FILE *log_events = open_log_file("events.log");

    int balances[num_processes - 1];
    parse_initial_balances(argc, argv, num_processes, balances);

    Pipe **pipes = init_pipes(num_processes, log_pipes);

    create_child_processes(num_processes, balances, pipes, log_pipes, log_events);

    handle_parent_process(num_processes, pipes, log_pipes, log_events);

    fclose(log_pipes);
    fclose(log_events);
    return 0;
}
