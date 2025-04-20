#include <jni.h>
#include <string>
#include <android/log.h>
#include <sys/socket.h> // For socket(), connect(), send(), etc.
#include <arpa/inet.h>  // For inet_pton(), htons()
#include <unistd.h>     // For close(), sleep functions
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>
#include <sstream> // <-- for ostringstream / istringstream
#include <iostream>
using namespace std;
#define LOG_TAG "MyCppApp"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define SERVER_PORT 8080

#define SERVER_IP "56.228.11.151"

static std::atomic<bool> shouldStop(false);

// Helper to safely close the socket only once
inline void safeClose(int &fd)
{
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}

struct Job
{
    string chrom;
    size_t start;
    string seq;

    string serialize() const
    {
        ostringstream oss;
        oss << chrom << '\n'
            << start << '\n'
            << seq << '\n';
        return oss.str();
    }

    static Job deserialize(const string &data)
    {
        istringstream iss(data);
        Job job;
        getline(iss, job.chrom);
        string startStr;
        getline(iss, startStr);
        // Trim carriage return if present
        if (!startStr.empty() && startStr.back() == '\r')
            startStr.pop_back();
        try
        {
            job.start = stoull(startStr);
        }
        catch (const exception &e)
        {
            job.start = 0;
            LOGI("Failed to parse start number '%s', defaulting to 0", startStr.c_str());
        }
        getline(iss, job.seq);
        return job;
    }
};

/**
 * Compare human and virus sequences to find regions with high identity.
 *
 * @param humanSeq The human genome sequence (approx. 1 million characters)
 * @param virusSeq The virus genome sequence (approx. 35,000 characters)
 * @param jobId The ID of the human sequence job
 * @param vJobId The ID of the virus sequence job
 * @return A formatted string with the results of the comparison
 */
