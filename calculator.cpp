#include "calculator.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <map>

using namespace std;

// ==================== TWMACalculator Implementation ====================

void TWMACalculator::PruneWindow(long long windowStart) {
    // Remove entries older than windowStart from the front of deque
    while (!window.empty() && window.front().timestamp < windowStart) {
        window.pop_front();
    }
}

void TWMACalculator::AddNewValue(double price, long long timestamp) {
    // If we have a previous entry, calculate its duration and update running sums
    if (!window.empty()) {
        long long prevTimestamp = window.back().timestamp;
        double prevPrice = window.back().price;
        long long duration = timestamp - prevTimestamp;
        
        // Add to weighted sum
        twmaRunningSum += prevPrice * duration;
        
        // For TWSTD calculation (we'll need TWMA later, so just accumulate for now)
        // We'll calculate deviations in GetCurrentTWSTD
    }
    
    // Add new entry
    window.push_back(DataPoint(timestamp, price));
    lastPrice = price;
    
    // Prune entries older than 30 seconds
    long long windowStart = timestamp - WINDOW_SIZE_NS;
    PruneWindow(windowStart);
}

double TWMACalculator::GetCurrentValue(long long t) {
    if (window.empty()) {
        return 0.0;
    }
    
    long long windowStart = t - WINDOW_SIZE_NS;
    
    // Start with running sum of completed entries
    double weightedSum = twmaRunningSum;
    
    // Add contribution of the last entry (its duration is from its timestamp to query time)
    long long lastTimestamp = window.back().timestamp;
    long long lastEntryStart = max(lastTimestamp, windowStart);
    double lastEntryDuration = static_cast<double>(t - lastEntryStart);
    
    if (lastEntryDuration > 0) {
        weightedSum += lastPrice * lastEntryDuration;
    }
    
    return weightedSum / WINDOW_SIZE_NS;
}

