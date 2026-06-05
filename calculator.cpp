#include "calculator.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <functional>
#include <memory>

using namespace std;

/* TWMACalculator: accumulate prior segments, handle open tail at query time */

void TWMACalculator::PruneWindow(long long windowStart) {
    /* Remove data points that are older than the 30-second window start.
       Keeps O(n) operations feasible since we pop from front only. */
    while (!window.empty() && window.front().timestamp < windowStart) {
        window.pop_front();
    }
}

void TWMACalculator::AddNewValue(double price, long long timestamp) {
    /* When a new price arrives, finalize the previous price's contribution.
       Accumulate (previous_price × duration) into running sum, then push the new price.
       The new price's duration stays unknown until the next tick arrives. */
    if (!window.empty()) {
        long long prevTimestamp = window.back().timestamp;
        double prevPrice = window.back().price;
        long long duration = timestamp - prevTimestamp;
        
        twmaRunningSum += prevPrice * duration;
    }
    
    window.push_back(DataPoint(timestamp, price));
    lastPrice = price;
    
    long long windowStart = timestamp - WINDOW_SIZE_NS;
    PruneWindow(windowStart);
}

double TWMACalculator::GetCurrentValue(long long t) {
    /* Compute time-weighted average price over the 30-second window.
       Uses accumulated sum of completed segments plus the current open segment.
       Divides by total window duration to get the average. */
    if (window.empty()) {
        return 0.0;
    }
    
    long long windowStart = t - WINDOW_SIZE_NS;
    double weightedSum = twmaRunningSum;
    
    /* Add the last open segment (duration = time from last timestamp to now) */
    long long lastTimestamp = window.back().timestamp;
    long long lastEntryStart = max(lastTimestamp, windowStart);
    double lastEntryDuration = static_cast<double>(t - lastEntryStart);
    
    if (lastEntryDuration > 0) {
        weightedSum += lastPrice * lastEntryDuration;
    }
    
    return weightedSum / WINDOW_SIZE_NS;
}

double TWMACalculator::GetCurrentTWSTD(long long t) {
    /* Compute time-weighted standard deviation around the TWMA.
       Each price's squared deviation is weighted by how long it held during the window. */
    if (window.empty()) {
        return 0.0;
    }
    
    double twma = GetCurrentValue(t);
    
    long long windowStart = t - WINDOW_SIZE_NS;
    double sumSquaredDev = 0.0;
    
    for (size_t i = 0; i < window.size(); ++i) {
        long long entryStart = window[i].timestamp;
        long long entryEnd;
        
        if (i + 1 < window.size()) {
            entryEnd = window[i + 1].timestamp;
        } else {
            entryEnd = t;
        }
        
        entryStart = max(entryStart, windowStart);
        entryEnd = min(entryEnd, t);
        
        if (entryEnd > entryStart) {
            double duration = static_cast<double>(entryEnd - entryStart);
            double deviation = window[i].price - twma;
            sumSquaredDev += duration * deviation * deviation;
        }
    }
    
    double variance = sumSquaredDev / WINDOW_SIZE_NS;
    return sqrt(variance);
}

/* TWMMCalculator: build price-level CDF, find 50th percentile by time */

vector<TWMMCalculator::PriceDuration> TWMMCalculator::GetPriceLevels(long long t) const {
    /* Build a cumulative distribution of prices by time spent at each level.
       Returns sorted vector where each entry holds a price and cumulative time up to that price. */
    if (window.empty()) {
        return {};
    }

    long long windowStart = t - WINDOW_SIZE_NS;
    map<double, double> priceDurations;
    
    for (size_t i = 0; i < window.size(); ++i) {
        long long entryStart = window[i].timestamp;
        long long entryEnd;

        if (i + 1 < window.size()) {
            entryEnd = window[i + 1].timestamp;
        } else {
            entryEnd = t;
        }

        entryStart = max(entryStart, windowStart);
        entryEnd = min(entryEnd, t);

        if (entryEnd > entryStart) {
            double duration_ns = static_cast<double>(entryEnd - entryStart);
            priceDurations[window[i].price] += duration_ns;
        }
    }

    vector<PriceDuration> result;
    double cumulative = 0.0;

    for (const auto& [price, duration] : priceDurations) {
        cumulative += duration;
        result.push_back({price, cumulative});
    }

    return result;
}

