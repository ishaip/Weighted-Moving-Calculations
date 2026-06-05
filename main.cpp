#include "calculator.h"
#include <iostream>
#include <vector>
#include <iomanip>
#include <chrono>
#include <deque>

using namespace std;

// Helper struct for sliding window calculations
struct WindowEntry {
    long long timestamp;
    double price;
};

// Calculate TWMA from a window of entries at query time t
double CalculateTWMAFromWindow(const deque<WindowEntry>& window, long long t) {
    if (window.empty()) return 0.0;
    
    double weightedSum = 0.0;
    for (size_t i = 0; i < window.size(); ++i) {
        long long endTime = (i + 1 < window.size()) ? window[i + 1].timestamp : t;
        double duration_ns = static_cast<double>(endTime - window[i].timestamp);
        weightedSum += window[i].price * duration_ns;
    }
    
    return weightedSum / 30000000000LL;  // 30 seconds in nanoseconds
}

// Calculate TWSTD from a window of entries at query time t
double CalculateTWSTDFromWindow(const deque<WindowEntry>& window, long long t) {
    if (window.empty()) return 0.0;
    
    double twma = CalculateTWMAFromWindow(window, t);
    double sumSquaredDev = 0.0;
    
    for (size_t i = 0; i < window.size(); ++i) {
        long long endTime = (i + 1 < window.size()) ? window[i + 1].timestamp : t;
        double duration_ns = static_cast<double>(endTime - window[i].timestamp);
        double deviation = window[i].price - twma;
        sumSquaredDev += duration_ns * deviation * deviation;
    }
    
    return std::sqrt(sumSquaredDev / 30000000000LL);
}

