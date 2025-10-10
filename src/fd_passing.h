#ifndef FD_PASSING_H
#define FD_PASSING_H

#include <sys/types.h>

// Send a file descriptor over a Unix domain socket
// Parameters:
//   sock - Unix domain socket file descriptor
//   fd_to_send - File descriptor to send
//   data - Optional metadata to send along with the FD
//   data_len - Length of metadata
// Returns:
//   0 on success, -1 on error
int sendfd(int sock, int fd_to_send, const void* data, size_t data_len);

// Receive a file descriptor over a Unix domain socket
// Parameters:
//   sock - Unix domain socket file descriptor
//   data_out - Buffer to receive metadata
//   data_size - Size of data buffer
//   received_len - Actual length of received data (output)
// Returns:
//   Received file descriptor on success, -1 on error
int recvfd(int sock, void* data_out, size_t data_size, ssize_t* received_len);

#endif