void TWMMCalculator::AddNewValue(double price, long long timestamp) {
    /* Add a new price and prune out-of-window data.
       TWMM needs full price history to compute percentile distributions. */
    window.push_back(DataPoint(timestamp, price));
    
    long long windowStart = timestamp - WINDOW_SIZE_NS;
    while (!window.empty() && window.front().timestamp < windowStart) {
        window.pop_front();
    }
}

double TWMMCalculator::GetCurrentValue(long long t) {
    /* Find the time-weighted median price: the price where 50% of window time
       is at or below it, and 50% is at or above it. */
    if (window.empty()) {
        return 0.0;
    }

    auto priceLevels = GetPriceLevels(t);

    if (priceLevels.empty()) {
        return 0.0;
    }

    double totalDuration = priceLevels.back().cumulativeDuration;

    /* Find price where 15s of time at/below it AND 15s at/above it */
    for (size_t i = 0; i < priceLevels.size(); ++i) {
        double cumulativeUp = priceLevels[i].cumulativeDuration;
        double cumulativeDown = totalDuration - (i > 0 ? priceLevels[i - 1].cumulativeDuration : 0.0);

        if (cumulativeUp >= HALF_WINDOW_NS && cumulativeDown >= HALF_WINDOW_NS) {
            return priceLevels[i].price;
        }
    }

    double targetDuration = totalDuration / 2.0;
    for (size_t i = 0; i < priceLevels.size(); ++i) {
        if (priceLevels[i].cumulativeDuration >= targetDuration) {
            return priceLevels[i].price;
        }
    }

    return priceLevels.back().price;
}

/* FileProcessor: chunk-based CSV streaming */

pair<vector<long long>, vector<double>> 
FileProcessor::ReadSecurityCSV(const string& filename) {
    /* Load all security data from CSV in BUFFER_SIZE chunks to minimize disk I/O.
       Returns timestamps and prices as parallel vectors for window calculations. */
    vector<long long> timestamps;
    vector<double> prices;

    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Error: Cannot open file " << filename << endl;
        return {timestamps, prices};
    }

    string line;
    getline(file, line);

    vector<string> buffer;
    while (std::getline(file, line)) {
        buffer.push_back(line);

        if (buffer.size() >= BUFFER_SIZE) {
            for (const auto& bufLine : buffer) {
                stringstream ss(bufLine);
                double timestampDouble;
                double price;
                char comma;

                if (ss >> timestampDouble >> comma >> price) {
                    long long timestamp = static_cast<long long>(timestampDouble);
                    timestamps.push_back(timestamp);
                    prices.push_back(price);
                }
            }
            buffer.clear();
        }
    }

    for (const auto& bufLine : buffer) {
        stringstream ss(bufLine);
        double timestampDouble;
        double price;
        char comma;

        if (ss >> timestampDouble >> comma >> price) {
            long long timestamp = static_cast<long long>(timestampDouble);
            timestamps.push_back(timestamp);
            prices.push_back(price);
        }
    }

    file.close();
    cout << "Loaded " << timestamps.size() << " data points from " << filename << endl;
    return {timestamps, prices};
}

void FileProcessor::WriteCSV(const string& filename,
                             const vector<long long>& timestamps,
                             const vector<double>& values,
                             const string& valueHeader) {
    /* Write single-column time-series data to CSV with header.
       Uses BUFFER_SIZE chunking to reduce write syscalls. */
    ofstream file(filename);
    if (!file.is_open()) {
        cerr << "Error: Cannot open file " << filename << " for writing" << endl;
        return;
    }

    file << "Time," << valueHeader << "\n";

    vector<string> buffer;
    for (size_t i = 0; i < timestamps.size(); ++i) {
        stringstream ss;
        ss << fixed << setprecision(6);
        ss << timestamps[i] << "," << values[i];
        buffer.push_back(ss.str());

        if (buffer.size() >= BUFFER_SIZE) {
            // Flush buffer
            for (const auto& line : buffer) {
                file << line << "\n";
            }
            buffer.clear();
        }
    }

    // Flush remaining buffer
    for (const auto& line : buffer) {
        file << line << "\n";
    }

    file.close();
    cout << "Wrote " << timestamps.size() << " rows to " << filename << endl;
}

