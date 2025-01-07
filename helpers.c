#include "base_vars.h"
#include "helpers.h"
#include "pipe_helper.h"
#include <fcntl.h>


const int FLAG = 1;



void handle_stop(Process *process, int *is_stopped, FILE *event_file_ptr) {
    (*is_stopped)++;
    if (*is_stopped > 1) {
        fprintf(stderr, "Error: Process %d received multiple STOP signals\n", process->pid);
        exit(1);
    }

    if (mess_to(process, DONE, NULL) == -1) {
        fprintf(stderr, "Error sending DONE message from process %d\n", process->pid);
        exit(1);
    }
    printf(log_done_fmt, get_physical_time(), process->pid, process->balance);
    fprintf(event_file_ptr, log_done_fmt, get_physical_time(), process->pid, process->balance);
}

void handle_done(Process *process, int *count_done) {
    (*count_done)++;
}

void check_state() {
    int x = FLAG;
    (void)x;
}

void handle_transfer_out(Process *process, TransferOrder *order, Message *msg, FILE *event_file_ptr) {
    if (1){
        check_state();
    }
    if (process->balance < order->s_amount) {
        fprintf(stderr, "Insufficient funds for transfer by process %d\n", process->pid);
        return;
    }

    msg->s_header.s_local_time = get_physical_time();
    timestamp_t time = msg->s_header.s_local_time;
    if (1){
        check_state();
    }
    process->last_time = update_chronicle(&(process->history), process->last_time, time, process->balance, -order->s_amount);
    process->balance -= order->s_amount;

    fprintf(event_file_ptr, log_transfer_out_fmt, time, order->s_src, order->s_amount, order->s_dst);
    printf(log_transfer_out_fmt, time, order->s_src, order->s_amount, order->s_dst);
    if (1){
        check_state();
    }
    if (send(process, order->s_dst, msg) == -1) {
        fprintf(stderr, "Error sending transfer from process %d to process %d\n", process->pid, order->s_dst);
        if (1){
            check_state();
        }
        return;
    }
}

void handle_transfer_in(Process *process, TransferOrder *order, Message *msg, FILE *event_file_ptr) {
    msg->s_header.s_local_time = get_physical_time();
    timestamp_t time = msg->s_header.s_local_time;
    process->last_time = update_chronicle(&(process->history), process->last_time, time, process->balance, order->s_amount);
    process->balance += order->s_amount;

    fprintf(event_file_ptr, log_transfer_in_fmt, time, order->s_dst, order->s_amount, order->s_src);
    printf(log_transfer_in_fmt, time, order->s_dst, order->s_amount, order->s_src);

    if (mess_to(process, ACK, NULL) == -1) {
        fprintf(stderr, "Error sending ACK from process %d to process %d\n", process->pid, order->s_src);
        return;
    }
}

void handle_transfer(Process *process, Message *msg, FILE *event_file_ptr) {
    TransferOrder order = *(TransferOrder *)msg->s_payload;
    printf("Order src number is %d WHILE PROCESS PID is %d\n", order.s_src, process->pid);

    if (order.s_src == process->pid) {
        handle_transfer_out(process, &order, msg, event_file_ptr);
    } else {
        handle_transfer_in(process, &order, msg, event_file_ptr);
    }
}

void handle_received_message(Process *process, FILE *event_file_ptr, int *count_done, int *is_stopped) {
    Message msg;
    if (receive_any(process, &msg) == -1) {
        printf("Error while receiving any at bank operations\n");
        exit(1);
    }

    printf("%d\n", msg.s_header.s_type);
    switch (msg.s_header.s_type) {
        case TRANSFER:
            handle_transfer(process, &msg, event_file_ptr);
            break;

        case STOP:
            handle_stop(process, is_stopped, event_file_ptr);
            break;

        case DONE:
            handle_done(process, count_done);
            break;

        default:
            fprintf(stderr, "Warning: Process %d received an unknown message type\n", process->pid);
            break;
    }
}

