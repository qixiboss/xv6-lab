#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int parent_to_child[2]; // 父到子管道
    int child_to_parent[2]; // 子到父管道

    // 创建两个管道
    if (pipe(parent_to_child) < 0 || pipe(child_to_parent) < 0) {
        fprintf(2, "pipe error\n");
        exit(1);
    }

    int pid = fork();
    if (pid < 0) {
        fprintf(2, "fork error\n");
        exit(1);
    }

    if (pid == 0) { // 子进程
        close(parent_to_child[1]); // 关闭父到子的写端
        close(child_to_parent[0]); // 关闭子到父的读端

        char buf;
        if (read(parent_to_child[0], &buf, 1) != 1) {
            fprintf(2, "child read error\n");
            exit(1);
        }
        printf("%d: received ping\n", getpid());

        if (write(child_to_parent[1], &buf, 1) != 1) {
            fprintf(2, "child write error\n");
            exit(1);
        }
        close(child_to_parent[1]); // 关闭子到父的写端
        exit(0);
    } else { // 父进程
        close(parent_to_child[0]); // 关闭父到子的读端
        close(child_to_parent[1]); // 关闭子到父的写端

        char buf = 'a';
        if (write(parent_to_child[1], &buf, 1) != 1) {
            fprintf(2, "parent write error\n");
            exit(1);
        }
        close(parent_to_child[1]); // 关闭父到子的写端

        if (read(child_to_parent[0], &buf, 1) != 1) {
            fprintf(2, "parent read error\n");
            exit(1);
        }
        printf("%d: received pong\n", getpid());
        exit(0);
    }
}