int main() {
    auto startTime = chrono::high_resolution_clock::now();

    cout << "=== Time-Weighted Moving Calculations ===" << endl;
    cout << endl;

    // Step 1: Load CSV data
    FileProcessor processor;
    auto [timestamps, prices] = processor.ReadSecurityCSV("security1.csv");

    if (timestamps.empty()) {
        std::cerr << "Error: No data loaded. Exiting." << std::endl;
        return 1;
    }

    cout << "First timestamp: " << timestamps[0] << endl;
    cout << "Last timestamp: " << timestamps.back() << endl;
    cout << endl;

    // Constants
    long long FIRST_TIMESTAMP = timestamps[0];
    long long LAST_TIMESTAMP = timestamps.back();
    long long WINDOW_SIZE_NS = 30000000000LL;
    long long WINDOW_START = FIRST_TIMESTAMP + WINDOW_SIZE_NS;
    long long INTERVAL_NS = 1000000000LL;

    // Step 2: Generate TWMA/TWSTD at timestamps using sliding window
    cout << "Generating TWMA/TWSTD at input timestamps..." << endl;
    vector<long long> twmaTwstdTimestamps;
    vector<double> twmaValues;
    vector<double> twstdValues;

    deque<WindowEntry> slidingWindow;
    size_t dataIdx = 0;

    // Build initial window up to 30 seconds
    while (dataIdx < timestamps.size() && timestamps[dataIdx] < WINDOW_START) {
        slidingWindow.push_back({timestamps[dataIdx], prices[dataIdx]});
        dataIdx++;
    }

    // Process queries at each data point after 30 seconds
    while (dataIdx < timestamps.size()) {
        long long queryTime = timestamps[dataIdx];
        
        // Remove entries older than 30 seconds
        while (!slidingWindow.empty() && (queryTime - slidingWindow.front().timestamp) > WINDOW_SIZE_NS) {
            slidingWindow.pop_front();
        }
        
        // Add new entry
        slidingWindow.push_back({timestamps[dataIdx], prices[dataIdx]});
        
        // Calculate and store
        twmaTwstdTimestamps.push_back(queryTime);
        twmaValues.push_back(CalculateTWMAFromWindow(slidingWindow, queryTime));
        twstdValues.push_back(CalculateTWSTDFromWindow(slidingWindow, queryTime));
        
        dataIdx++;
    }

    processor.WriteCSV("twma_twstd_timestamps.csv", twmaTwstdTimestamps, twmaValues, 
                       twstdValues, "TWMA", "TWSTD");
    cout << endl;

    // Step 3: Generate TWMA/TWSTD at 1-second intervals using incremental approach
    cout << "Generating TWMA/TWSTD at 1-second intervals..." << endl;
    vector<long long> twmaIntervalTimestamps;
    vector<double> twmaIntervalValues;
    vector<double> twstdIntervalValues;

    slidingWindow.clear();
    dataIdx = 0;
    long long currentIntervalTime = WINDOW_START;

    // Build initial window
    while (dataIdx < timestamps.size() && timestamps[dataIdx] < WINDOW_START) {
        slidingWindow.push_back({timestamps[dataIdx], prices[dataIdx]});
        dataIdx++;
    }

    // Process 1-second intervals
    while (currentIntervalTime <= LAST_TIMESTAMP) {
        // Add any new data points up to this interval
        while (dataIdx < timestamps.size() && timestamps[dataIdx] <= currentIntervalTime) {
            slidingWindow.push_back({timestamps[dataIdx], prices[dataIdx]});
            dataIdx++;
        }
        
        // Remove entries older than 30 seconds
        while (!slidingWindow.empty() && (currentIntervalTime - slidingWindow.front().timestamp) > WINDOW_SIZE_NS) {
            slidingWindow.pop_front();
        }
        
        // Calculate and store
        twmaIntervalTimestamps.push_back(currentIntervalTime);
        twmaIntervalValues.push_back(CalculateTWMAFromWindow(slidingWindow, currentIntervalTime));
        twstdIntervalValues.push_back(CalculateTWSTDFromWindow(slidingWindow, currentIntervalTime));
        
        currentIntervalTime += INTERVAL_NS;
    }

    processor.WriteCSV("twma_twstd_intervals.csv", twmaIntervalTimestamps, twmaIntervalValues,
                       twstdIntervalValues, "TWMA", "TWSTD");
    cout << endl;

    // Step 4 & 5: Generate TWMM at timestamps and intervals using sliding window
    cout << "Generating TWMM at input timestamps..." << endl;
    vector<long long> twmmTimestamps;
    vector<double> twmmTimestampValues;

    // TWMM calculation inline using sorted prices
    deque<WindowEntry> twmmSlidingWindow;
    dataIdx = 0;

    // Build initial window up to 30 seconds
    while (dataIdx < timestamps.size() && timestamps[dataIdx] < WINDOW_START) {
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
        // Create a map of price -> total duration
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

    cout << "Generating TWMM at 1-second intervals..." << endl;
    vector<long long> twmmIntervalTimestamps;
    vector<double> twmmIntervalValues;

    twmmSlidingWindow.clear();
    dataIdx = 0;
    currentIntervalTime = WINDOW_START;

    // Build initial window
    while (dataIdx < timestamps.size() && timestamps[dataIdx] < WINDOW_START) {
        twmmSlidingWindow.push_back({timestamps[dataIdx], prices[dataIdx]});
        dataIdx++;
    }

    // Process 1-second intervals
    while (currentIntervalTime <= LAST_TIMESTAMP) {
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
    cout << endl;

    // Timing summary
    auto endTime = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(endTime - startTime);

    cout << "=== Execution Summary ===" << endl;
    cout << "Total time: " << duration.count() << " ms" << endl;
    cout << "Output files generated:" << endl;
    cout << "  - twma_twstd_timestamps.csv (" << twmaTwstdTimestamps.size() << " rows)" << endl;
    cout << "  - twma_twstd_intervals.csv (" << twmaIntervalTimestamps.size() << " rows)" << endl;
    cout << "  - twmm_timestamps.csv (" << twmmTimestamps.size() << " rows)" << endl;
    cout << "  - twmm_intervals.csv (" << twmmIntervalTimestamps.size() << " rows)" << endl;

    return 0;
}
