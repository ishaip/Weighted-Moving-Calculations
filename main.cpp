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

    // Pass 1: Load all data and find the first valid timestamp (where the 30s window
    // fills up) and the last timestamp (where we stop processing).
    // This one-pass read is needed to set boundaries for our sliding window queries.
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
    couPass 2-5: Run the four independent calculation pipelines.
    // Each one re-opens the CSV file and streams chunks through its own calculator,
    // then writes to its own output file. There's zero shared state between them.
    //
    // THREADING OPPORTUNITY: These 4 calls are embarrassingly parallel. Wrapping
    // them in std::async or std::thread would give ~4x speedup with minimal change:
    //   auto t1 = std::async(std::launch::async, [&] { return processor.ProcessTWMATimestamps(...); });
    //   auto t2 = std::async(std::launch::async, [&] { return processor.ProcessTWMAIntervals(...); });
    //   auto t3 = std::async(std::launch::async, [&] { return processor.ProcessTWMMTimestamps(...); });
    //   auto t4 = std::async(std::launch::async, [&] { return processor.ProcessTWMMIntervals(...); });
    //   t1.get(); t2.get(); t3.get(); t4.get();

    // Step 2-5: FileProcessor handles all file reading/writing in chunks
    processor.ProcessTWMATimestamps("security1.csv", windowStart, lastTimestamp);
    processor.ProcessTWMAIntervals("security1.csv", windowStart, lastTimestamp);
    processor.ProcessTWMMTimestamps("security1.csv", windowStart, lastTimestamp);
    processor.ProcessTWMMIntervals("security1.csv", windowStart, lastTimestamp);

    return 0;
}