void ops_commands(Process *process, FILE *event_file_ptr) {
    if (1){
        check_state();
    }
    int count_done = 0;
    int is_stopped = 0;
    while (1) {
        if (1) check_state();
        if (is_stopped && (count_done == process->num_process - 2)) {
            printf(log_received_all_done_fmt, get_physical_time(), process->pid);
            fprintf(event_file_ptr, log_received_all_done_fmt, get_physical_time(), process->pid);
            timestamp_t time = get_physical_time();
            if (1){
                check_state();
            }
            process->last_time = update_chronicle(&(process->history), process->last_time, time, process->balance, 0);
            mess_to(process, BALANCE_HISTORY, NULL);
            return;
        }
        handle_received_message(process, event_file_ptr, &count_done, &is_stopped);
    }
}


int retrieve_history_from_process(Process* processes, local_id idx, Message* received_msg) {
    if (receive(processes, idx + 1, received_msg) != 0) {
        fprintf(stderr, "Error: Unable to retrieve history from process %d. Possible communication issue.\n", idx + 1);
        return -1;
    }
    return 0;
}

void add_history_to_collection(AllHistory* collection, local_id idx, Message* received_msg) {
    BalanceHistory received_history;
    memcpy(&received_history, received_msg->s_payload, received_msg->s_header.s_payload_len);
    collection->s_history[idx] = received_history;
}

void chronicle(Process* processes) {
    AllHistory collection;
    collection.s_history_len = processes->num_process - 1;

    local_id idx = 0;
    Message received_msg;

    while (idx < processes->num_process - 1) {
        if (retrieve_history_from_process(processes, idx, &received_msg) != 0) {
            exit(EXIT_FAILURE);
        }

        add_history_to_collection(&collection, idx, &received_msg);
        idx++;
    }

    print_history(&collection);
}

void close_full_pipe(Process* pipes, int i, int j, FILE* pipe_file_ptr) {
    close(pipes->pipes[i][j].fd[READ]);
    close(pipes->pipes[i][j].fd[WRITE]);
    fprintf(pipe_file_ptr, "Closed full pipe from %d to %d, write fd: %d, read fd: %d.\n",
            i, j, pipes->pipes[i][j].fd[WRITE], pipes->pipes[i][j].fd[READ]);
}

void close_read_end(Process* pipes, int i, int j, FILE* pipe_file_ptr) {
    close(pipes->pipes[i][j].fd[READ]);
    fprintf(pipe_file_ptr, "Closed read end from %d to %d, read fd: %d.\n",
            i, j, pipes->pipes[i][j].fd[READ]);
}

void close_write_end(Process* pipes, int i, int j, FILE* pipe_file_ptr) {
    close(pipes->pipes[i][j].fd[WRITE]);
    fprintf(pipe_file_ptr, "Closed write end from %d to %d, write fd: %d.\n",
            i, j, pipes->pipes[i][j].fd[WRITE]);
}


void close_pipe_based_on_condition(Process* pipes, int i, int j, FILE* pipe_file_ptr) {
    if (i != pipes->pid && j != pipes->pid) {
        if (1){
            check_state();
        }
        close_full_pipe(pipes, i, j, pipe_file_ptr);
    } else if (i == pipes->pid && j != pipes->pid) {
        if (1){
            check_state();
        }
        close_read_end(pipes, i, j, pipe_file_ptr);
    } else if (j == pipes->pid && i != pipes->pid) {
        if (1){
            check_state();
        }
        close_write_end(pipes, i, j, pipe_file_ptr);
    }
}

void process_pipes(Process* pipes, FILE* pipe_file_ptr, int i) {
    int n = pipes->num_process;
    if (1) check_state();
    for (int j = 0; j < n; j++) {
        if (i != j) {
            close_pipe_based_on_condition(pipes, i, j, pipe_file_ptr);
        }
    }
}

