#include "util.h"
#include "const.h"
#include <errno.h>

int send(void *context, local_id destination, const Message *message) {
    Process *proc_ptr = (Process *) context;
    Process current_process = *proc_ptr;
    
    int write_fd = current_process.pipes[current_process.pid][destination].fd[WRITE];
    printf("Process %d writes to file descriptor: write: %d, read: %d\n",
           current_process.pid, write_fd, current_process.pipes[current_process.pid][destination].fd[READ]);
    
    ssize_t bytes_written = write(write_fd, &(message->s_header), sizeof(MessageHeader) + message->s_header.s_payload_len);
    if (bytes_written < 0) {
        fprintf(stderr, "Error writing from process %d to process %d\n", current_process.pid, destination);
        return -1;
    }
    
    printf("Recorded message of length: %d\n", message->s_header.s_payload_len);
    return 0;
}
int send_multicast(void *context, const Message *message) {
    Process *proc_ptr = (Process *) context;
    Process current_proc = *proc_ptr;
    
    for (int idx = 0; idx < current_proc.num_process; idx++) {
        if (idx == current_proc.pid) {
            continue;
        }
        
        if (send(&current_proc, idx, message) < 0) {
            fprintf(stderr, "Error when multicasting from process %d to process %d\n", current_proc.pid, idx);
            return -1;
        }
    }
    return 0;
}


int check(int fd_to_read, Message *message) {
    if (message == NULL) {
        fprintf(stderr, "Error: pointer to message is NULL\n");
        return -1;
    }

    if (fd_to_read < 0) {
        fprintf(stderr, "Error: invalid file descriptor (%d)\n", fd_to_read);
        return -1;
    }

    ssize_t read_status = read(fd_to_read, &(message->s_header), sizeof(MessageHeader));

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


int message(int fd, Message *msg_ptr) {
    if (msg_ptr == NULL) {
        fprintf(stderr, "Error: message not initialized (NULL pointer)\n");
        return -1;
    }

    if (fd < 0) {
        fprintf(stderr, "Error: Invalid file descriptor (%d)\n", fd);
        return -1;
    }

    size_t payload_length = msg_ptr->s_header.s_payload_len;
    if (payload_length == 0) {
        printf("Message received with length %zu (no payload)\n", payload_length);
        return 0;
    }

    size_t bytes_read = 0;
    char *payload_buffer = (char *) &(msg_ptr->s_payload);

    while (bytes_read < payload_length) {
        ssize_t result = read(fd, payload_buffer + bytes_read, payload_length - bytes_read);

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
        printf("Successfully read message of length %zu bytes\n", payload_length);
        return 0;
    } else {
        fprintf(stderr, "Error: Payload length mismatch. Expected: %zu, read: %zu\n",
                payload_length, bytes_read);
        return -4;
    }
}

int receive(void *process_context, local_id sender_id, Message *msg_buffer) {
    if (process_context == NULL || msg_buffer == NULL) {
        fprintf(stderr, "Error: invalid process or message (NULL pointer)\n");
        return -1;
    }

    Process *proc_info = (Process *)process_context;
    Process active_proc = *proc_info;

    int read_descriptor = active_proc.pipes[sender_id][active_proc.pid].fd[READ];
    int write_descriptor = active_proc.pipes[sender_id][active_proc.pid].fd[WRITE];
    printf("Process %d is reading from the channel: write fd: %d, read fd: %d\n",
           active_proc.pid, write_descriptor, read_descriptor);

    while (1) {
        int availability_status = check(read_descriptor, msg_buffer);
        
        if (availability_status == 2) {
            continue;
        }

        if (availability_status == 0) {
            printf("Process %d: Message header read successfully\n", active_proc.pid);
            break;
        } 

        fprintf(stderr, "Process %d: error trying to read header from process %d\n", active_proc.pid, sender_id);
        return -2;
    }

    int body_read_status = message(read_descriptor, msg_buffer);
    if (body_read_status != 0) {
        fprintf(stderr, "Process %d: error reading message body from process %d\n", active_proc.pid, sender_id);
        return -3;
    }

    printf("Process %d: a message from process %d was successfully received and processed\n", active_proc.pid, sender_id);
    return 0;
}

int receive_any(void *context, Message *msg_buffer) {
    if (context == NULL || msg_buffer == NULL) {
        fprintf(stderr, "Error: invalid context or message buffer (NULL value)\n");
        return -1;
    }

    Process *proc_info = (Process *)context;
    Process active_proc = *proc_info;

    while (1) {
        for (local_id src_id = 0; src_id < active_proc.num_process; ++src_id) {
            if (src_id == active_proc.pid) {
                continue;
            }

            int channel_fd = active_proc.pipes[src_id][active_proc.pid].fd[READ];
            int availability_check = check(channel_fd, msg_buffer);

            if (availability_check == 2) {
                continue;
            }

            if (availability_check < 0) {
                fprintf(stderr, "Process %d: Error reading header from process %d\n",
                        active_proc.pid, src_id);
                return -2;
            }

            int payload_read_result = message(channel_fd, msg_buffer);
            if (payload_read_result != 0) {
                fprintf(stderr, "Process %d: error reading message body from process %d\n",
                        active_proc.pid, src_id);
                return -3;
            }

            printf("Process %d: a message from process %d was successfully received and processed\n",
                   active_proc.pid, src_id);
            return 0;
        }
    }

    fprintf(stderr, "Process %d: Failed to receive message from any process\n", active_proc.pid);
    return -4;
}
