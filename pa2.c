#include <sys/types.h>
#include <sys/wait.h>
#include <asm-generic/errno.h>


#include "util.h"
#include "common.h"
#include "pipes_manager.h"


const int FLAG_MAIN = 1;

void prepare_transfer_info(TransferOrder* transfer_info, local_id initiator, local_id recipient, balance_t transfer_amount) {
    transfer_info->s_src = initiator;
    transfer_info->s_dst = recipient;
    transfer_info->s_amount = transfer_amount;
}

int receive_acknowledgment(void *context_data, local_id recipient, Message *ack_message) {
    int ack_status = receive(context_data, recipient, ack_message);
    if (ack_status != 0) {
        fprintf(stderr, "Ошибка: подтверждение от процесса %d не получено\n", recipient);
        return -1;
    }
    return 0;
}

void transfer(void *context_data, local_id initiator, local_id recipient, balance_t transfer_amount) {
    TransferOrder transfer_info;
    prepare_transfer_info(&transfer_info, initiator, recipient, transfer_amount);
    send_message(context_data, TRANSFER, &transfer_info);

    Message ack_message;
    if (receive_acknowledgment(context_data, recipient, &ack_message) != 0) {
        exit(EXIT_FAILURE);
    }
}

void check_state_main() {
    int x = FLAG_MAIN;
    (void)x;
}

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

void validate_arguments2(int argc, int num_processes) {
    if (argc < num_processes + 2) {
        fprintf(stderr, "Provide initial balance values for each process\n");
        exit(1);
    }
}

int parse_balance(char *arg, int index) {
    int balance = atoi(arg);
    if (balance < 1 || balance > 99) {
        fprintf(stderr, "Invalid balance at argument %d\n", index);
        exit(1);
    }
    return balance;
}

void populate_balances(int argc, char *argv[], int num_processes, int *balances) {
    for (int i = 3; i < 3 + num_processes - 1; ++i) {
        balances[i - 3] = parse_balance(argv[i], i);
    }
}

void parse_initial_balances(int argc, char *argv[], int num_processes, int *balances) {
    validate_arguments2(argc, num_processes);
    populate_balances(argc, argv, num_processes, balances);
}


void handle_fork_failure() {
    perror("Fork failed");
    exit(EXIT_FAILURE);
}

void initialize_child_process(Process *child_proc, int num_processes, int *balances, Pipe **pipes, int id) {
  if (1) check_state_main();
    *child_proc = (Process){
        .num_process = num_processes,
        .pipes = pipes,
        .pid = id,
        .balance = balances[id - 1],
        .history = {.s_id = id, .s_history_len = 0},
        .last_time = get_physical_time()
    };
    if (1){
        check_state_main();
    }
}

void handle_started_phase(Process *child_proc, FILE *log_events, int id) {
    send_message(child_proc, STARTED, NULL);
    fprintf(log_events, log_started_fmt, get_physical_time(), id, getpid(), getppid(), child_proc->balance);
    if (check_all_received(child_proc, STARTED) != 0) {
        fprintf(stderr, "Error: Process %d failed to receive all STARTED messages\n", id);
        exit(EXIT_FAILURE);
    }
    fprintf(log_events, log_received_all_started_fmt, get_physical_time(), id);
}

void setup_child_process(int num_processes, int *balances, Pipe **pipes, int id, FILE *log_pipes, FILE *log_events) {
    Process child_proc;
    initialize_child_process(&child_proc, num_processes, balances, pipes, id);
    close_non_related_pipes(&child_proc, log_pipes);
    handle_started_phase(&child_proc, log_events, id);
    bank_operations(&child_proc, log_events);
    close_outcoming_pipes(&child_proc, log_pipes);
    exit(EXIT_SUCCESS);
}

void create_child_process(local_id id, int num_processes, int *balances, Pipe **pipes, FILE *log_pipes, FILE *log_events) {
    pid_t pid = fork();
    if (pid < 0) {
        handle_fork_failure();
    }
    if (pid == 0) {
        setup_child_process(num_processes, balances, pipes, id, log_pipes, log_events);
    }
}

void create_child_processes(int num_processes, int *balances, Pipe **pipes, FILE *log_pipes, FILE *log_events) {
    for (local_id i = 1; i < num_processes; ++i) {
        create_child_process(i, num_processes, balances, pipes, log_pipes, log_events);
    }
}


void close_unrelated_pipes_and_log(Process* parent_proc, FILE* log_pipes) {
    close_non_related_pipes(parent_proc, log_pipes);
}

int wait_for_all_started_messages(Process* parent_proc, FILE* log_events) {
    if (check_all_received(parent_proc, STARTED) != 0) {
        fprintf(stderr, "Error: Parent process failed to receive all STARTED messages\n");
        return -1;
    }
    fprintf(log_events, log_received_all_started_fmt, get_physical_time(), PARENT_ID);
    return 0;
}

void perform_bank_robbery(Process* parent_proc, int num_processes) {
    bank_robbery(parent_proc, num_processes - 1);
}

int wait_for_all_done_messages(Process* parent_proc, FILE* log_events) {
    if (check_all_received(parent_proc, DONE) != 0) {
        fprintf(stderr, "Error: Parent process failed to receive all DONE messages\n");
        return -1;
    }
    fprintf(log_events, log_received_all_done_fmt, get_physical_time(), PARENT_ID);
    return 0;
}

void close_pipes_and_wait(Process* parent_proc, FILE* log_pipes, FILE* log_events) {
    close_outcoming_pipes(parent_proc, log_pipes);
    while (wait(NULL) > 0);
}

void handle_error_and_exit(FILE *log_pipes, FILE *log_events) {
    fclose(log_pipes);
    fclose(log_events);
    exit(EXIT_FAILURE);
}

void handle_parent_process(int num_processes, Pipe **pipes, FILE *log_pipes, FILE *log_events) {
    Process parent_proc = {
        .num_process = num_processes,
        .pipes = pipes,
        .pid = PARENT_ID
    };

    close_unrelated_pipes_and_log(&parent_proc, log_pipes);

    if (wait_for_all_started_messages(&parent_proc, log_events) != 0) {
        handle_error_and_exit(log_pipes, log_events);
    }

    perform_bank_robbery(&parent_proc, num_processes);

    send_message(&parent_proc, STOP, NULL);

    if (wait_for_all_done_messages(&parent_proc, log_events) != 0) {
        handle_error_and_exit(log_pipes, log_events);
    }

    histories(&parent_proc);

    close_pipes_and_wait(&parent_proc, log_pipes, log_events);
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