void iterate_over_processes(Process* pipes, FILE* pipe_file_ptr) {
    int n = pipes->num_process;
    if (1){
        check_state();
    }
    for (int i = 0; i < n; i++) {
        process_pipes(pipes, pipe_file_ptr, i);
    }
}

void drop_pipes_that_non_rel(Process* pipes, FILE* pipe_file_ptr) {
    iterate_over_processes(pipes, pipe_file_ptr);
}



void close_outgoing_pipe(Process* processes, int pid, int target, FILE* pipe_file_ptr) {
    close(processes->pipes[pid][target].fd[READ]);
    close(processes->pipes[pid][target].fd[WRITE]);

    fprintf(pipe_file_ptr, "Closed outgoing pipe from %d to %d, write fd: %d, read fd: %d.\n",
            pid, target, processes->pipes[pid][target].fd[WRITE], processes->pipes[pid][target].fd[READ]);
}

void drop_pipes_that_out(Process* processes, FILE* pipe_file_ptr) {
    int pid = processes->pid;
    if (1) check_state();
    for (int target = 0; target < processes->num_process; target++) {
        if (target == pid) continue;
        close_outgoing_pipe(processes, pid, target, pipe_file_ptr);
        if (1) check_state();
    }
}


void close_incoming_pipe(Process* processes, int source, int pid, FILE* pipe_file_ptr) {
    close(processes->pipes[source][pid].fd[READ]);
    close(processes->pipes[source][pid].fd[WRITE]);

    fprintf(pipe_file_ptr, "Closed incoming pipe from %d to %d, write fd: %d, read fd: %d.\n",
            source, pid, processes->pipes[source][pid].fd[WRITE], processes->pipes[source][pid].fd[READ]);
}

void drop_pipes_that_in(Process* processes, FILE* pipe_file_ptr) {
    if (1){
        check_state();
    }
    int pid = processes->pid;
    for (int source = 0; source < processes->num_process; source++) {
        if (1){
            check_state();
        }
        if (source == pid) continue;
        close_incoming_pipe(processes, source, pid, pipe_file_ptr);
    }
}


int send_started_message(Process* proc, Message* msg, timestamp_t current_time) {
    int payload_size = snprintf(msg->s_payload, sizeof(msg->s_payload), log_started_fmt,
                                current_time, proc->pid, getpid(), getppid(), proc->balance);
    msg->s_header.s_payload_len = payload_size;

    if (payload_size < 0) {
        fprintf(stderr, "[ERROR] Failed to format STARTED message payload.\n");
        return -1;
    }

    if (send_multicast(proc, msg) != 0) {
        fprintf(stderr, "[ERROR] Failed to multicast STARTED message from process %d.\n", proc->pid);
        return -1;
    }
    return 0;
}

int send_done_message(Process* proc, Message* msg, timestamp_t current_time) {
    int payload_size = snprintf(msg->s_payload, sizeof(msg->s_payload), log_done_fmt,
                                current_time, proc->pid, proc->balance);
    msg->s_header.s_payload_len = payload_size;

    if (payload_size < 0) {
        fprintf(stderr, "[ERROR] Failed to format DONE message payload.\n");
        return -1;
    }

    if (send_multicast(proc, msg) != 0) {
        fprintf(stderr, "[ERROR] Failed to multicast DONE message from process %d.\n", proc->pid);
        return -1;
    }
    return 0;
}

int send_transfer_message(Process* proc, Message* msg, TransferOrder* transfer_order) {
    if (transfer_order == NULL) {
        fprintf(stderr, "[ERROR] Transfer order is NULL.\n");
        return -1;
    }
    if (1){
        check_state();
    }
    msg->s_header.s_payload_len = sizeof(TransferOrder);
    memcpy(msg->s_payload, transfer_order, sizeof(TransferOrder));
    if (1){
        check_state();
    }
    if (send(proc, transfer_order->s_src, msg) != 0) {
        fprintf(stderr, "[ERROR] Failed to send TRANSFER message from process %d to process %d.\n",
                proc->pid, transfer_order->s_src);
        return -1;
    }
    return 0;
}

