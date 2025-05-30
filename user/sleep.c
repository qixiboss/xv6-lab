#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        // 参数错误时输出到标准错误（文件描述符 2）
        write(2, "usage: sleep ticks\n", 18);
        exit(1);
    }
    int ticks = atoi(argv[1]);  // 将字符串参数转换为整数
    sleep(ticks);               // 调用系统调用 sleep
    exit(0);                    // 正常退出
}
