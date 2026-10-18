#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080
#define BUFSIZE 4096

/* Answers every request with a short greeting. */
static void handle(int client) {
    char buf[BUFSIZE];
    ssize_t n = recv(client, buf, sizeof buf - 1, 0);
    if (n <= 0)
        return;
    buf[n] = '\0';
    const char *reply = "HTTP/1.0 200 OK\r\n\r\nHello from Edith!\n";
    send(client, reply, strlen(reply), 0);
}

int main(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(PORT)};
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind");
        return 1;
    }
    listen(fd, 16);
    for (;;) {
        int client = accept(fd, NULL, NULL);
        handle(client);
    }
}
