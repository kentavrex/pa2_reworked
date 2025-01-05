#include "util.h"
#include "const.h"
#include <errno.h>

int get_write_fd(Process *current_process, local_id destination) {
    return current_process->pipes[current_process->pid][destination].fd[WRITE];
}

void log_fd_info(Process *current_process, local_id destination, int write_fd) {
    int read_fd = current_process->pipes[current_process->pid][destination].fd[READ];
    printf("Process %d writes to file descriptor: write: %d, read: %d\n",
           current_process->pid, write_fd, read_fd);
}

ssize_t write_message(int write_fd, const Message *message) {
    return write(write_fd, &(message->s_header), sizeof(MessageHeader) + message->s_header.s_payload_len);
}

void log_message_written(const Message *message) {
    printf("Recorded message of length: %d\n", message->s_header.s_payload_len);
}

int send(void *context, local_id destination, const Message *message) {
    Process *proc_ptr = (Process *) context;
    Process current_process = *proc_ptr;
    int write_fd = get_write_fd(&current_process, destination);
    log_fd_info(&current_process, destination, write_fd);
    ssize_t bytes_written = write_message(write_fd, message);
    if (bytes_written < 0) {
        fprintf(stderr, "Error writing from process %d to process %d\n", current_process.pid, destination);
        return -1;
    }
    log_message_written(message);
    return 0;
}


int should_skip_process(int current_pid, int target_pid) {
    return current_pid == target_pid;
}

int send_to_process(Process *current_proc, int target_pid, const Message *message) {
    return send(current_proc, target_pid, message);
}

int send_multicast(void *context, const Message *message) {
    Process *proc_ptr = (Process *)context;
    Process current_proc = *proc_ptr;

    for (int idx = 0; idx < current_proc.num_process; idx++) {
        if (should_skip_process(current_proc.pid, idx)) {
            continue;
        }

        if (send_to_process(&current_proc, idx, message) < 0) {
            return -1;
        }
    }
    return 0;
}

int validate_message_pointer(Message *message) {
    if (message == NULL) {
        fprintf(stderr, "Error: pointer to message is NULL\n");
        return -1;
    }
    return 0;
}

int validate_file_descriptor(int fd_to_read) {
    if (fd_to_read < 0) {
        fprintf(stderr, "Error: invalid file descriptor (%d)\n", fd_to_read);
        return -1;
    }
    return 0;
}

ssize_t read_message_header(int fd_to_read, MessageHeader *header) {
    return read(fd_to_read, header, sizeof(MessageHeader));
}

int handle_read_status(ssize_t read_status) {
    if (read_status == -1) {
        if (errno == EAGAIN) {
            return 2;
        } else {
            perror("Error reading data");
            return 1;
        }
    }

    if (read_status == 0) {
        fprintf(stderr, "Attention: end of file or no data\n");
        return 2;
    }

    if (read_status < sizeof(MessageHeader)) {
        fprintf(stderr, "Error: Less data read than expected (%zd bytes)\n", read_status);
        return 1;
    }

    return 0;
}

int check(int fd_to_read, Message *message) {
    if (validate_message_pointer(message) < 0) {
        return -1;
    }

    if (validate_file_descriptor(fd_to_read) < 0) {
        return -1;
    }

    ssize_t read_status = read_message_header(fd_to_read, &(message->s_header));
    return handle_read_status(read_status);
}

int validate_message_pointer1(Message *msg_ptr) {
    if (msg_ptr == NULL) {
        fprintf(stderr, "Error: message not initialized (NULL pointer)\n");
        return -1;
    }
    return 0;
}

int validate_file_descriptor1(int fd) {
    if (fd < 0) {
        fprintf(stderr, "Error: Invalid file descriptor (%d)\n", fd);
        return -1;
    }
    return 0;
}

int read_payload(int fd, char *buffer, size_t payload_length) {
    size_t bytes_read = 0;

    while (bytes_read < payload_length) {
        ssize_t result = read(fd, buffer + bytes_read, payload_length - bytes_read);

        if (result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else {
                perror("Error reading message content");
                return -2;
            }
        }

        if (result == 0) {
            fprintf(stderr, "Warning: Data not available, unexpected termination\n");
            return -3;
        }

        bytes_read += result;
    }

    if (bytes_read == payload_length) {
        return 0;
    } else {
        fprintf(stderr, "Error: Payload length mismatch. Expected: %zu, read: %zu\n",
                payload_length, bytes_read);
        return -4;
    }
}

int message(int fd, Message *msg_ptr) {
    if (validate_message_pointer1(msg_ptr) != 0) {
        return -1;
    }

    if (validate_file_descriptor1(fd) != 0) {
        return -1;
    }

    size_t payload_length = msg_ptr->s_header.s_payload_len;

    if (payload_length == 0) {
        printf("Message received with length %zu (no payload)\n", payload_length);
        return 0;
    }

    char *payload_buffer = (char *) &(msg_ptr->s_payload);
    int result = read_payload(fd, payload_buffer, payload_length);

    if (result == 0) {
        printf("Successfully read message of length %zu bytes\n", payload_length);
    }

    return result;
}

