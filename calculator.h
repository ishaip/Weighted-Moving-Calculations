#ifndef CALCULATOR_H
#define CALCULATOR_H

#include <vector>
#include <list>
#include <map>
#include <string>
#include <cmath>
#include <utility>
#include <algorithm>

using namespace std;

// Data point structure to hold timestamp, price, and calculated duration
struct DataPoint {
    long long timestamp;  // nanoseconds
    double price;
    double duration;      // nanoseconds, calculated at time of query

    DataPoint(long long ts, double p) : timestamp(ts), price(p), duration(0.0) {}
};

// Time-Weighted Moving Average and Standard Deviation Calculator
class TWMACalculator {
private:
    vector<DataPoint> window;              // Vector holding all added entries
    long long lastTimestamp = -1;               // Timestamp of last added value
    double lastPrice = 0.0;                     // Price of last added value
    
    // Constants
    static constexpr long long WINDOW_SIZE_NS = 30000000000LL;  // 30 seconds in nanoseconds

public:
    TWMACalculator() = default;

    // Add a new price-timestamp pair
    void AddNewValue(double price, long long timestamp);

    // Get current TWMA at time t
    double GetCurrentTWMA(long long t);

    // Get current TWSTD (Time-Weighted Standard Deviation) at time t
    double GetCurrentTWSTD(long long t);
};

// Time-Weighted Moving Median Calculator using dual-heap approach
class TWMMCalculator {
private:
    list<DataPoint> entries;              // All entries in the moving window
    
    // Constants
    static constexpr long long WINDOW_SIZE_NS = 30000000000LL;  // 30 seconds in nanoseconds
    static constexpr long long HALF_WINDOW_NS = 15000000000LL;  // 15 seconds in nanoseconds

    // Helper: Remove entries outside 30-second window
    void PruneWindow(long long t);

    // Helper: Calculate cumulative durations for price levels
    struct PriceDuration {
        double price;
        double cumulativeDuration;  // Duration at or below this price
    };

    // Helper: Get sorted unique prices with cumulative durations
    std::vector<PriceDuration> GetPriceLevels(long long t) const;

public:
    TWMMCalculator() = default;

    // Add a new price-timestamp pair
    void AddNewValue(double price, long long timestamp);

    // Get current TWMM (Time-Weighted Moving Median) at time t
    double GetCurrentTWMM(long long t);
};

// File processor for buffered CSV I/O
class FileProcessor {
private:
    static constexpr int BUFFER_SIZE = 1000;  // Lines per buffer

public:
    FileProcessor() = default;

    // Read CSV file and parse into vectors of timestamps and prices
    // Returns pair of (timestamps, prices)
    pair<vector<long long>, vector<double>> 
    ReadSecurityCSV(const string& filename);

    // Write CSV with timestamp and single value column
    void WriteCSV(const string& filename, 
                  const vector<long long>& timestamps,
                  const vector<double>& values,
                  const string& valueHeader);

    // Write CSV with timestamp and two value columns
    void WriteCSV(const string& filename,
                  const vector<long long>& timestamps,
                  const vector<double>& values1,
                  const vector<double>& values2,
                  const string& header1,
                  const string& header2);
};

#endif // CALCULATOR_H
