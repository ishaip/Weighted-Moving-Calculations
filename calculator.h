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

/* Time-weighted stats over 30s window: accumulate completed segments,
   handle open tail at query time. Chunk-based CSV streaming for speed. */

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
    
    void PruneWindow(long long windowStart);

public:
    TWMACalculator() = default;

    void AddNewValue(double price, long long timestamp) override;
    double GetCurrentValue(long long t) override;
    double GetCurrentTWSTD(long long t);
};

// Time-Weighted Moving Median Calculator
class TWMMCalculator : public MovingCalculator {
private:
    deque<DataPoint> window;               // Deque holding 30-second window of entries
    static constexpr long long HALF_WINDOW_NS = 15000000000LL;  // 15 seconds in nanoseconds

    struct PriceDuration {
        double price;
        double cumulativeDuration;
    };

    vector<PriceDuration> GetPriceLevels(long long t) const;

public:
    TWMMCalculator() = default;

    void AddNewValue(double price, long long timestamp) override;
    double GetCurrentValue(long long t) override;
};

// File processor for buffered CSV I/O
class FileProcessor {
private:
    static constexpr int BUFFER_SIZE = 10000;  // Lines per buffer

public:
    FileProcessor() = default;

    pair<vector<long long>, vector<double>> 
    ReadSecurityCSV(const string& filename);

    void ProcessTWMATimestamps(const string& csvFilename, long long windowStart,
                               long long lastTimestamp);
    void ProcessTWMAIntervals(const string& csvFilename, long long windowStart,
                              long long lastTimestamp);
    void ProcessTWMMTimestamps(const string& csvFilename, long long windowStart,
                               long long lastTimestamp);
    void ProcessTWMMIntervals(const string& csvFilename, long long windowStart,
                              long long lastTimestamp);

    void WriteCSV(const string& filename, 
                  const vector<long long>& timestamps,
                  const vector<double>& values,
                  const string& valueHeader);

    void WriteCSV(const string& filename,
                  const vector<long long>& timestamps,
                  const vector<double>& values1,
                  const vector<double>& values2,
                  const string& header1,
                  const string& header2);

private:
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