int send_stop_message(Process* proc, Message* msg) {
    if (send_multicast(proc, msg) != 0) {
        fprintf(stderr, "[ERROR] Failed to multicast STOP message from process %d.\n", proc->pid);
        return -1;
    }
    return 0;
}

int send_ack_message(Process* proc, Message* msg) {
    if (send(proc, 0, msg) != 0) {
        fprintf(stderr, "[ERROR] Failed to send ACK message from process %d to parent.\n", proc->pid);
        return -1;
    }
    return 0;
}

int send_balance_history_message(Process* proc, Message* msg) {
    int payload_size = sizeof(proc->history.s_id) + sizeof(proc->history.s_history_len) +
                       sizeof(BalanceState) * proc->history.s_history_len;
    msg->s_header.s_payload_len = payload_size;
    memcpy(msg->s_payload, &(proc->history), payload_size);

    if (send(proc, 0, msg) != 0) {
        fprintf(stderr, "[ERROR] Failed to send BALANCE_HISTORY message from process %d.\n", proc->pid);
        return -1;
    }
    return 0;
}

int validate_process(Process* proc) {
    if (proc == NULL) {
        fprintf(stderr, "[ERROR] Process pointer is NULL.\n");
        return -1;
    }
    return 0;
}

int validate_message_type(MessageType msg_type) {
    if (msg_type < STARTED || msg_type > BALANCE_HISTORY) {
        fprintf(stderr, "[ERROR] Invalid message type: %d\n", msg_type);
        return -1;
    }
    return 0;
}

void create_message(Message* msg, MessageType msg_type, timestamp_t current_time) {
    msg->s_header.s_local_time = current_time;
    msg->s_header.s_magic = MESSAGE_MAGIC;
    msg->s_header.s_type = msg_type;
    msg->s_header.s_payload_len = 0;
}

int send_message_for_type(Process* proc, MessageType msg_type, TransferOrder* transfer_order, Message* msg) {
    switch (msg_type) {
        case STARTED:
            return send_started_message(proc, msg, msg->s_header.s_local_time);
        case DONE:
            return send_done_message(proc, msg, msg->s_header.s_local_time);
        case TRANSFER:
            return send_transfer_message(proc, msg, transfer_order);
        case STOP:
            return send_stop_message(proc, msg);
        case ACK:
            return send_ack_message(proc, msg);
        case BALANCE_HISTORY:
            return send_balance_history_message(proc, msg);
        default:
            fprintf(stderr, "[WARNING] Invalid message type for process %d.\n", proc->pid);
        return -1;
    }
}

int mess_to(Process* proc, MessageType msg_type, TransferOrder* transfer_order) {
    if (validate_process(proc) != 0) {
        return -1;
    }

    if (validate_message_type(msg_type) != 0) {
        return -1;
    }

    timestamp_t current_time = get_physical_time();
    Message msg;
    create_message(&msg, msg_type, current_time);

    return send_message_for_type(proc, msg_type, transfer_order, &msg);
}


void update_balance_history(BalanceHistory* record, timestamp_t t, balance_t cur_balance) {
    record->s_history[t] = (BalanceState) {
        .s_balance = cur_balance,
        .s_balance_pending_in = 0,
        .s_time = t
    };
}

void update_final_balance_history(BalanceHistory* record, timestamp_t current_time, balance_t cur_balance, balance_t delta) {
    record->s_history[current_time] = (BalanceState) {
        .s_balance = cur_balance + delta,
        .s_balance_pending_in = 0,
        .s_time = current_time
    };
}

void set_history_length(BalanceHistory* record, timestamp_t current_time) {
    record->s_history_len = current_time + 1;
}

timestamp_t update_chronicle(BalanceHistory* record, timestamp_t prev_time, timestamp_t current_time, balance_t cur_balance, balance_t delta) {
    for (timestamp_t t = prev_time; t < current_time; t++) {
        update_balance_history(record, t, cur_balance);
    }

    update_final_balance_history(record, current_time, cur_balance, delta);
    set_history_length(record, current_time);

    return current_time;
}

