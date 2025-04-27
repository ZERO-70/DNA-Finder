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
#include <sstream>
#include <iostream>
#include <arpa/inet.h>
using namespace std;
#define LOG_TAG "MyCppApp"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define SERVER_PORT 8080

#define SERVER_IP "56.228.11.151"

const size_t V_CHUNK = 5000; // length of each virus slice
const size_t V_STEP = 4000;  // win - over

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

struct Task
{
    Job human;     // 200 kb slice
    size_t vIndex; // which 5 kb virus chunk to use
    size_t vStart; // start position of the virus chunk
    string vSeq;   // virus chunk sequence

    static Task deserialize(const std::string &data)
    {
        std::istringstream iss(data);
        Task t;
        getline(iss, t.human.chrom);
        std::string tmp;
        getline(iss, tmp); // start
        t.human.start = std::stoull(tmp);
        getline(iss, t.human.seq); // 3rd line: sequence
        getline(iss, tmp);         // 4th line: index
        t.vIndex = std::stoul(tmp);
        getline(iss, tmp); // 5th line: virus start position
        t.vStart = std::stoull(tmp);
        getline(iss, t.vSeq); // 6th line: virus sequence
        return t;
    }
};

/**
 * Compare human and virus sequences using Smith-Waterman algorithm to find regions with high identity.
 *
 * @param env JNI environment pointer
 * @param thiz JNI service object
 * @param humanSeq The human genome sequence (approx. 1 million characters)
 * @param virusSeq The virus genome sequence (approx. 35,000 characters)
 * @param jobId The ID of the human sequence job
 * @param vJobId The ID of the virus sequence job
 * @return A formatted string with the results of the comparison
 */
