#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

// --- THREADPOOL CLASS IMPLEMENTATION ---
class ThreadPool {
private:
    std::vector<std::thread> workers;           // The active threads
    std::queue<std::function<void()>> tasks;    // Task queue

    std::mutex queue_mutex;                     // Protects access to the queue
    std::condition_variable condition;          // Notifies threads when a task is available
    bool stop_pool;                             // Flag to shut down everything

public:
    // Constructor: Creates a fixed number of worker threads
    ThreadPool(size_t threads) : stop_pool(false) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        // Lock the queue to safely extract a task
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] {
                            return this->stop_pool || !this->tasks.empty();
                            });

                        // Exit thread if pool is stopping and no tasks remain
                        if (this->stop_pool && this->tasks.empty()) return;

                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task(); // Execute the task (counting words in a file)
                }
                });
        }
    }

    // Adds a new task to the pool
    void push(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(std::move(task));
        }
        condition.notify_one(); // Wake up one thread to handle the task
    }

    // Destructor: ensures everything shuts down cleanly
    ~ThreadPool() {
        stop();
    }

    void stop() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop_pool = true;
        }
        condition.notify_all(); // Wake up all threads so they check the stop flag
        for (std::thread& worker : workers) {
            if (worker.joinable()) worker.join();
        }
    }

    size_t task_count() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        return tasks.size();
    }
};

// --- LOGIC: WORD COUNTING ---

void processFile(std::string fileName, std::atomic<int>& globalCounter) {
    std::ifstream file(fileName);
    if (!file.is_open()) {
        std::cerr << "Error opening file: " << fileName << std::endl;
        return;
    }

    std::string word;
    int localCount = 0;
    while (file >> word) {
        localCount++;
    }

    globalCounter += localCount; // Thread-safe addition
    std::cout << "[Thread " << std::this_thread::get_id() << "] "
        << fileName << ": " << localCount << " words." << std::endl;
}

int main() {
    // 1. List of files to process
    std::vector<std::string> myFiles = { "DINOSAURATLAS.txt", "ThePoisonousPrinciplesContained.txt", "text3.txt" };

    // 2. Thread-safe counter (atomic prevents race conditions)
    std::atomic<int> totalWords(0);

    // 3. Create the pool with 4 threads
    ThreadPool pool(4);

    // 4. Send each file as a task to the pool
    for (const std::string& file : myFiles) {
        pool.push([file, &totalWords] {
            processFile(file, totalWords);
            });
    }

    // 5. Shutdown the pool and wait for all threads to finish
    pool.stop();

    std::cout << "\n========================================" << std::endl;
    std::cout << "TOTAL WORD COUNT: " << totalWords << " words." << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}