double TWMACalculator::GetCurrentTWSTD(long long t) {
    if (window.empty()) {
        return 0.0;
    }
    
    double twma = GetCurrentValue(t);
    
    long long windowStart = t - WINDOW_SIZE_NS;
    double sumSquaredDev = 0.0;
    
    // Calculate sum of squared deviations for all entries in window
    for (size_t i = 0; i < window.size(); ++i) {
        long long entryStart = window[i].timestamp;
        long long entryEnd;
        
        // Determine end time of this entry
        if (i + 1 < window.size()) {
            entryEnd = window[i + 1].timestamp;
        } else {
            entryEnd = t;
        }
        
        // Clip to window boundaries
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

// ==================== TWMMCalculator Implementation ====================

vector<TWMMCalculator::PriceDuration> TWMMCalculator::GetPriceLevels(long long t) const {
    if (window.empty()) {
        return {};
    }

    long long windowStart = t - WINDOW_SIZE_NS;

    // Map of price to cumulative duration
    map<double, double> priceDurations;
    
    // Calculate durations for each entry
    for (size_t i = 0; i < window.size(); ++i) {
        long long entryStart = window[i].timestamp;
        long long entryEnd;

        if (i + 1 < window.size()) {
            entryEnd = window[i + 1].timestamp;
        } else {
            entryEnd = t;
        }

        // Clip to window boundaries
        entryStart = max(entryStart, windowStart);
        entryEnd = min(entryEnd, t);

        if (entryEnd > entryStart) {
            double duration_ns = static_cast<double>(entryEnd - entryStart);
            priceDurations[window[i].price] += duration_ns;
        }
    }

    // Convert map to sorted vector
    vector<PriceDuration> result;
    double cumulative = 0.0;

    for (const auto& [price, duration] : priceDurations) {
        cumulative += duration;
        result.push_back({price, cumulative});
    }

    return result;
}

void TWMMCalculator::AddNewValue(double price, long long timestamp) {
    window.push_back(DataPoint(timestamp, price));
    
    // Prune entries older than 30 seconds
    long long windowStart = timestamp - WINDOW_SIZE_NS;
    while (!window.empty() && window.front().timestamp < windowStart) {
        window.pop_front();
    }
}

double TWMMCalculator::GetCurrentValue(long long t) {
    if (window.empty()) {
        return 0.0;
    }

    auto priceLevels = GetPriceLevels(t);

    if (priceLevels.empty()) {
        return 0.0;
    }

    // Total duration in window
    double totalDuration = priceLevels.back().cumulativeDuration;

    // Find the median: price where cumulative >= 15s and (total - cumulative) >= 15s
    for (size_t i = 0; i < priceLevels.size(); ++i) {
        double cumulativeUp = priceLevels[i].cumulativeDuration;
        double cumulativeDown = totalDuration - (i > 0 ? priceLevels[i - 1].cumulativeDuration : 0.0);

        if (cumulativeUp >= HALF_WINDOW_NS && cumulativeDown >= HALF_WINDOW_NS) {
            return priceLevels[i].price;
        }
    }

    // If no exact median found, return the middle price by cumulative duration
    double targetDuration = totalDuration / 2.0;
    for (size_t i = 0; i < priceLevels.size(); ++i) {
        if (priceLevels[i].cumulativeDuration >= targetDuration) {
            return priceLevels[i].price;
        }
    }

    return priceLevels.back().price;
}

// ==================== FileProcessor Implementation ====================

pair<vector<long long>, vector<double>> 
FileProcessor::ReadSecurityCSV(const string& filename) {
    vector<long long> timestamps;
    vector<double> prices;

    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Error: Cannot open file " << filename << endl;
        return {timestamps, prices};
    }

    string line;
    
    // Skip header
    getline(file, line);

    // Read data in buffers
    vector<string> buffer;
    while (std::getline(file, line)) {
        buffer.push_back(line);

        if (buffer.size() >= BUFFER_SIZE) {
            // Process buffer
            for (const auto& bufLine : buffer) {
                stringstream ss(bufLine);
                double timestampDouble;
                double price;
                char comma;

                if (ss >> timestampDouble >> comma >> price) {
                    // Convert double timestamp to long long (nanoseconds)
                    long long timestamp = static_cast<long long>(timestampDouble);
                    timestamps.push_back(timestamp);
                    prices.push_back(price);
                }
            }
            buffer.clear();
        }
    }

    // Process remaining buffer
    for (const auto& bufLine : buffer) {
        stringstream ss(bufLine);
        double timestampDouble;
        double price;
        char comma;

        if (ss >> timestampDouble >> comma >> price) {
            // Convert double timestamp to long long (nanoseconds)
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
    ofstream file(filename);
    if (!file.is_open()) {
        cerr << "Error: Cannot open file " << filename << " for writing" << endl;
        return;
    }

    // Write header
    file << "Time," << valueHeader << "\n";

    // Write data in buffers
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
    ofstream file(filename);
    if (!file.is_open()) {
        cerr << "Error: Cannot open file " << filename << " for writing" << endl;
        return;
    }

    // Write header
    file << "Time," << header1 << "," << header2 << "\n";

    // Write data in buffers
    vector<string> buffer;
    for (size_t i = 0; i < timestamps.size(); ++i) {
        stringstream ss;
        ss << fixed << setprecision(6);
        ss << timestamps[i] << "," << values1[i] << "," << values2[i];
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
    std::cout << "Wrote " << timestamps.size() << " rows to " << filename << std::endl;
}

// ==================== Multithreading Optimization Opportunities ====================
// CURRENT ARCHITECTURE: Sequential chunk processing (single-threaded)
//   Read BUFFER_SIZE lines -> Calculate -> Write results -> Repeat
//
// STRATEGY 1: Producer-Consumer with Queue (Recommended)
//   - Thread 1 (Reader):     Reads CSV chunks, pushes to thread-safe queue
//   - Thread 2 (Processor):  Pops chunks, calculates sequentially, pushes results
//   - Thread 3 (Writer):     Pops results, writes to file
//   - Benefit: I/O and calculation overlap; hides latency
//   - Implementation: std::queue + std::mutex + std::condition_variable
//   - Constraint: Calculator must remain sequential (30s sliding window state)
//
// STRATEGY 2: Read-Ahead with std::async
//   - Main thread processes current chunk
//   - Background thread reads next chunk asynchronously
//   - When done, next chunk ready in memory
//   - Benefit: Simpler than thread pool; good for prefetch
//   - Implementation: std::future<vector<string>> nextChunk
//   - Limitation: Only hides read latency, not write/calculate
//
// STRATEGY 3: Parallel Writing Only
//   - Keep calculation sequential, offload writes to separate thread
//   - Main thread: process chunks, push results to write queue
//   - Writer thread: drain write queue, flush to file
//   - Benefit: Easiest to implement; reduces write blocking
//   - Implementation: Simple queue + background writer thread
//
// KEY CONSTRAINT: Sequential Calculation
//   - 30-second sliding window requires ordered state transitions
//   - Chunk N state depends on final state of chunk N-1
//   - Cannot parallelize across chunks
//   - Only option: parallelize I/O around sequential calculation
//
// NEXT STEPS FOR MULTITHREADING:
//   1. Add thread-safe queue wrapper class
//   2. Create reader thread, populate queue with raw chunks
//   3. Keep processor sequential, consume from queue
//   4. Create writer thread, process output queue

void FileProcessor::ProcessTWMATimestamps(const string& csvFilename, long long windowStart,
                                          long long lastTimestamp) {
    cout << "Generating TWMA/TWSTD at input timestamps..." << endl;
    int rowsWritten = 0;

    TWMACalculator twmaCalc;
    ifstream inFile(csvFilename);
    ofstream outFile("twma_twstd_timestamps.csv");
    outFile << "Time,TWMA,TWSTD\n";

    string line;
    getline(inFile, line);  // Skip header

    vector<string> readBuffer;
    readBuffer.reserve(BUFFER_SIZE);

    while (true) {
        // Read a chunk of BUFFER_SIZE lines
        readBuffer.clear();
        while ((int)readBuffer.size() < BUFFER_SIZE && getline(inFile, line)) {
            if (!line.empty()) readBuffer.push_back(line);
        }
        if (readBuffer.empty()) break;

        // Process chunk: calculate results for each line
        stringstream writeBuffer;
        writeBuffer << fixed << setprecision(6);
        for (const auto& bufLine : readBuffer) {
            stringstream ss(bufLine);
            string tsStr, priceStr;
            getline(ss, tsStr, ',');
            getline(ss, priceStr, ',');
            long long timestamp = static_cast<long long>(stod(tsStr));
            double price = stod(priceStr);
            twmaCalc.AddNewValue(price, timestamp);
            if (timestamp >= windowStart) {
                writeBuffer << timestamp << ","
                            << twmaCalc.GetCurrentValue(timestamp) << ","
                            << twmaCalc.GetCurrentTWSTD(timestamp) << "\n";
                ++rowsWritten;
            }
        }

        // Write chunk results to file
        outFile << writeBuffer.str();
    }

    inFile.close();
    outFile.close();
    cout << "Wrote " << rowsWritten << " rows to twma_twstd_timestamps.csv" << endl;
}

// ===== MULTITHREADING NOTE: Same parallelization pattern applies to interval processing =====
// Chunk pipeline can use producer-consumer queue for I/O overlap
// Calculation still sequential: must process 1-second intervals in order
void FileProcessor::ProcessTWMAIntervals(const string& csvFilename, long long windowStart,
                                         long long lastTimestamp) {
    cout << "Generating TWMA/TWSTD at 1-second intervals..." << endl;
    int rowsWritten = 0;

    TWMACalculator twmaCalc;
    ifstream inFile(csvFilename);
    ofstream outFile("twma_twstd_intervals.csv");
    outFile << "Time,TWMA,TWSTD\n";

    string line;
    getline(inFile, line);  // Skip header

    long long currentIntervalTime = windowStart;
    vector<string> readBuffer;
    readBuffer.reserve(BUFFER_SIZE);

    while (true) {
        // Read a chunk of BUFFER_SIZE lines
        readBuffer.clear();
        while ((int)readBuffer.size() < BUFFER_SIZE && getline(inFile, line)) {
            if (!line.empty()) readBuffer.push_back(line);
        }
        if (readBuffer.empty()) break;

        // Process chunk: advance intervals as data arrives
        stringstream writeBuffer;
        writeBuffer << fixed << setprecision(6);
        for (const auto& bufLine : readBuffer) {
            stringstream ss(bufLine);
            string tsStr, priceStr;
            getline(ss, tsStr, ',');
            getline(ss, priceStr, ',');
            long long timestamp = static_cast<long long>(stod(tsStr));
            double price = stod(priceStr);
            twmaCalc.AddNewValue(price, timestamp);
            while (currentIntervalTime <= timestamp && currentIntervalTime <= lastTimestamp) {
                writeBuffer << currentIntervalTime << ","
                            << twmaCalc.GetCurrentValue(currentIntervalTime) << ","
                            << twmaCalc.GetCurrentTWSTD(currentIntervalTime) << "\n";
                ++rowsWritten;
                currentIntervalTime += 1000000000LL;
            }
        }

        // Write chunk results to file
        outFile << writeBuffer.str();
    }

    // Write any remaining intervals after all data
    stringstream writeBuffer;
    writeBuffer << fixed << setprecision(6);
    while (currentIntervalTime <= lastTimestamp) {
        writeBuffer << currentIntervalTime << ","
                    << twmaCalc.GetCurrentValue(currentIntervalTime) << ","
                    << twmaCalc.GetCurrentTWSTD(currentIntervalTime) << "\n";
        ++rowsWritten;
        currentIntervalTime += 1000000000LL;
    }
    outFile << writeBuffer.str();

    inFile.close();
    outFile.close();
    cout << "Wrote " << rowsWritten << " rows to twma_twstd_intervals.csv" << endl;
}

// ===== MULTITHREADING NOTE: TWMM uses same chunk pipeline pattern =====
// All 4 process methods can independently use producer-consumer queues
// Multiple Process methods could run in parallel (separate threads) since they
// operate on separate calculator instances and output files
void FileProcessor::ProcessTWMMTimestamps(const string& csvFilename, long long windowStart,
                                          long long lastTimestamp) {
    cout << "Generating TWMM at input timestamps..." << endl;
    int rowsWritten = 0;

    TWMMCalculator twmmCalc;
    ifstream inFile(csvFilename);
    ofstream outFile("twmm_timestamps.csv");
    outFile << "Time,TWMM\n";

    string line;
    getline(inFile, line);  // Skip header

    vector<string> readBuffer;
    readBuffer.reserve(BUFFER_SIZE);

    while (true) {
        // Read a chunk of BUFFER_SIZE lines
        readBuffer.clear();
        while ((int)readBuffer.size() < BUFFER_SIZE && getline(inFile, line)) {
            if (!line.empty()) readBuffer.push_back(line);
        }
        if (readBuffer.empty()) break;

        // Process chunk: calculate results for each line
        stringstream writeBuffer;
        writeBuffer << fixed << setprecision(6);
        for (const auto& bufLine : readBuffer) {
            stringstream ss(bufLine);
            string tsStr, priceStr;
            getline(ss, tsStr, ',');
            getline(ss, priceStr, ',');
            long long timestamp = static_cast<long long>(stod(tsStr));
            double price = stod(priceStr);
            twmmCalc.AddNewValue(price, timestamp);
            if (timestamp >= windowStart) {
                writeBuffer << timestamp << ","
                            << twmmCalc.GetCurrentValue(timestamp) << "\n";
                ++rowsWritten;
            }
        }

        // Write chunk results to file
        outFile << writeBuffer.str();
    }

    inFile.close();
    outFile.close();
    cout << "Wrote " << rowsWritten << " rows to twmm_timestamps.csv" << endl;
}

// ===== MULTITHREADING OPPORTUNITY: Process all 4 methods in parallel from main() =====
// Currently: main() calls Process* methods sequentially
// Optimization: Spawn 4 threads in main(), one for each Process* method
// Benefits: 4x wall-clock speedup (each processes independently with own calculator/file)
// Implementation: std::thread t1(...), t2(...), t3(...), t4(...); t1.join(); t2.join(); etc.
void FileProcessor::ProcessTWMMIntervals(const string& csvFilename, long long windowStart,
                                         long long lastTimestamp) {
    cout << "Generating TWMM at 1-second intervals..." << endl;
    int rowsWritten = 0;

    TWMMCalculator twmmCalc;
    ifstream inFile(csvFilename);
    ofstream outFile("twmm_intervals.csv");
    outFile << "Time,TWMM\n";

    string line;
    getline(inFile, line);  // Skip header

    long long currentIntervalTime = windowStart;
    vector<string> readBuffer;
    readBuffer.reserve(BUFFER_SIZE);

    while (true) {
        // Read a chunk of BUFFER_SIZE lines
        readBuffer.clear();
        while ((int)readBuffer.size() < BUFFER_SIZE && getline(inFile, line)) {
            if (!line.empty()) readBuffer.push_back(line);
        }
        if (readBuffer.empty()) break;

        // Process chunk: advance intervals as data arrives
        stringstream writeBuffer;
        writeBuffer << fixed << setprecision(6);
        for (const auto& bufLine : readBuffer) {
            stringstream ss(bufLine);
            string tsStr, priceStr;
            getline(ss, tsStr, ',');
            getline(ss, priceStr, ',');
            long long timestamp = static_cast<long long>(stod(tsStr));
            double price = stod(priceStr);
            twmmCalc.AddNewValue(price, timestamp);
            while (currentIntervalTime <= timestamp && currentIntervalTime <= lastTimestamp) {
                writeBuffer << currentIntervalTime << ","
                            << twmmCalc.GetCurrentValue(currentIntervalTime) << "\n";
                ++rowsWritten;
                currentIntervalTime += 1000000000LL;
            }
        }

        // Write chunk results to file
        outFile << writeBuffer.str();
    }

    // Write any remaining intervals after all data
    stringstream writeBuffer;
    writeBuffer << fixed << setprecision(6);
    while (currentIntervalTime <= lastTimestamp) {
        writeBuffer << currentIntervalTime << ","
                    << twmmCalc.GetCurrentValue(currentIntervalTime) << "\n";
        ++rowsWritten;
        currentIntervalTime += 1000000000LL;
    }
    outFile << writeBuffer.str();

    inFile.close();
    outFile.close();
    cout << "Wrote " << rowsWritten << " rows to twmm_intervals.csv" << endl;
}