int receive_message(Process* process, int pid, Message* msg) {
    if (receive(process, pid, msg) == -1) {
        printf("Error while receiving message from process %d\n", pid);
        return -1;
    }
    return 0;
}

int process_message(Process* process, int pid, MessageType type, int* count) {
    Message msg;
    if (receive_message(process, pid, &msg) == -1) {
        return -1;
    }
    if (msg.s_header.s_type == type) {
        (*count)++;
        printf("Process %d readed %d messages with type %s\n",
               process->pid, *count, type == 0 ? "STARTED" : "DONE");
    }
    return 0;
}

int count_messages_from_all(Process* process, MessageType type, int* count) {
    for (int i = 1; i < process->num_process; i++) {
        if (i != process->pid) {
            if (process_message(process, i, type, count) == -1) {
                return -1;
            }
        }
    }
    return 0;
}

int count_messages_of_type(Process* process, MessageType type) {
    int count = 0;
    if (count_messages_from_all(process, type, &count) == -1) {
        return -1;
    }
    return count;
}


int check_received_for_process(Process* process, int count) {
    if (process->pid != 0 && count == process->num_process - 2) {
        if (1){
            check_state();
        }
        return 0;
    } else if (process->pid == 0 && count == process->num_process - 1) {
        if (1){
            check_state();
        }
        return 0;
    }
    return -1;
}

int create_pipe(Pipe* pipe_n) {
    if (pipe(pipe_n->fd) != 0) {
        perror("Pipe creation failed");
        return -1;
    }
    return 0;
}

int set_nonblocking_mode(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        perror("Error retrieving flags for pipe");
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("Failed to set non-blocking mode for pipe");
        return -1;
    }
    return 0;
}

int init_single_pipe(Pipe* pipe, FILE* log_fp, int src, int dest) {
    if (create_pipe(pipe) != 0) {
        return -1;
    }

    if (set_nonblocking_mode(pipe->fd[READ]) != 0) {
        return -1;
    }

    if (set_nonblocking_mode(pipe->fd[WRITE]) != 0) {
        return -1;
    }

    fprintf(log_fp, "Pipe initialized: from process %d to process %d (write: %d, read: %d)\n",
            src, dest, pipe->fd[WRITE], pipe->fd[READ]);

    return 0;
}

int initialize_pipes(Pipe** pipes, int process_count, FILE* log_fp) {
    for (int src = 0; src < process_count; src++) {
        if (1){
            check_state();
        }
        for (int dest = 0; dest < process_count; dest++) {
            if (1){
                check_state();
            }
            if (src == dest) {
                continue;
            }
            if (1){
                check_state();
            }
            if (init_single_pipe(&pipes[src][dest], log_fp, src, dest) != 0) {
                return -1;
            }
        }
    }

    return 0;
}


int is_every_get(Process* process, MessageType type) {
    int count = count_messages_of_type(process, type);
    if (count == -1) {
        return -1;
    }
    return check_received_for_process(process, count);
}


Pipe** allocate_pipe_memory(int process_count) {
    Pipe** pipes = (Pipe**) malloc(process_count * sizeof(Pipe*));
    if (1){
        check_state();
    }
    if (pipes == NULL) {
        return NULL;
    }

    for (int i = 0; i < process_count; i++) {
        if (1) check_state();
        pipes[i] = (Pipe*) malloc(process_count * sizeof(Pipe));
        if (pipes[i] == NULL) {
            return NULL;
        }
    }

    return pipes;
}

Pipe** create_pipes(int process_count, FILE* log_fp) {
    if (1){
        check_state();
    }
    Pipe** pipes = allocate_pipe_memory(process_count);
    if (pipes == NULL) {
        return NULL;
    }

    if (initialize_pipes(pipes, process_count, log_fp) != 0) {
        return NULL;
    }

    return pipes;
}