static string compareSequences(JNIEnv *env, jobject thiz, const string &humanSeq, const string &virusSeq, int jobId, int vJobId)
{
    LOGI("compareSequences called: humanSeq.len=%zu, virusSeq.len=%zu",
         humanSeq.length(), virusSeq.length());
    jclass cls = env->GetObjectClass(thiz);
    jmethodID midProgress = env->GetMethodID(cls, "onNativeProgress", "(I)V");

    const int MATCH_SCORE = 2;
    const int MISMATCH_SCORE = -1;
    const int GAP_SCORE = -2;

    float bestIdentity = 0.0;
    size_t bestHumanStart = -1;
    size_t bestVirusStart = -1;

    if (humanSeq.length() < 10 || virusSeq.length() < 10)
    {
        LOGI("Sequences too short for comparison");
        return "job_id=" + to_string(jobId) + ";vjob_id=" + to_string(vJobId) +
               ";human_start=-1;virus_start=-1;identity=0";
    }

    const size_t m = humanSeq.length();
    const size_t n = virusSeq.length();

    LOGI("Starting memory-optimized Smith-Waterman comparison: virus length=%zu, human length=%zu", n, m);

    // Use linear memory approach - just keep two rows in memory
    // current row and previous row
    vector<int> previous(n + 1, 0);
    vector<int> current(n + 1, 0);

    // For tracking the highest score and its position
    int maxScore = 0;
    size_t maxI = 0, maxJ = 0;

    // To store traceback information
    struct TracebackCell
    {
        char move;
        int score;
    };
    vector<TracebackCell> maxScoreHistory;

    const size_t totalCells = m * n;
    int lastPercent = -1;
    size_t cellsProcessed = 0;

    for (size_t i = 1; i <= m; i++)
    {
        fill(current.begin(), current.end(), 0);

        for (size_t j = 1; j <= n; j++)
        {
            if (shouldStop)
            {
                // Early exit if requested
                LOGI("Smith-Waterman algorithm interrupted");
                return "job_id=" + to_string(jobId) + ";vjob_id=" + to_string(vJobId) +
                       ";human_start=-1;virus_start=-1;identity=0";
            }

            int match = (humanSeq[i - 1] == virusSeq[j - 1]) ? MATCH_SCORE : MISMATCH_SCORE;

            int diagonal = previous[j - 1] + match;
            int up = previous[j] + GAP_SCORE;
            int left = current[j - 1] + GAP_SCORE;

            int score = max({0, diagonal, up, left});
            current[j] = score;
            if (score > maxScore)
            {
                maxScore = score;
                maxI = i;
                maxJ = j;

                maxScoreHistory.clear();

                char move = 'n';
                if (score == diagonal)
                    move = 'd';
                else if (score == up)
                    move = 'u';
                else if (score == left)
                    move = 'l';

                maxScoreHistory.push_back({move, score});
            }

            // Update progress
            cellsProcessed++;
            int percent = static_cast<int>((cellsProcessed * 100) / totalCells);
            if (percent != lastPercent && percent % 5 == 0)
            { // less frequent updates (every 5%)
                lastPercent = percent;
                LOGI("Smith-Waterman progress: %d%% (%zu / %zu)", percent, cellsProcessed, totalCells);
                env->CallVoidMethod(thiz, midProgress, percent);
            }
        }

        // Swap rows for next iteration
        swap(previous, current);
    }

    // If no alignment found (all zeros), return no match
    if (maxScore == 0)
    {
        LOGI("No significant alignment found");
        return "job_id=" + to_string(jobId) + ";vjob_id=" + to_string(vJobId) +
               ";human_start=-1;virus_start=-1;identity=0";
    }

    LOGI("Found best score %d at position (%zu,%zu)", maxScore, maxI, maxJ);

    const size_t WINDOW_SIZE = 100; // Use a reasonable window size
    size_t startI = (maxI > WINDOW_SIZE) ? (maxI - WINDOW_SIZE) : 0;
    size_t startJ = (maxJ > WINDOW_SIZE) ? (maxJ - WINDOW_SIZE) : 0;

    size_t windowM = min(maxI + WINDOW_SIZE, m) - startI;
    size_t windowN = min(maxJ + WINDOW_SIZE, n) - startJ;

    vector<vector<int>> subMatrix(windowM + 1, vector<int>(windowN + 1, 0));
    vector<vector<char>> tbMatrix(windowM + 1, vector<char>(windowN + 1, 'n'));

    // Fill the sub-matrix using Smith-Waterman
    for (size_t i = 1; i <= windowM; i++)
    {
        for (size_t j = 1; j <= windowN; j++)
        {
            size_t origI = startI + i;
            size_t origJ = startJ + j;

            int match = (humanSeq[origI - 1] == virusSeq[origJ - 1]) ? MATCH_SCORE : MISMATCH_SCORE;

            int diagonal = subMatrix[i - 1][j - 1] + match;
            int up = subMatrix[i - 1][j] + GAP_SCORE;
            int left = subMatrix[i][j - 1] + GAP_SCORE;

            int score = max({0, diagonal, up, left});
            subMatrix[i][j] = score;

            // Store traceback pointer
            if (score == 0)
                tbMatrix[i][j] = 'n';
            else if (score == diagonal)
                tbMatrix[i][j] = 'd';
            else if (score == up)
                tbMatrix[i][j] = 'u';
            else if (score == left)
                tbMatrix[i][j] = 'l';
        }
    }

    // Now perform traceback from the maximum score position in our sub-matrix
    size_t localMaxI = maxI - startI;
    size_t localMaxJ = maxJ - startJ;

    string alignedHuman = "";
    string alignedVirus = "";
    size_t i = localMaxI;
    size_t j = localMaxJ;

    while (i > 0 && j > 0 && subMatrix[i][j] > 0)
    {
        if (tbMatrix[i][j] == 'd')
        {
            alignedHuman = humanSeq[startI + i - 1] + alignedHuman;
            alignedVirus = virusSeq[startJ + j - 1] + alignedVirus;
            i--;
            j--;
        }
        else if (tbMatrix[i][j] == 'u')
        {
            alignedHuman = humanSeq[startI + i - 1] + alignedHuman;
            alignedVirus = '-' + alignedVirus;
            i--;
        }
        else if (tbMatrix[i][j] == 'l')
        {
            alignedHuman = '-' + alignedHuman;
            alignedVirus = virusSeq[startJ + j - 1] + alignedVirus;
            j--;
        }
        else
        {
            // Should not happen with a correct implementation
            break;
        }
    }

    // Calculate starting positions (0-based) for the alignment
    bestHumanStart = startI + localMaxI - alignedHuman.length();
    bestVirusStart = startJ + localMaxJ - alignedVirus.length();

    size_t alen = alignedHuman.length();
    size_t matches = 0;
    for (size_t k = 0; k < alen; ++k)
    {
        if (alignedHuman[k] == alignedVirus[k] && alignedHuman[k] != '-')
        {
            ++matches;
        }
    }
    bestIdentity = static_cast<float>(matches) / max(size_t(1), alen);

    bestIdentity = static_cast<float>(matches) / max(size_t(1), alignedHuman.length());
    LOGI("SW Alignment - Human start: %zu, Virus start: %zu, Identity: %f (%zu/%zu)",
         bestHumanStart, bestVirusStart, bestIdentity, matches, alignedHuman.length());

    // Format the result string
    ostringstream result;
    result << "job_id=" << jobId
           << ";vjob_id=" << vJobId
           << ";human_start=" << bestHumanStart
           << ";virus_start=" << bestVirusStart
           << ";identity=" << bestIdentity
           << ";length=" << alen
           << ";matches=" << matches;

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
Java_com_example_dinoapp_NumberSenderService_sendNumbersToServer(JNIEnv *env, jobject thisObj, jint startNumber)
{
    shouldStop = false;

    // Get reference to the service object for callbacks
    jobject thiz = env->NewGlobalRef(thisObj);

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
    LOGI("Connected to server.");

    // Skip receiving the full virus genome since it's now included in each task

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

        LOGI("Received data size: %zu", dataSize);
        if (dataSize <= 0)
        {
            LOGI("Invalid data size received");
            safeClose(sock);
            return;
        }

        // Receive task data
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
                LOGI("Receive failed for task data");
                safeClose(sock);
                return;
            }
            received += bytes;
        }
        if (shouldStop)
            break;

        // Deserialize Task (human + virus chunk data)
        Task task = Task::deserialize(buffer);
        LOGI("Human slice: %s:%zu len=%zu virus-chunk=%zu virus-start=%zu virus-seq-len=%zu",
             task.human.chrom.c_str(),
             task.human.start,
             task.human.seq.length(),
             task.vIndex,
             task.vStart,
             task.vSeq.length());

        // Use the virus chunk data directly from the task
        // No need to compute the offset or extract from full virus genome
        string virusSlice = task.vSeq;

        // Perform sequence comparison using the virus chunk from the task
        int jobId = static_cast<int>(task.human.start);
        string result = compareSequences(env, thiz, task.human.seq, virusSlice,
                                         /*jobId*/ static_cast<int>(task.human.start),
                                         /*vJobId*/ static_cast<int>(task.vIndex));
        if (shouldStop)
            break;

        LOGI("Sending comparison result: %s", result.c_str());

        // Send the size of the comparison result first
        uint32_t netLen = htonl((uint32_t)result.size());
        ssize_t sizeSent = send(sock, (char *)&netLen, sizeof(netLen), 0);
        if (sizeSent < 0)
        {
            LOGI("Error sending result size");
            safeClose(sock);
            return;
        }

        // Then send the comparison result itself
        ssize_t dataSent = send(sock, result.c_str(), result.size(), 0);
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
    // Release global reference
    env->DeleteGlobalRef(thiz);
}