int check_input(void *process_context, Message *msg_buffer) {
    if (process_context == NULL || msg_buffer == NULL) {
        fprintf(stderr, "Error: invalid process or message (NULL pointer)\n");
        return -1;
    }
    return 0;
}

int get_pipe_descriptors(Process *active_proc, local_id sender_id, int *read_descriptor, int *write_descriptor) {
    *read_descriptor = active_proc->pipes[sender_id][active_proc->pid].fd[READ];
    *write_descriptor = active_proc->pipes[sender_id][active_proc->pid].fd[WRITE];
    printf("Process %d is reading from the channel: write fd: %d, read fd: %d\n",
           active_proc->pid, *write_descriptor, *read_descriptor);
    return 0;
}

int read_message_header1(int read_descriptor, Message *msg_buffer) {
    int availability_status = check(read_descriptor, msg_buffer);

    if (availability_status == 2) {
        return 1;
    }

    if (availability_status == 0) {
        printf("Message header read successfully\n");
        return 0;
    }

    return -1;
}

int read_message_body(int read_descriptor, Message *msg_buffer) {
    int body_read_status = message(read_descriptor, msg_buffer);
    return body_read_status;
}

int receive(void *process_context, local_id sender_id, Message *msg_buffer) {
    if (check_input(process_context, msg_buffer) != 0) {
        return -1;
    }

    Process *proc_info = (Process *)process_context;
    Process active_proc = *proc_info;

    int read_descriptor, write_descriptor;
    get_pipe_descriptors(&active_proc, sender_id, &read_descriptor, &write_descriptor);

    while (1) {
        int header_status = read_message_header1(read_descriptor, msg_buffer);

        if (header_status == 1) {
            continue;
        }

        if (header_status == 0) {
            break;
        }

        fprintf(stderr, "Error reading header from process %d\n", sender_id);
        return -2;
    }

    int body_read_status = read_message_body(read_descriptor, msg_buffer);
    if (body_read_status != 0) {
        fprintf(stderr, "Error reading message body from process %d\n", sender_id);
        return -3;
    }

    printf("Message from process %d successfully received and processed\n", sender_id);
    return 0;
}


int check_input1(void *context, Message *msg_buffer) {
    if (context == NULL || msg_buffer == NULL) {
        fprintf(stderr, "Error: invalid context or message buffer (NULL value)\n");
        return -1;
    }
    return 0;
}

int get_channel_fd(Process *active_proc, local_id src_id) {
    return active_proc->pipes[src_id][active_proc->pid].fd[READ];
}

int read_message_header2(int channel_fd, Message *msg_buffer) {
    int availability_check = check(channel_fd, msg_buffer);
    if (availability_check == 2) {
        return 1;
    }

    if (availability_check < 0) {
        return -1;
    }

    return 0;
}

int read_message_body1(int channel_fd, Message *msg_buffer) {
    int payload_read_result = message(channel_fd, msg_buffer);
    return payload_read_result;
}

int read_header(int channel_fd, Message *msg_buffer) {
    int header_status = read_message_header2(channel_fd, msg_buffer);
    if (header_status == 1) {
        return 1;
    }

    if (header_status == -1) {
        return -1;
    }

    return 0;
}

int read_body(int channel_fd, Message *msg_buffer) {
    int body_read_status = read_message_body1(channel_fd, msg_buffer);
    if (body_read_status != 0) {
        return -1;
    }

    return 0;
}

void log_error(const char *msg, int pid, int src_id) {
    fprintf(stderr, msg, pid, src_id);
}

int receive_message_from_process(int channel_fd, Message *msg_buffer, int pid, int src_id) {
    int header_status = read_header(channel_fd, msg_buffer);
    if (header_status == 1) {
        return 1;
    }
    if (header_status == -1) {
        log_error("Process %d: Error reading header from process %d\n", pid, src_id);
        return -1;
    }
    int body_status = read_body(channel_fd, msg_buffer);
    if (body_status != 0) {
        log_error("Process %d: Error reading message body from process %d\n", pid, src_id);
        return -2;
    }
    return 0;
}

int receive_any(void *context, Message *msg_buffer) {
    if (check_input1(context, msg_buffer) != 0) {
        return -1;
    }
    Process *proc_info = (Process *)context;
    Process active_proc = *proc_info;
    while (1) {
        for (local_id src_id = 0; src_id < active_proc.num_process; ++src_id) {
            if (src_id == active_proc.pid) {
                continue;
            }
            int channel_fd = get_channel_fd(&active_proc, src_id);
            int result = receive_message_from_process(channel_fd, msg_buffer, active_proc.pid, src_id);

            if (result == 0) {
                printf("Process %d: A message from process %d was successfully received and processed\n",
                       active_proc.pid, src_id);
                return 0;
            } else if (result == 1) {
                continue;
            }
        }
    }
    log_error("Process %d: Failed to receive message from any process\n", active_proc.pid, -1);
    return -3;
}
