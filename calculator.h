#ifndef CALCULATOR_H
#define CALCULATOR_H

#include <vector>
#include <list>
#include <deque>
#include <map>
#include <string>
#include <cmath>
#include <utility>
#include <algorithm>
#include <functional>

using namespace std;

// Data point structure to hold timestamp, price, and calculated duration
struct DataPoint {
    long long timestamp;  // nanoseconds
    double price;
    double duration;      // nanoseconds, calculated at time of query

    DataPoint(long long ts, double p) : timestamp(ts), price(p), duration(0.0) {}
};

// Abstract base class for moving window calculations
class MovingCalculator {
protected:
    static constexpr long long WINDOW_SIZE_NS = 30000000000LL;  // 30 seconds in nanoseconds

public:
    virtual ~MovingCalculator() = default;

    // Add a new price-timestamp pair
    virtual void AddNewValue(double price, long long timestamp) = 0;

    // Get current calculated value at time t
    virtual double GetCurrentValue(long long t) = 0;
};

// Time-Weighted Moving Average and Standard Deviation Calculator
class TWMACalculator : public MovingCalculator {
private:
    deque<DataPoint> window;               // Deque holding 30-second window of entries
    double twmaRunningSum = 0.0;           // Weighted sum of completed entries
    double lastPrice = 0.0;                // Price of last added value
    
    // Helper: Remove entries older than 30 seconds
    void PruneWindow(long long windowStart);

public:
    TWMACalculator() = default;

    // Add a new price-timestamp pair
    void AddNewValue(double price, long long timestamp) override;

    // Get current TWMA at time t
    double GetCurrentValue(long long t) override;

    // Get current TWSTD (Time-Weighted Standard Deviation) at time t
    double GetCurrentTWSTD(long long t);
};

// Time-Weighted Moving Median Calculator
class TWMMCalculator : public MovingCalculator {
private:
    deque<DataPoint> window;               // Deque holding 30-second window of entries
    static constexpr long long HALF_WINDOW_NS = 15000000000LL;  // 15 seconds in nanoseconds

    // Helper: Calculate cumulative durations for price levels
    struct PriceDuration {
        double price;
        double cumulativeDuration;  // Duration at or below this price
    };

    // Helper: Get sorted unique prices with cumulative durations
    vector<PriceDuration> GetPriceLevels(long long t) const;

public:
    TWMMCalculator() = default;

    // Add a new price-timestamp pair
    void AddNewValue(double price, long long timestamp) override;

    // Get current TWMM (Time-Weighted Moving Median) at time t
    double GetCurrentValue(long long t) override;
};

// File processor for buffered CSV I/O
class FileProcessor {
private:
    static constexpr int BUFFER_SIZE = 10000;  // Lines per buffer

public:
    FileProcessor() = default;

    // Read CSV file and parse into vectors of timestamps and prices
    // Returns pair of (timestamps, prices)
    pair<vector<long long>, vector<double>> 
    ReadSecurityCSV(const string& filename);

    // Stream processing methods - FileProcessor handles file I/O in chunks
    void ProcessTWMATimestamps(const string& csvFilename, long long windowStart,
                               long long lastTimestamp);
    void ProcessTWMAIntervals(const string& csvFilename, long long windowStart,
                              long long lastTimestamp);
    void ProcessTWMMTimestamps(const string& csvFilename, long long windowStart,
                               long long lastTimestamp);
    void ProcessTWMMIntervals(const string& csvFilename, long long windowStart,
                              long long lastTimestamp);

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

private:
    // Generic chunk processor - unifies all 4 Process methods
    // callback: called for each data line, receives (timestamp, price, writeBuffer, rowsWritten)
    // finalize: called after all data processed (for interval tail output)
    typedef std::function<void(long long, double, stringstream&, int&)> CallbackFunc;
    typedef std::function<void(stringstream&, int&)> FinalizeCallback;
    
    void ProcessChunkedData(
        const string& csvFilename,
        const string& outputFilename,
        const string& header,
        long long windowStart,
        long long lastTimestamp,
        CallbackFunc callback,
        FinalizeCallback finalize = nullptr
    );
};

#endif // CALCULATOR_H