void FileProcessor::WriteCSV(const string& filename,
                             const vector<long long>& timestamps,
                             const vector<double>& values1,
                             const vector<double>& values2,
                             const string& header1,
                             const string& header2) {
    /* Write two-column time-series data to CSV with headers.
       Used for TWMA/TWSTD pairs (time + mean, time + std dev). */
    ofstream file(filename);
    if (!file.is_open()) {
        cerr << "Error: Cannot open file " << filename << " for writing" << endl;
        return;
    }

    file << "Time," << header1 << "," << header2 << "\n";

    vector<string> buffer;
    for (size_t i = 0; i < timestamps.size(); ++i) {
        stringstream ss;
        ss << fixed << setprecision(6);
        ss << timestamps[i] << "," << values1[i] << "," << values2[i];
        buffer.push_back(ss.str());

        if (buffer.size() >= BUFFER_SIZE) {
            for (const auto& line : buffer) {
                file << line << "\n";
            }
            buffer.clear();
        }
    }

    for (const auto& line : buffer) {
        file << line << "\n";
    }

    file.close();
    std::cout << "Wrote " << timestamps.size() << " rows to " << filename << std::endl;
}

/* Generic chunk processor: read → parse → callback → write.
   MULTITHREADING: read/compute can overlap via double-buffer (50 lines). */

void FileProcessor::ProcessChunkedData(
    /* Streams CSV in BUFFER_SIZE chunks, applies callback to each row, writes output.
       Callback computes values; finalize callback handles remaining intervals after EOF. */
    const string& csvFilename,
    const string& outputFilename,
    const string& header,
    long long windowStart,
    long long lastTimestamp,
    CallbackFunc callback,
    FinalizeCallback finalize) {
    
    ifstream inFile(csvFilename);
    ofstream outFile(outputFilename);
    outFile << header << "\n";
    
    string line;
    getline(inFile, line);
    
    int rowsWritten = 0;
    vector<string> readBuffer;
    readBuffer.reserve(BUFFER_SIZE);
    
    while (true) {
        readBuffer.clear();
        while ((int)readBuffer.size() < BUFFER_SIZE && getline(inFile, line)) {
            if (!line.empty()) readBuffer.push_back(line);
        }
        if (readBuffer.empty()) break;
        
        stringstream writeBuffer;
        writeBuffer << fixed << setprecision(6);
        for (const auto& bufLine : readBuffer) {
            stringstream ss(bufLine);
            string tsStr, priceStr;
            getline(ss, tsStr, ',');
            getline(ss, priceStr, ',');
            long long timestamp = static_cast<long long>(stod(tsStr));
            double price = stod(priceStr);
            
            callback(timestamp, price, writeBuffer, rowsWritten);
        }
        
        outFile << writeBuffer.str();
    }
    
    if (finalize) {
        stringstream writeBuffer;
        writeBuffer << fixed << setprecision(6);
        finalize(writeBuffer, rowsWritten);
        outFile << writeBuffer.str();
    }
    
    inFile.close();
    outFile.close();
    cout << "Wrote " << rowsWritten << " rows to " << outputFilename << endl;
}

void FileProcessor::ProcessTWMATimestamps(const string& csvFilename, long long windowStart,
                                          long long lastTimestamp) {
    /* Compute time-weighted mean and standard deviation at each input timestamp.
       Outputs one row per data point in the window. */
    cout << "Generating TWMA/TWSTD at input timestamps..." << endl;
    
    auto twmaCalc = make_shared<TWMACalculator>();
    
    auto callback = [&](long long timestamp, double price, stringstream& writeBuffer, int& rowsWritten) {
        twmaCalc->AddNewValue(price, timestamp);
        if (timestamp >= windowStart) {
            writeBuffer << timestamp << ","
                        << twmaCalc->GetCurrentValue(timestamp) << ","
                        << twmaCalc->GetCurrentTWSTD(timestamp) << "\n";
            ++rowsWritten;
        }
    };
    
    ProcessChunkedData(csvFilename, "twma_twstd_timestamps.csv", "Time,TWMA,TWSTD",
                       windowStart, lastTimestamp, callback);
}

