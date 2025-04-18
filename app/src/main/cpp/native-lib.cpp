#include <jni.h>
#include <string>
#include <android/log.h>
#include <sys/socket.h>    // For socket(), connect(), send(), etc.
#include <arpa/inet.h>     // For inet_pton(), htons()
#include <unistd.h>        // For close(), sleep functions
#include <cstring>
#include <chrono>
#include <thread>

#define LOG_TAG "MyCppApp"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define SERVER_PORT 8080

// Use your AWS server's public IP address here:
#define SERVER_IP "56.228.11.151"

// Native function to connect to the server, wait for "OK 200", and then send numbers every 1 second.
extern "C"
JNIEXPORT void JNICALL
Java_com_example_dinoapp_NumberSenderService_sendNumbersToServer(JNIEnv* env, jobject /* this */, jint startNumber) {
    int counter = startNumber;

    // Step A: Create a TCP socket.
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOGI("Socket creation error");
        return;
    }

    // Step B: Set up the server address structure.
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    // Convert the IP address from text to binary form.
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        LOGI("Invalid server address: %s", SERVER_IP);
        close(sock);
        return;
    }

    // Step C: Connect to the server.
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        LOGI("Connection to server failed");
        close(sock);
        return;
    }
    LOGI("Connected to server. Waiting for initial message...");

    // Step D: Wait to receive the initial "OK 200" message.
    char recvbuf[512] = {0};
    int iResult = recv(sock, recvbuf, sizeof(recvbuf) - 1, 0);
    if (iResult <= 0) {
        LOGI("Failed to receive initial message");
        close(sock);
        return;
    }
    recvbuf[iResult] = '\0';
    std::string initMsg(recvbuf);
    LOGI("Received initial message: %s", initMsg.c_str());

    if (initMsg != "OK 200") {
        LOGI("Did not receive expected OK 200. Received: %s", initMsg.c_str());
        close(sock);
        return;
    }

    while (true) {
        std::string msg = std::to_string(counter);
        const char* sendbuf = msg.c_str();
        int sent = send(sock, sendbuf, strlen(sendbuf), 0);
        if (sent < 0) {
            LOGI("Sending data failed");
            break;
        }
        LOGI("Sent number: %s", sendbuf);

        std::this_thread::sleep_for(std::chrono::seconds(1));
        counter++;
    }

    close(sock);
}
