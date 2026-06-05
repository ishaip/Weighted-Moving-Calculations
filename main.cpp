#include "calculator.h"
#include <iostream>
#include <vector>
#include <iomanip>
#include <deque>

using namespace std;

// Constants
#define WINDOW_SIZE_NS 30000000000LL
#define INTERVAL_NS 1000000000LL

// Helper struct for sliding window calculations
struct WindowEntry {
    long long timestamp;
    double price;
};

// Generate TWMA/TWSTD at input timestamps
void GenerateTWMATWSTDTimestamps(const vector<long long>& timestamps, const vector<double>& prices,
                                 long long windowStart, FileProcessor& processor) {
    cout << "Generating TWMA/TWSTD at input timestamps..." << endl;
    vector<long long> twmaTwstdTimestamps;
    vector<double> twmaValues;
    vector<double> twstdValues;

    TWMACalculator twmaCalc;
    size_t dataIdx = 0;

    // Build initial window up to 30 seconds
    while (dataIdx < timestamps.size() && timestamps[dataIdx] < windowStart) {
        twmaCalc.AddNewValue(prices[dataIdx], timestamps[dataIdx]);
        dataIdx++;
    }

    // Process queries at each data point after 30 seconds
    while (dataIdx < timestamps.size()) {
        long long queryTime = timestamps[dataIdx];
        
        // Add new entry
        twmaCalc.AddNewValue(prices[dataIdx], queryTime);
        
        // Calculate and store
        twmaTwstdTimestamps.push_back(queryTime);
        twmaValues.push_back(twmaCalc.GetCurrentValue(queryTime));
        twstdValues.push_back(twmaCalc.GetCurrentTWSTD(queryTime));
        
        dataIdx++;
    }

    processor.WriteCSV("twma_twstd_timestamps.csv", twmaTwstdTimestamps, twmaValues, 
                       twstdValues, "TWMA", "TWSTD");
    cout << endl;
}

// Generate TWMA/TWSTD at 1-second intervals
void GenerateTWMATWSTDIntervals(const vector<long long>& timestamps, const vector<double>& prices,
                                long long windowStart, long long lastTimestamp, FileProcessor& processor) {
    cout << "Generating TWMA/TWSTD at 1-second intervals..." << endl;
    vector<long long> twmaIntervalTimestamps;
    vector<double> twmaIntervalValues;
    vector<double> twstdIntervalValues;

    TWMACalculator twmaCalc;
    size_t dataIdx = 0;
    long long currentIntervalTime = windowStart;

    // Build initial window
    while (dataIdx < timestamps.size() && timestamps[dataIdx] < windowStart) {
        twmaCalc.AddNewValue(prices[dataIdx], timestamps[dataIdx]);
        dataIdx++;
    }

    // Process 1-second intervals
    while (currentIntervalTime <= lastTimestamp) {
        // Add any new data points up to this interval
        while (dataIdx < timestamps.size() && timestamps[dataIdx] <= currentIntervalTime) {
            twmaCalc.AddNewValue(prices[dataIdx], timestamps[dataIdx]);
            dataIdx++;
        }
        
        // Calculate and store
        twmaIntervalTimestamps.push_back(currentIntervalTime);
        twmaIntervalValues.push_back(twmaCalc.GetCurrentValue(currentIntervalTime));
        twstdIntervalValues.push_back(twmaCalc.GetCurrentTWSTD(currentIntervalTime));
        
        currentIntervalTime += INTERVAL_NS;
    }

    processor.WriteCSV("twma_twstd_intervals.csv", twmaIntervalTimestamps, twmaIntervalValues,
                       twstdIntervalValues, "TWMA", "TWSTD");
    cout << endl;
}

// Generate TWMM at input timestamps
void GenerateTWMMTimestamps(const vector<long long>& timestamps, const vector<double>& prices,
                            long long windowStart, FileProcessor& processor) {
    cout << "Generating TWMM at input timestamps..." << endl;
    vector<long long> twmmTimestamps;
    vector<double> twmmTimestampValues;

    deque<WindowEntry> twmmSlidingWindow;
    size_t dataIdx = 0;

    // Build initial window up to 30 seconds
    while (dataIdx < timestamps.size() && timestamps[dataIdx] < windowStart) {
        twmmSlidingWindow.push_back({timestamps[dataIdx], prices[dataIdx]});
        dataIdx++;
    }

    // Process queries at each data point after 30 seconds
    while (dataIdx < timestamps.size()) {
        long long queryTime = timestamps[dataIdx];
        
        // Remove entries older than 30 seconds
        while (!twmmSlidingWindow.empty() && (queryTime - twmmSlidingWindow.front().timestamp) > WINDOW_SIZE_NS) {
            twmmSlidingWindow.pop_front();
        }
        
        // Add new entry
        twmmSlidingWindow.push_back({timestamps[dataIdx], prices[dataIdx]});
        
        // Calculate TWMM: find the price where cumulative duration = 15 seconds
        map<double, double> priceDurations;
        for (size_t i = 0; i < twmmSlidingWindow.size(); ++i) {
            long long endTime = (i + 1 < twmmSlidingWindow.size()) ? twmmSlidingWindow[i + 1].timestamp : queryTime;
            double duration_ns = static_cast<double>(endTime - twmmSlidingWindow[i].timestamp);
            priceDurations[twmmSlidingWindow[i].price] += duration_ns;
        }
        
        // Find median: price where cumulative >= 15 seconds and (total - cumulative) >= 15 seconds
        double twmm = 0.0;
        double cumulative = 0.0;
        double totalDuration = 0.0;
        for (const auto& [p, d] : priceDurations) totalDuration += d;
        
        for (const auto& [price, duration] : priceDurations) {
            cumulative += duration;
            if (cumulative >= 15000000000LL) {  // 15 seconds
                double belowMedian = cumulative;
                double aboveMedian = totalDuration - (cumulative - duration);
                if (belowMedian >= 15000000000LL && aboveMedian >= 15000000000LL) {
                    twmm = price;
                    break;
                }
            }
        }
        
        if (twmm == 0.0 && !priceDurations.empty()) {
            // Fallback to middle cumulative duration
            double targetCum = totalDuration / 2.0;
            cumulative = 0.0;
            for (const auto& [price, duration] : priceDurations) {
                cumulative += duration;
                if (cumulative >= targetCum) {
                    twmm = price;
                    break;
                }
            }
        }
        
        // Calculate and store
        twmmTimestamps.push_back(queryTime);
        twmmTimestampValues.push_back(twmm);
        
        dataIdx++;
    }

    processor.WriteCSV("twmm_timestamps.csv", twmmTimestamps, twmmTimestampValues, "TWMM");
    cout << endl;
}

