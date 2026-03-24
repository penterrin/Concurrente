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
#include <windows.h> // Used for file listing in older C++ on Windows

// Global mutex to prevent console output from overlapping
std::mutex cout_mutex;

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop_pool;

public:
    ThreadPool(size_t threads) : stop_pool(false) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] {
                            return this->stop_pool || !this->tasks.empty();
                            });
                        if (this->stop_pool && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
                });
        }
    }

    ~ThreadPool() { stop(); }

    void push(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(std::move(task));
        }
        condition.notify_one();
    }

    void stop() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop_pool = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable()) worker.join();
        }
    }
};

// Task to count words in a single file
void countWordsTask(std::string filePath, std::atomic<long>& totalCounter) {
    std::ifstream file(filePath);
    if (!file.is_open()) return;

    std::string word;
    long localCount = 0;
    while (file >> word) {
        localCount++;
    }
    totalCounter += localCount;

    // Mutex lock to ensure std::cout doesn't overlap
    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "[File] " << filePath << ": " << localCount << " words." << std::endl;
    }
}

int main() {
    std::atomic<long> totalWords{ 0 };
    ThreadPool pool(4);

    // REQUIREMENT: The first task lists the files in "resources" folder
    pool.push([&pool, &totalWords]() {
        std::string folderPath = "resources\\";
        std::string searchPath = folderPath + "*.txt";

        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

        if (hFind == INVALID_HANDLE_VALUE) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Error: 'resources' folder not found or empty!" << std::endl;
            return;
        }

        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::string fileName = folderPath + findData.cFileName;

                // Pushing a new task for each file found
                pool.push([fileName, &totalWords]() {
                    countWordsTask(fileName, totalWords);
                    });
            }
        } while (FindNextFileA(hFind, &findData));

        FindClose(hFind);
        });

    std::cout << "Tasks dispatched. Processing...\n" << std::endl;

    // Wait a brief moment to let the first task finish scanning
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Wait for all threads to finish their work
    pool.stop();

    std::cout << "\n========================================" << std::endl;
    std::cout << "FINAL TOTAL: " << totalWords << " words." << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "\nPress Enter to exit...";
    std::cin.get();

    return 0;
}