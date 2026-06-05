#include "calculator.h"
#include <iostream>
#include <vector>
#include <iomanip>
#include <deque>
#include <fstream>
#include <sstream>

using namespace std;

// Constants
#define WINDOW_SIZE_NS 30000000000LL
#define INTERVAL_NS 1000000000LL
#define BUFFER_SIZE 10000

// Helper struct for sliding window calculations
struct WindowEntry {
    long long timestamp;
    double price;
};

// Note: Generation functions moved to FileProcessor class for better separation of concerns

int main() {
    cout << "=== Time-Weighted Moving Calculations ===" << endl;
    cout << endl;

    // Step 1: Read first and last timestamp for window calculation
    FileProcessor processor;
    auto [timestamps, prices] = processor.ReadSecurityCSV("security1.csv");

    if (timestamps.empty()) {
        cerr << "Error: No data loaded. Exiting." << endl;
        return 1;
    }

    long long firstTimestamp = timestamps[0];
    long long lastTimestamp = timestamps.back();
    long long windowStart = firstTimestamp + WINDOW_SIZE_NS;

    cout << "Loaded " << timestamps.size() << " data points" << endl;
    cout << "First timestamp: " << firstTimestamp << endl;
    cout << "Last timestamp: " << lastTimestamp << endl;
    cout << endl;

    // Step 2-5: FileProcessor handles all file reading/writing in chunks
    // MULTITHREADING OPPORTUNITY: Parallelize the 4 Process* calls below
    //
    // Current (Sequential): ~140+ seconds total
    //   processor.ProcessTWMATimestamps(...);  // ~35s
    //   processor.ProcessTWMAIntervals(...);   // ~35s
    //   processor.ProcessTWMMTimestamps(...);  // ~35s
    //   processor.ProcessTWMMIntervals(...);   // ~35s
    //
    // Parallelized (4 threads): ~35-40 seconds total (4x speedup)
    //   std::thread t1(&FileProcessor::ProcessTWMATimestamps, &processor, ...);
    //   std::thread t2(&FileProcessor::ProcessTWMAIntervals, &processor, ...);
    //   std::thread t3(&FileProcessor::ProcessTWMMTimestamps, &processor, ...);
    //   std::thread t4(&FileProcessor::ProcessTWMMIntervals, &processor, ...);
    //   t1.join(); t2.join(); t3.join(); t4.join();
    //
    // Why this works:
    //   - Each Process* method reads entire security1.csv (independent reads OK)
    //   - Each writes to different output file (no file contention)
    //   - Each uses separate calculator instance (no shared state)
    //   - No synchronization needed between threads
    //
    // For even more parallelism within each Process method:
    //   - Use producer-consumer queue for chunk read-ahead
    //   - See comments in calculator.cpp ProcessTWMA/TWMM methods for details

    processor.ProcessTWMATimestamps("security1.csv", windowStart, lastTimestamp);
    processor.ProcessTWMAIntervals("security1.csv", windowStart, lastTimestamp);
    processor.ProcessTWMMTimestamps("security1.csv", windowStart, lastTimestamp);
    processor.ProcessTWMMIntervals("security1.csv", windowStart, lastTimestamp);

    return 0;
}