// Generate TWMM at 1-second intervals
void GenerateTWMMIntervals(const vector<long long>& timestamps, const vector<double>& prices,
                           long long windowStart, long long lastTimestamp, FileProcessor& processor) {
    cout << "Generating TWMM at 1-second intervals..." << endl;
    vector<long long> twmmIntervalTimestamps;
    vector<double> twmmIntervalValues;

    deque<WindowEntry> twmmSlidingWindow;
    size_t dataIdx = 0;
    long long currentIntervalTime = windowStart;

    // Build initial window
    while (dataIdx < timestamps.size() && timestamps[dataIdx] < windowStart) {
        twmmSlidingWindow.push_back({timestamps[dataIdx], prices[dataIdx]});
        dataIdx++;
    }

    // Process 1-second intervals
    while (currentIntervalTime <= lastTimestamp) {
        // Add any new data points up to this interval
        while (dataIdx < timestamps.size() && timestamps[dataIdx] <= currentIntervalTime) {
            twmmSlidingWindow.push_back({timestamps[dataIdx], prices[dataIdx]});
            dataIdx++;
        }
        
        // Remove entries older than 30 seconds
        while (!twmmSlidingWindow.empty() && (currentIntervalTime - twmmSlidingWindow.front().timestamp) > WINDOW_SIZE_NS) {
            twmmSlidingWindow.pop_front();
        }
        
        // Calculate TWMM: find the price where cumulative duration = 15 seconds
        map<double, double> priceDurations;
        for (size_t i = 0; i < twmmSlidingWindow.size(); ++i) {
            long long endTime = (i + 1 < twmmSlidingWindow.size()) ? twmmSlidingWindow[i + 1].timestamp : currentIntervalTime;
            double duration_ns = static_cast<double>(endTime - twmmSlidingWindow[i].timestamp);
            priceDurations[twmmSlidingWindow[i].price] += duration_ns;
        }
        
        // Find median
        double twmm = 0.0;
        double cumulative = 0.0;
        double totalDuration = 0.0;
        for (const auto& [p, d] : priceDurations) totalDuration += d;
        
        for (const auto& [price, duration] : priceDurations) {
            cumulative += duration;
            if (cumulative >= 15000000000LL) {
                double belowMedian = cumulative;
                double aboveMedian = totalDuration - (cumulative - duration);
                if (belowMedian >= 15000000000LL && aboveMedian >= 15000000000LL) {
                    twmm = price;
                    break;
                }
            }
        }
        
        if (twmm == 0.0 && !priceDurations.empty()) {
            double targetCum = totalDuration / 2.0;
            cumulative = 0.0;
            for (const auto& [price, duration] : priceDurations) {
                cumulative += duration;
                if (cumulative >= targetCum) {
                    twmm = price;
                    break;
                }
            }
        }
        
        // Calculate and store
        twmmIntervalTimestamps.push_back(currentIntervalTime);
        twmmIntervalValues.push_back(twmm);
        
        currentIntervalTime += INTERVAL_NS;
    }

    processor.WriteCSV("twmm_intervals.csv", twmmIntervalTimestamps, twmmIntervalValues, "TWMM");
}

int main() {
    cout << "=== Time-Weighted Moving Calculations ===" << endl;
    cout << endl;

    // Step 1: Load CSV data
    FileProcessor processor;
    auto [timestamps, prices] = processor.ReadSecurityCSV("security1.csv");

    if (timestamps.empty()) {
        cerr << "Error: No data loaded. Exiting." << endl;
        return 1;
    }

    cout << "First timestamp: " << timestamps[0] << endl;
    cout << "Last timestamp: " << timestamps.back() << endl;
    cout << endl;

    long long firstTimestamp = timestamps[0];
    long long lastTimestamp = timestamps.back();
    long long windowStart = firstTimestamp + WINDOW_SIZE_NS;

    // Step 2-5: Generate TWMA/TWSTD and TWMM at timestamps and intervals
    GenerateTWMATWSTDTimestamps(timestamps, prices, windowStart, processor);
    GenerateTWMATWSTDIntervals(timestamps, prices, windowStart, lastTimestamp, processor);
    GenerateTWMMTimestamps(timestamps, prices, windowStart, processor);
    GenerateTWMMIntervals(timestamps, prices, windowStart, lastTimestamp, processor);

    return 0;
}