void FileProcessor::ProcessTWMAIntervals(const string& csvFilename, long long windowStart,
                                         long long lastTimestamp) {
    /* Compute time-weighted mean and standard deviation at fixed 1-second intervals.
       Generates uniform time grid output even if data is sparse. */
    cout << "Generating TWMA/TWSTD at 1-second intervals..." << endl;
    
    auto twmaCalc = make_shared<TWMACalculator>();
    long long currentIntervalTime = windowStart;
    
    auto callback = [&](long long timestamp, double price, stringstream& writeBuffer, int& rowsWritten) {
        twmaCalc->AddNewValue(price, timestamp);
        while (currentIntervalTime <= timestamp && currentIntervalTime <= lastTimestamp) {
            writeBuffer << currentIntervalTime << ","
                        << twmaCalc->GetCurrentValue(currentIntervalTime) << ","
                        << twmaCalc->GetCurrentTWSTD(currentIntervalTime) << "\n";
            ++rowsWritten;
            currentIntervalTime += 1000000000LL;
        }
    };
    
    auto finalize = [&](stringstream& writeBuffer, int& rowsWritten) {
        while (currentIntervalTime <= lastTimestamp) {
            writeBuffer << currentIntervalTime << ","
                        << twmaCalc->GetCurrentValue(currentIntervalTime) << ","
                        << twmaCalc->GetCurrentTWSTD(currentIntervalTime) << "\n";
            ++rowsWritten;
            currentIntervalTime += 1000000000LL;
        }
    };
    
    ProcessChunkedData(csvFilename, "twma_twstd_intervals.csv", "Time,TWMA,TWSTD",
                       windowStart, lastTimestamp, callback, finalize);
}

void FileProcessor::ProcessTWMMTimestamps(const string& csvFilename, long long windowStart,
                                          long long lastTimestamp) {
    /* Compute time-weighted median price at each input timestamp.
       Outputs one row per data point in the window. */
    cout << "Generating TWMM at input timestamps..." << endl;
    
    auto twmmCalc = make_shared<TWMMCalculator>();
    
    auto callback = [&](long long timestamp, double price, stringstream& writeBuffer, int& rowsWritten) {
        twmmCalc->AddNewValue(price, timestamp);
        if (timestamp >= windowStart) {
            writeBuffer << timestamp << ","
                        << twmmCalc->GetCurrentValue(timestamp) << "\n";
            ++rowsWritten;
        }
    };
    
    ProcessChunkedData(csvFilename, "twmm_timestamps.csv", "Time,TWMM",
                       windowStart, lastTimestamp, callback);
}

void FileProcessor::ProcessTWMMIntervals(const string& csvFilename, long long windowStart,
                                         long long lastTimestamp) {
    /* Compute time-weighted median price at fixed 1-second intervals.
       Generates uniform time grid output even if data is sparse. */
    cout << "Generating TWMM at 1-second intervals..." << endl;
    
    auto twmmCalc = make_shared<TWMMCalculator>();
    long long currentIntervalTime = windowStart;
    
    auto callback = [&](long long timestamp, double price, stringstream& writeBuffer, int& rowsWritten) {
        twmmCalc->AddNewValue(price, timestamp);
        while (currentIntervalTime <= timestamp && currentIntervalTime <= lastTimestamp) {
            writeBuffer << currentIntervalTime << ","
                        << twmmCalc->GetCurrentValue(currentIntervalTime) << "\n";
            ++rowsWritten;
            currentIntervalTime += 1000000000LL;
        }
    };
    
    auto finalize = [&](stringstream& writeBuffer, int& rowsWritten) {
        while (currentIntervalTime <= lastTimestamp) {
            writeBuffer << currentIntervalTime << ","
                        << twmmCalc->GetCurrentValue(currentIntervalTime) << "\n";
            ++rowsWritten;
            currentIntervalTime += 1000000000LL;
        }
    };
    
    ProcessChunkedData(csvFilename, "twmm_intervals.csv", "Time,TWMM",
                       windowStart, lastTimestamp, callback, finalize);
}