string compareSequences(const string &humanSeq, const string &virusSeq, int jobId, int vJobId)
{
    const size_t kWindowSize = 40; // 40-bp window
    float bestIdentity = 0.0;
    size_t bestHumanStart = -1;
    size_t bestVirusStart = -1;

    // Only process if we have enough data in both sequences
    if (humanSeq.length() < kWindowSize || virusSeq.length() < kWindowSize)
    {
        LOGI("Sequences too short for comparison");
        return "job_id=" + to_string(jobId) + ";vjob_id=" + to_string(vJobId) +
               ";human_start=-1;virus_start=-1;identity=0";
    }

    // Calculate the maximum start positions for sliding the window
    const size_t maxVirusPos = virusSeq.length() - kWindowSize;
    const size_t maxHumanPos = humanSeq.length() - kWindowSize;

    LOGI("Starting sequence comparison: virus length=%zu, human length=%zu",
         virusSeq.length(), humanSeq.length());

    // Slide window across virus sequence
    for (size_t virusPos = 0; virusPos <= maxVirusPos && !shouldStop; virusPos++)
    {
        // Extract the current virus window
        const string virusWindow = virusSeq.substr(virusPos, kWindowSize);

        // Slide this virus window across the human sequence
        for (size_t humanPos = 0; humanPos <= maxHumanPos && !shouldStop; humanPos++)
        {
            LOGI("`,`,`,`computing`,`,`,`");
            // Extract the current human window
            const string humanWindow = humanSeq.substr(humanPos, kWindowSize);

            // Count matching bases
            size_t matches = 0;
            for (size_t i = 0; i < kWindowSize; i++)
            {
                if (virusWindow[i] == humanWindow[i])
                {
                    matches++;
                }
            }

            // Calculate identity
            float identity = static_cast<float>(matches) / kWindowSize;

            // Check if this is a hit (identity >= 0.80) and better than previous best
            if (identity >= 0.80 && identity > bestIdentity)
            {
                bestIdentity = identity;
                bestHumanStart = humanPos;
                bestVirusStart = virusPos;
            }
        }
    }

    // Format the result string
    ostringstream result;
    result << "job_id=" << jobId << ";vjob_id=" << vJobId << ";";

    if (bestIdentity >= 0.80)
    {
        result << "human_start=" << bestHumanStart << ";virus_start=" << bestVirusStart
               << ";identity=" << bestIdentity;
    }
    else
    {
        result << "human_start=-1;virus_start=-1;identity=0";
    }

    LOGI("Comparison results: %s", result.str().c_str());
    return result.str();
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_dinoapp_NumberSenderService_stopConnection(JNIEnv *env, jobject /* this */)
{
    // Signal to stop loops
    shouldStop = true;
    LOGI("Signal received to stop the connection");
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_dinoapp_NumberSenderService_sendNumbersToServer(JNIEnv *env, jobject /* this */, jint startNumber)
{
    shouldStop = false;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    // Set recv timeout so we can check shouldStop periodically
    struct timeval tv{.tv_sec = 1, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (sock < 0)
    {
        LOGI("Socket creation error");
        return;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0)
    {
        LOGI("Invalid server address: %s", SERVER_IP);
        safeClose(sock);
        return;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        LOGI("Connection to server failed");
        safeClose(sock);
        return;
    }
    LOGI("Connected to server. Waiting for size of data...");

    // Step D: receive the size of the virus data from the server.
    size_t vdataSize;
    ssize_t viResult = recv(sock, (char *)&vdataSize, sizeof(vdataSize), 0);
    if (viResult < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            if (shouldStop)
                return;
            LOGI("Timeout while waiting for data size");
            safeClose(sock);
            return;
        }
        LOGI("Error receiving data size");
        safeClose(sock);
        return;
    }
    LOGI("Received virus data size: %zu", vdataSize);
    if (vdataSize <= 0)
    {
        LOGI("Invalid virus data size received");
        safeClose(sock);
        return;
    }
    // Step D: receive the virus data from the server.
    string vbuffer(vdataSize, '\0');
    size_t vreceived = 0;
    // Loop to read full payload, but break if stopping
    while (vreceived < vdataSize)
    {
        ssize_t bytes = recv(sock, &vbuffer[vreceived], vdataSize - vreceived, 0);
        if (bytes < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                if (shouldStop)
                    break;
                continue;
            }
            LOGI("Receive failed for virus data");
            safeClose(sock);
            return;
        }
        vreceived += bytes;
    }
    if (shouldStop)
        return;
    // Deserialize
    Job vjob = Job::deserialize(vbuffer);
    LOGI("Virus Job - Chromosome: %s, Start: %llu, Sequence length: %zu",
         vjob.chrom.c_str(), vjob.start, vjob.seq.length());

    int vJobId = static_cast<int>(startNumber); // Using the startNumber parameter as the virus job ID

    while (!shouldStop)
    {
        // Receive size of human sequence data
        size_t dataSize;
        ssize_t iResult = recv(sock, (char *)&dataSize, sizeof(dataSize), 0);

        if (iResult < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                if (shouldStop)
                    break;
                continue;
            }
            LOGI("Error receiving human data size");
            safeClose(sock);
            return;
        }

        LOGI("Received human data size: %zu", dataSize);
        if (dataSize <= 0)
        {
            LOGI("Invalid human data size received");
            safeClose(sock);
            return;
        }

        // Receive human sequence data
        string buffer(dataSize, '\0');
        size_t received = 0;
        while (received < dataSize)
        {
            ssize_t bytes = recv(sock, &buffer[received], dataSize - received, 0);
            if (bytes < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    if (shouldStop)
                        break;
                    continue;
                }
                LOGI("Receive failed for human data");
                safeClose(sock);
                return;
            }
            received += bytes;
        }
        if (shouldStop)
            break;

        // Deserialize human job
        Job job = Job::deserialize(buffer);
        LOGI("Human Job - Chromosome: %s, Start: %llu, Sequence length: %zu",
             job.chrom.c_str(), job.start, job.seq.length());

        // Perform sequence comparison
        int jobId = static_cast<int>(job.start); // Using job start position as job ID
        string result = compareSequences(job.seq, vjob.seq, jobId, vJobId);
        LOGI("Sending comparison result: %s", result.c_str());

        // Send the size of the comparison result first
        size_t resultSize = result.size();
        ssize_t sizeSent = send(sock, (char *)&resultSize, sizeof(resultSize), 0);
        if (sizeSent < 0)
        {
            LOGI("Error sending result size");
            safeClose(sock);
            return;
        }

        // Then send the comparison result itself
        ssize_t dataSent = send(sock, result.c_str(), resultSize, 0);
        if (dataSent < 0)
        {
            LOGI("Error sending comparison result");
            safeClose(sock);
            return;
        }
    }

    // Final cleanup
    LOGI("Closing socket");
    safeClose(sock);
}
