// fd_passing.c
// File descriptor passing over Unix domain sockets using SCM_RIGHTS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "fd_passing.h"

// -------------------------------------------------------------------
// Send a file descriptor over a Unix domain socket
// -------------------------------------------------------------------
int sendfd(int sock, int fd_to_send, const void* data, size_t data_len) {
    struct msghdr msg = {0};
    struct iovec iov[1];
    char ctrl_buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr* cmsg;
    
    // Setup data to send (metadata)
    iov[0].iov_base = (void*)data;
    iov[0].iov_len = data_len;
    
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    
    // Setup control message to send FD
    msg.msg_control = ctrl_buf;
    msg.msg_controllen = sizeof(ctrl_buf);
    
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    
    // Copy the file descriptor to send
    memcpy(CMSG_DATA(cmsg), &fd_to_send, sizeof(int));
    
    // Send message with FD
    ssize_t sent = sendmsg(sock, &msg, 0);
    if (sent < 0) {
        perror("sendmsg");
        return -1;
    }
    
    return 0;
}

// -------------------------------------------------------------------
// Receive a file descriptor over a Unix domain socket
// -------------------------------------------------------------------
int recvfd(int sock, void* data_out, size_t data_size, ssize_t* received_len) {
    struct msghdr msg = {0};
    struct iovec iov[1];
    char ctrl_buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr* cmsg;
    
    // Setup buffer to receive data (metadata)
    iov[0].iov_base = data_out;
    iov[0].iov_len = data_size;
    
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    
    // Setup control buffer to receive FD
    msg.msg_control = ctrl_buf;
    msg.msg_controllen = sizeof(ctrl_buf);
    
    // Receive message with FD
    ssize_t n = recvmsg(sock, &msg, 0);
    if (n < 0) {
        perror("recvmsg");
        return -1;
    }
    
    if (received_len) {
        *received_len = n;
    }
    
    // Extract the file descriptor
    cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == NULL || cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
        fprintf(stderr, "[FD_PASSING] No valid control message received\n");
        return -1;
    }
    
    if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
        fprintf(stderr, "[FD_PASSING] Invalid control message type\n");
        return -1;
    }
    
    int received_fd;
    memcpy(&received_fd, CMSG_DATA(cmsg), sizeof(int));
    
    return received_fd;
}

